#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <cairo-pdf.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include "util.h"
#include "annotator.h"


#define MAX_MARKER_DIST 35000.0
#define MIN_MARKER_DIST 50.0

const float Rearth = 6371000.0;

#define LABEL_CROSSHAIR_R 3
#define TEXT_MARGIN       2

static int font_height = -1;







#if 0
// compares two POIs by their draw_x. Sorts disabled POIs to the end
static int compar_poi_x( const void* _idx0, const void* _idx1, void* cookie )
{
  const poi_t* poi  = (const struct poi_t*)cookie;
  const int*          idx0 = (const int*)_idx0;
  const int*          idx1 = (const int*)_idx1;

  // I sort disabled POIs to the end
  if( poi[ *idx1 ].draw_x < 0 )
      return -1;
  if( poi[ *idx0 ].draw_x < 0 )
      return 1;

  if( poi[ *idx0 ].draw_x < poi[ *idx1 ].draw_x )
    return -1;
  else
    return 1;
}

static
void draw_label( const poi_t* poi )
{
  fl_xyline( x() + poi->draw_x - LABEL_CROSSHAIR_R, y() + poi->draw_y,
             x() + poi->draw_x + LABEL_CROSSHAIR_R );
  fl_yxline( x() + poi->draw_x, y() + poi->draw_y + LABEL_CROSSHAIR_R,
             y() + poi->draw_label_y - font_height );
  fl_yxline( x() + poi->draw_x, y() + poi->draw_y + LABEL_CROSSHAIR_R,
             y() + poi->draw_y - LABEL_CROSSHAIR_R );
  fl_draw( poi->name, x() + poi->draw_x, y() + poi->draw_label_y );
}

static
double arclen_sq( double lat0_rad, double lon0_rad,
                  double lat1_rad, double lon1_rad)
{
  // On the surface of the earth the arclen is dtheta*Rearth
  //
  // Given v0,v1, |dth| ~ |sin(dth)| = | v0 x v1 |
  //
  // v = [ cos(lat) cos(lon)
  //       cos(lat) sin(lon)
  //       sin(lat) ]
  //
  // |v0 x v1| ~ sqrt( cos(lat0)^2 cos(lat1)^2 dlon^2 + dlat^2 )

  const double dlat = lat1_rad - lat0_rad;
  const double dlon = lon1_rad - lon0_rad;

  const double cos_lat0_sq = cos(lon0_rad)*cos(lon0_rad);
  const double cos_lat1_sq = cos(lat1_rad)*cos(lat1_rad);
  return Rearth*Rearth *
    ( dlon*dlon * cos_lat0_sq * cos_lat1_sq +
      dlat*dlat );
}

#endif


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

  uint8_t*         image_rgb32 = NULL;
  cairo_surface_t* pdf         = NULL;
  cairo_t*         cr          = NULL;
  cairo_surface_t* frame       = NULL;

  TRY(NULL != (image_rgb32 = malloc(width*height*4)));

  TRY(RGB32_from_BGR24(// output
                       image_rgb32,
                       // input
                       image_bgr,
                       width,
                       height));


  const int    POINTS_PER_INCH = 72;
  const int    PIXELS_PER_INCH = 300;
  const double SCALE           = (double)POINTS_PER_INCH / (double)PIXELS_PER_INCH;

  TRY(NULL !=
      (pdf = cairo_pdf_surface_create(pdf_filename,
                                      (width  * POINTS_PER_INCH) / PIXELS_PER_INCH,
                                      (height * POINTS_PER_INCH) / PIXELS_PER_INCH)));
  TRY(NULL != (cr = cairo_create(pdf)));
  TRY(NULL != (frame =
               cairo_image_surface_create_for_data(image_rgb32,
                                                   CAIRO_FORMAT_RGB24,
                                                   width, height,
                                                   width*4) ));
  cairo_scale(cr, SCALE, SCALE);
  cairo_set_source_surface(cr, frame, 0,0);
  cairo_paint(cr);

  cairo_surface_show_page(pdf);



#if 0
  for(int i=0; i<Npois; i++)
  {
    pois[i].draw_x = INT_MAX;

    const double len_sq = arclen_sq( lat_rad, lon_rad,
                                     pois[ipoi].lat_rad, pois[ipoi].lon_rad );
    if(MIN_MARKER_DIST*MIN_MARKER_DIST > len_sq ||
       MAX_MARKER_DIST*MAX_MARKER_DIST < len_sq )
      pois[i].draw_x = -1;
  }

  // Now I compute all the crosshair positions (poi->draw_x, poi->draw_y)
  {
    // I project this lat/lon point, then unproject it back using the picking
    // code. If a significant difference is observed, I don't draw this label.
    // This happens if the POI is occluded

    // this is mostly lifted from vertex.glsl
    double cos_lat = cos( lat_rad );
    double sin_lat = sin( lat_rad );

    for( int i=0; i<poi_N; i++ )
    {
      double sin_dlat = sin( pois[i].lat_rad - lat_rad );
      double cos_dlat = cos( pois[i].lat_rad - lat_rad );
      double sin_dlon = sin( pois[i].lon_rad - lon_rad );
      double cos_dlon = cos( pois[i].lon_rad - lon_rad );

      double sin_poi_lat  = sin( pois[i].lat_rad );
      double cos_poi_lat  = cos( pois[i].lat_rad );

      double east  = cos_poi_lat * sin_dlon;
      double north = ( sin_dlat*cos_dlon + sin_poi_lat*cos_lat*(1.0 - cos_dlon)) ;

      // I had this:
      //   double height =
      //     (Rearth + pois[i].ele_m) *
      //     ( cos_dlat*cos_dlon + sin_poi_lat*sin_lat*(1.0 - cos_dlon) )
      //     /* this is bad for roundoff error */
      //     - Rearth - ele_m;
      // I refactor it to remove the big-big expression to improve precision
      double height =
        pois[i].ele_m * ( cos_dlat*cos_dlon +
                          sin_poi_lat*sin_lat*(1.0 - cos_dlon) ) +
        Rearth *
        ( cos_dlat*cos_dlon - 1 + // better, still big-big here; refactor this
          sin_poi_lat*sin_lat*(1.0 - cos_dlon) )
        - ele_m;

      double zeff  = (Rearth + pois[i].ele_m)*sqrt( east*east + north*north );

      double aspect = (double)w() / (double)h();
      double x_normalized = atan2(east, north) / M_PI;
      double y_normalized = height / M_PI * aspect / zeff;

      // I now have the normalized coordinates. These are linear in (-1,1)
      // across the image. Convert these to be from (0,1)
      double x_normalized_01 = ( x_normalized + 1.0) / 2.0;
      double y_normalized_01 = (-y_normalized + 1.0) / 2.0;

      int draw_x = (int)( 0.5 + ((double)w() - 1.0) * x_normalized_01 );
      int draw_y = (int)( 0.5 + ((double)h() - 1.0) * y_normalized_01 );

      assert( draw_x >= 0 && draw_x < w() );
      // draw_y will be checked below in the fuzz loop


      // I'm finished with the projection. I now unproject to look for
      // occlusions

      // The rendered peaks usually don't end up exactly where the POI list
      // says they should be. I scan the depth map vertically to find the true
      // peak (or to decide that it's occluded)
      uint8_t db_last   = 255; // 255 == sky, so we'll automatically skip it
                               // in the loop
      int   fuzz_nearest;
      double err_nearest = 1.0e10f;
      double depth_have  =
          hypotf( pois[i].lat_rad - lat_rad, pois[i].lon_rad - lon_rad ) *
          180.0f / (double)M_PI;
      const CvMat* depth = render_terrain_getdepth();

#define PEAK_LABEL_FUZZ_PX 4
      for( int fuzz = -PEAK_LABEL_FUZZ_PX; fuzz < PEAK_LABEL_FUZZ_PX; fuzz++ )
      {
          if( draw_y + fuzz < 0 )
          {
              // The next iteration will be in-bounds. If there's too much (or
              // little) fuzz, we'll exit empty-handed
              fuzz = -draw_y-1;
              continue;
          }
          else if( draw_y + fuzz >= h() )
              break;

          // As I move down the image the depth will get closer and closer. I
          // pick the highest value that's closest
          uint8_t db = depth->data.ptr[draw_x + (draw_y+fuzz)*depth->cols];

          // we already looked at this depth
          if(db == db_last) continue;

          double err = fabsf( depth_have - (double)db/255.0f );
          if( err < err_nearest )
          {
              err_nearest  = err;
              fuzz_nearest = fuzz;
          }
          else
              // it can only get worse from here, so give up
              break;

          db_last = db;
      }

      if( err_nearest > 0.04f )
      {
          // indicate that this POI shouldn't be drawn
          poi[ poi_indices[i] ].draw_x = -1;
      }
      else
      {
          poi[ poi_indices[i] ].draw_x = draw_x;
          poi[ poi_indices[i] ].draw_y = draw_y + fuzz_nearest;
      }
    }
  }

  fl_font( LABEL_FONT, LABEL_FONT_SIZE );
  font_height = fl_height();

  // Now that I have all the crosshair positions, compute the label positions.
  //
  // start out by sorting the POIs by their draw_x
  qsort_r( poi_indices, poi_N, sizeof(poi_indices[0]),
           &compar_poi_x, poi );

  // I now traverse the sorted list of POIs, keeping track of groups of POIs
  // that overlap in the horizontal. After each overlapping group is complete,
  // set up the labels of each group member to stagger the labels and avoid
  // overlap
  int overlapgroup_right = -1;                        // not in an overlapping group at first
  int current_y          = font_height + TEXT_MARGIN; // start on top
  for( int i=0; i<poi_N; i++ )
  {
    poi_t* thispoi = &poi[ poi_indices[i] ];

    // disabled POIs have been sorted to the back, so as soon as I see one, I'm
    // done
    if( thispoi->draw_x < 0 )
        break;

    int left  = thispoi->draw_x;
    int right = thispoi->draw_x + fl_width( thispoi->name );

    if( left > overlapgroup_right || current_y + font_height + TEXT_MARGIN >= h() )
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
    current_y += font_height + TEXT_MARGIN;

    thispoi->draw_label_y = current_y;
  }
#endif
  result = true;

 done:
  free(image_rgb32);

  if(cr    != NULL) cairo_destroy(cr);
  if(pdf   != NULL) cairo_surface_destroy(pdf);
  if(frame != NULL) cairo_surface_destroy(frame);

  return result;
}
