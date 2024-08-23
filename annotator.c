#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <float.h>
#include <cairo-pdf.h>
#include <cairo-svg.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include "util.h"
#include "annotator.h"
#include "horizonator.h"


#define MAX_MARKER_DIST 100000.0
#define MIN_MARKER_DIST 500.0

#define FUZZ_RANGE   500.
#define FUZZ_PIXEL_Y 6


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
  // WARNING: users of tihs are assuming that x_bearing == 0 (i.e. that
  // there's no margin on the left of the text). But there could be
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
                 double y_label,
                 double lat, double lon,
                 const char* name )
{
  cairo_move_to(cr, x-LABEL_CROSSHAIR_R, y);
  cairo_rel_line_to(cr, 2*LABEL_CROSSHAIR_R, 0);

  cairo_move_to(cr, x, y+LABEL_CROSSHAIR_R);
  cairo_line_to(cr, x, y_label);

  cairo_stroke(cr);


  // cairo wants the bottom of the label
  cairo_move_to(cr, x, y_label + font_height);
  char url[256];
  bool url_valid =
    (snprintf(url, sizeof(url),
              "uri='https://caltopo.com/map.html#ll=%f,%f&z=15&b=mbt'",
              lat, lon)
     < (int)sizeof(url));
  if(url_valid) cairo_tag_begin (cr, CAIRO_TAG_LINK, url);
  cairo_show_text(cr, name);
  if(url_valid) cairo_tag_end (cr, CAIRO_TAG_LINK);
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
              const char* out_filename,
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
  cairo_surface_t* surface     = NULL;
  cairo_t*         cr          = NULL;
  cairo_surface_t* frame       = NULL;

  ////// Paint the render into the surface
  TRY(NULL != (image_rgb32 = malloc(width*height*4)));

  TRY(RGB32_from_BGR24(// output
                       image_rgb32,
                       // input
                       image_bgr,
                       width,
                       height));

  const int strlen_out_filename = strlen(out_filename);
  if(0 == strcasecmp(".pdf", &out_filename[strlen_out_filename-4]))
      TRY(NULL !=
          (surface = cairo_pdf_surface_create(out_filename,
                                              width  * CAIRO_SCALE,
                                              height * CAIRO_SCALE)));
  else if(0 == strcasecmp(".svg", &out_filename[strlen_out_filename-4]))
  {
      MSG("WARNING: writing out an .svg file; the links don't work with those yet; fix it, or use .pdf");
      TRY(NULL !=
          (surface = cairo_svg_surface_create(out_filename,
                                              width  * CAIRO_SCALE,
                                              height * CAIRO_SCALE)));
  }
  else
  {
      MSG("ERROR: output filename must be either xxx.pdf or xxx.svg; got '%s'", out_filename);
      goto done;
  }

  TRY(NULL != (cr = cairo_create(surface)));
  cairo_scale(cr, CAIRO_SCALE, CAIRO_SCALE);

  const double cos_lat = cos(lat * M_PI/180.);

  ////// Make links to the map

  ////// I do this FIRST because I cannot figure out how to tell cairo to make
  ////// transparent link boxes. I have to draw SOMETHING to get links. So I
  ////// draw rectangles below the panorama. These will end up invisible
  ////// (occluded by the panorama), and I still get my links
  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Doesn't matter; anything will do

  // 10x10 doesn't work with big images:
  //   $ pdfimages -list /tmp/tst.pdf
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Catalog object is wrong type (null)
  //   Syntax Error: Couldn't find trailer dictionary
  //   Internal Error: xref num -1 not found but needed, try to reconstruct<0a>
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Catalog object is wrong type (null)
  //   Syntax Error: Couldn't read page catalog
  // mupdf still works, but evince does not
  const int cell_width  = 14;
  const int cell_height = 14;
  for(int y=0; y<height-cell_height; y += cell_height)
  {
    for(int x=0; x<width-cell_width; x += cell_width)
    {
      const float range = range_image[width*y + x];

      if(range <= 0.0f)
        // no render data here
        continue;

      float lat_cell,lon_cell;
      if(!horizonator_unproject(&lat_cell, &lon_cell,
                                x+cell_width/2, y+cell_height/2,
                                // have range_enh, not range_en
                                range, -1.,
                                lat, cos_lat,
                                lon,
                                az_deg0, az_deg1,
                                width, height))
        continue;

      char url[256];
      bool url_valid =
        (snprintf(url, sizeof(url),
                  "uri='https://caltopo.com/map.html#ll=%f,%f&z=15&b=mbt'",
                  lat_cell, lon_cell)
         < (int)sizeof(url));
      if(!url_valid) break;

      cairo_tag_begin (cr, CAIRO_TAG_LINK, url);
      cairo_rectangle(cr, x,y,cell_width,cell_height);
      cairo_fill(cr);
      cairo_tag_end (cr, CAIRO_TAG_LINK);
    }
  }

  /////// Draw the pano
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
  for(int i=0; i<Npois; i++)
  {
      double crosshair_x, crosshair_y;
      double range_have;
      if(!horizonator_project(&crosshair_x, &crosshair_y, &range_have,
                              lat, cos_lat,
                              lon,
                              ele_m,
                              pois[i].lat,
                              pois[i].lon,
                              pois[i].ele_m,
                              az_deg0 * M_PI/180.,
                              az_deg1 * M_PI/180.,
                              width,
                              height))
          continue;

      if(range_have < MIN_MARKER_DIST ||
         range_have > MAX_MARKER_DIST )
          // too close or too far to label
          continue;

      // crosshair_y will be checked below in the fuzz loop


      // I'm finished with the projection. I now unproject to look for
      // occlusions

      // The rendered peaks usually don't end up exactly where the POI list
      // says they should be. I scan the range map vertically to find the true
      // peak (or to decide that it's occluded)
      int   fuzz_nearest = 0; // initializing to pacify compiler
      double err_nearest = DBL_MAX;

      for( int fuzz = -FUZZ_PIXEL_Y; fuzz < FUZZ_PIXEL_Y; fuzz++ )
      {
        if(crosshair_y + (double)fuzz < 0)
          continue;
        if( crosshair_y + (double)fuzz >= height )
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
               poi->lat, poi->lon,
               poi->name);

    current_y += font_height;
  }

  const int bearing_annotation_spacing = 15;
  for(int az=180; az>-180; az -= bearing_annotation_spacing)
  {
      double x;
      if(!horizonator_x_from_az(// output
                                &x, NULL,
                                // input
                                (double)az * M_PI/180.,
                                az_deg0 * M_PI/180.,
                                az_deg1 * M_PI/180.,
                                width))
          continue;

      char text[16];
      sprintf(text, "%ddeg", az);

      double w = string_width(cr, text);
      // cairo wants the bottom of the label
      cairo_move_to(cr, x-w/2., height - font_height);
      cairo_show_text(cr, text);
  }

  cairo_surface_show_page(surface);

  result = true;

 done:

  if(frame   != NULL) cairo_surface_destroy(frame);
  if(cr      != NULL) cairo_destroy(cr);
  if(surface != NULL) cairo_surface_destroy(surface);

  free(image_rgb32);

  return result;
}
