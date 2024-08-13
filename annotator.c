#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <cairo-pdf.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include "util.h"
#include "annotator.h"


#define MAX_MARKER_DIST 100000.0
#define MIN_MARKER_DIST 500.0

#define FUZZ_RANGE   100.
#define FUZZ_PIXEL_Y 4


const float Rearth = 6371000.0;

#define LABEL_CROSSHAIR_R 3
#define TEXT_MARGIN       2

static const double POINTS_PER_INCH = 72.;
static const double PIXELS_PER_INCH = 300.;
static const double CAIRO_SCALE     = POINTS_PER_INCH / PIXELS_PER_INCH;

static int font_height = 20;

static
double string_width(cairo_t *cr,
                    const char* s)
{
  cairo_text_extents_t extents;
  cairo_text_extents(cr, s,
                     &extents);
  return extents.x_bearing+extents.width;
}




typedef struct
{
  float x,y;
} xy_t;

// compares two POIs by their draw_x. Sorts disabled POIs to the end
static int compar_poi_x( const void* _idx0, const void* _idx1, void* cookie )
{
  const xy_t* xy   = (const xy_t*)cookie;
  const int*  idx0 = (const int*)_idx0;
  const int*  idx1 = (const int*)_idx1;

  if( xy[ *idx0 ].x < xy[ *idx1 ].x )
    return -1;
  else
    return 1;
}

static
void draw_label( cairo_t* cr,
                 double x, double y,
                 // top of the label
                 double y_label, const char* name )
{
  cairo_move_to(cr, x-LABEL_CROSSHAIR_R, y);
  cairo_rel_line_to(cr, 2*LABEL_CROSSHAIR_R, 0);

  cairo_move_to(cr, x, y+LABEL_CROSSHAIR_R);
  cairo_line_to(cr, x, y_label);

  cairo_stroke(cr);


  // cairo wants the bottom of the label
  cairo_move_to(cr, x, y_label + font_height);
  cairo_show_text(cr, name);
}

// Unwraps an angle x to lie within pi of an angle near. All angles in radians
// Copy from vertex.glsl
static
double unwrap_near_rad(double x, double near)
{
    double d = (x - near) / (2.*M_PI);
    return (d - round(d)) * 2.*M_PI + near;
}


#define TRY(x) do {                             \
    if(!(x))                                    \
    {                                           \
      MSG( "ERROR: " #x " failed");             \
      goto done;                                \
    }                                           \
  } while(0)



// My image stores a pixel in 24 bits, while cairo expects 32 bits (despite the
// name of the format being CAIRO_FORMAT_RGB24). I convert with this function
//
// Dense storage assumed in both input and output.
static
bool RGB32_from_BGR24(// output
                      uint8_t* image_rgb32,
                      // input
                      const uint8_t* image_bgr24,
                      const int width,
                      const int height)
{
  bool result = false;
  int stride_rgb32 = width*4;
  int stride_bgr24 = width*3;

  struct SwsContext* sws_ctx = NULL;
  TRY(NULL != (sws_ctx =
               sws_getContext(width, height, AV_PIX_FMT_BGR24,
                              width, height, AV_PIX_FMT_RGB32,
                              SWS_POINT, NULL, NULL, NULL)));

  sws_scale(sws_ctx,
            &image_bgr24, &stride_bgr24, 0, height,
            &image_rgb32, &stride_rgb32);
  result = true;

done:
  if(sws_ctx != NULL)
    sws_freeContext(sws_ctx);
  return result;
}

bool annotate(// input
              const char* pdf_filename,
              // assumed to be stored densely.
              const uint8_t* image_bgr,
              const float*   range_image,
              const int width,
              const int height,

              const poi_t* pois,
              const int Npois,
              const double lat,
              const double lon,
              const double az_deg0,
              const double az_deg1,
              const double ele_m)
{
  bool result = false;

  // For sorting, further down
  int poi_indices[Npois];
  int Npoi_indices = 0;

  xy_t labels_xy[Npois];

  uint8_t*         image_rgb32 = NULL;
  cairo_surface_t* pdf         = NULL;
  cairo_t*         cr          = NULL;
  cairo_surface_t* frame       = NULL;

  ////// Paint the render into the pdf
  TRY(NULL != (image_rgb32 = malloc(width*height*4)));

  TRY(RGB32_from_BGR24(// output
                       image_rgb32,
                       // input
                       image_bgr,
                       width,
                       height));

  TRY(NULL !=
      (pdf = cairo_pdf_surface_create(pdf_filename,
                                      width  * CAIRO_SCALE,
                                      height * CAIRO_SCALE)));
  TRY(NULL != (cr = cairo_create(pdf)));
  cairo_scale(cr, CAIRO_SCALE, CAIRO_SCALE);

  TRY(NULL != (frame =
               cairo_image_surface_create_for_data(image_rgb32,
                                                   CAIRO_FORMAT_RGB24,
                                                   width, height,
                                                   width*4) ));
  cairo_set_source_surface(cr, frame, 0,0);
  cairo_paint(cr);

  cairo_set_font_size(cr, font_height - TEXT_MARGIN);
  cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);




  ////// Pick and render the annotations
  const double lat_rad = lat * M_PI/180.;
  const double lon_rad = lon * M_PI/180.;
  const double cos_lat = cos(lat_rad);

  for(int i=0; i<Npois; i++)
  {
      const double dlat = pois[i].lat*M_PI/180. - lat_rad;
      const double dlon = pois[i].lon*M_PI/180. - lon_rad;

      const double east        = dlon * Rearth * cos_lat;
      const double north       = dlat * Rearth;
      const double distance_sq_ne = east*east + north*north;
      if(distance_sq_ne < MIN_MARKER_DIST*MIN_MARKER_DIST ||
         distance_sq_ne > MAX_MARKER_DIST*MAX_MARKER_DIST )
          // too close or too far to label
          continue;

      // Project this lat/lon point, then unproject it back using the picking
      // code. If a significant difference is observed, I don't draw this label.
      // This happens if the POI is occluded

      // The projection code is mostly lifted from vertex.glsl. Would be nice to
      // consolidate
      const double h           = pois[i].ele_m - ele_m;
      const double distance_ne = sqrt(distance_sq_ne);
      const double range_have  = sqrt(distance_sq_ne + h*h);

      double az_rad = atan2(east, north);
      // az = 0:     North
      // az = 90deg: East
      const double az_rad0 = az_deg0 * M_PI/180.;
      double       az_rad1 = az_deg1 * M_PI/180.;

      // az_rad1 should be within 2pi of az_rad0 and az_rad1 > az_rad0
      az_rad1 = unwrap_near_rad(az_rad1-az_rad0, M_PI) + az_rad0;

      // in [0,2pi]
      const double az_rad_center = (az_rad0 + az_rad1)/2.;

      az_rad = unwrap_near_rad(az_rad, az_rad_center);

      const double az_ndc_per_rad = 2.0 / (az_rad1 - az_rad0);

      const double az_ndc = (az_rad - az_rad_center) * az_ndc_per_rad;
      if(! (-1. <= az_ndc && az_ndc <= 1.) )
        continue;

      const double aspect = (double)width / (double)height;
      const double el_ndc = atan2(h, distance_ne) * aspect * az_ndc_per_rad;
      if(! (-1. <= el_ndc && el_ndc <= 1.) )
        continue;

      // [-1,1] -> (-0.5,W-0.5)
      const float crosshair_x = (float)(( az_ndc + 1.)/2.*width  - 0.5);
      const float crosshair_y = (float)((-el_ndc + 1.)/2.*height - 0.5);

      // crosshair_y will be checked below in the fuzz loop


      // I'm finished with the projection. I now unproject to look for
      // occlusions

      // The rendered peaks usually don't end up exactly where the POI list
      // says they should be. I scan the range map vertically to find the true
      // peak (or to decide that it's occluded)
      int   fuzz_nearest;
      double err_nearest = 1.0e10f;

      for( int fuzz = -FUZZ_PIXEL_Y; fuzz < FUZZ_PIXEL_Y; fuzz++ )
      {
        if(crosshair_y + (float)fuzz < 0)
        {
          // The next iteration will be in-bounds. If there's too much (or
          // little) fuzz, we'll exit empty-handed
          fuzz = -crosshair_y-1;
          continue;
        }
        if( crosshair_y + (float)fuzz >= height )
          break;

        // As I move down the image the range will get closer and closer. I
        // pick the highest value that's closest
        const float range =
          range_image[width*( (int)round(crosshair_y) + fuzz) +
                      (int)round(crosshair_x)];

        if(range <= 0.0f)
          // no render data here
          continue;

        double err = fabs(range_have - range);
        if( err < err_nearest )
        {
          err_nearest  = err;
          fuzz_nearest = fuzz;
        }
        else
          // it can only get worse from here, so give up
          break;
      }

      if( err_nearest < FUZZ_RANGE )
      {
          poi_indices[Npoi_indices++] = i;
          labels_xy[i].x = crosshair_x;
          labels_xy[i].y = crosshair_y + (float)fuzz_nearest;
      }
  }

  // Now that I have all the crosshair positions, compute the label positions.

  // start out by sorting the POIs by their crosshair_x
  qsort_r( poi_indices, Npoi_indices, sizeof(poi_indices[0]),
           &compar_poi_x, labels_xy );

  // I now traverse the sorted list of POIs, keeping track of groups of POIs
  // that overlap in the horizontal. After each overlapping group is complete,
  // set up the labels of each group member to stagger the labels and avoid
  // overlap
  float overlapgroup_right = -1; // not in an overlapping group at first
  float current_y          = 0;  // start on top
  for( int i=0; i<Npoi_indices; i++ )
  {
    const poi_t* poi      = &pois     [ poi_indices[i] ];
    xy_t*        label_xy = &labels_xy[ poi_indices[i] ];

    float left  = label_xy->x;
    float right = label_xy->x + string_width(cr,poi->name);

    if( left > overlapgroup_right || current_y + font_height >= height )
    {
      // not overlapping, or the label is too low. Draw label on top.
      current_y = 0;
      overlapgroup_right = right;
    }
    else
    {
      // I overlap the previous. Thus draw the label a bit lower
      if( overlapgroup_right < right )
        overlapgroup_right = right;
    }

    draw_label(cr,
               label_xy->x, label_xy->y, current_y,
               poi->name);

    current_y += font_height;
  }

  cairo_surface_show_page(pdf);

  result = true;

 done:

  if(frame != NULL) cairo_surface_destroy(frame);
  if(cr    != NULL) cairo_destroy(cr);
  if(pdf   != NULL) cairo_surface_destroy(pdf);

  free(image_rgb32);

  return result;
}