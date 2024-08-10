#include <math.h>
#include <stdlib.h>
#include "fltk_annotated_image.hh"
#include "render_terrain.h"

#define LABEL_COLOR          FL_YELLOW
#define LABEL_CROSSHAIR_R    3
#define LABEL_FONT           FL_HELVETICA
#define LABEL_FONT_SIZE      10
#define DIRECTIONS_FONT_SIZE 16

#define TEXT_MARGIN       2

const float Rearth = 6371000.0;

static int font_height = -1;

// compares two POIs by their draw_x. Sorts disabled POIs to the end
static int compar_poi_x( const void* _idx0, const void* _idx1, void* cookie )
{
  const struct poi_t* poi  = (const struct poi_t*)cookie;
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

void CvFltkWidget_annotated::setTransformation( float view_lat_rad, float view_lon_rad, float view_ele_m,

                                                enum cameraType_t cameraType,

                                                // these are used only for perspective cameras
                                                float az,  float el,  float yaw,
                                                float horizontal_fov
                                               )
{
  initPOIs( view_lat_rad, view_lon_rad );

  int*          poi_indices;
  int           poi_N;
  struct poi_t* poi;
  getPOIs( &poi_indices, &poi_N, &poi);

  // Now I compute all the crosshair positions (poi->draw_x, poi->draw_y)
  {
    if( cameraType == mercator )
    {
      // I project this lat/lon point, then unproject it back using the picking
      // code. If a significant difference is observed, I don't draw this label.
      // This happens if the POI is occluded

      // this is mostly lifted from vertex.glsl
      float cos_view_lat = cosf( view_lat_rad );
      float sin_view_lat = sinf( view_lat_rad );

      for( int i=0; i<poi_N; i++ )
      {
        float lat_rad = poi[ poi_indices[i] ].lat_rad;
        float lon_rad = poi[ poi_indices[i] ].lon_rad;
        float ele     = poi[ poi_indices[i] ].ele_m;

        float sin_dlat = sinf( lat_rad - view_lat_rad );
        float cos_dlat = cosf( lat_rad - view_lat_rad );
        float sin_dlon = sinf( lon_rad - view_lon_rad );
        float cos_dlon = cosf( lon_rad - view_lon_rad );

        float sin_lat  = sinf( lat_rad );
        float cos_lat  = cosf( lat_rad );

        float east  = cos_lat * sin_dlon;
        float north = ( sin_dlat*cos_dlon + sin_lat*cos_view_lat*(1.0 - cos_dlon)) ;
        float height = (Rearth + ele) * ( cos_dlat*cos_dlon + sin_lat*sin_view_lat*(1.0 - cos_dlon) )
          /* this is bad for roundoff error */
          - Rearth - view_ele_m;

        float zeff  = (Rearth + ele)*hypotf( east, north );

        float aspect = (float)w() / (float)h();
        float x_normalized = atan2f(east, north) / M_PI;
        float y_normalized = height / M_PI * aspect / zeff;

        // I now have the normalized coordinates. These are linear in (-1,1)
        // across the image. Convert these to be from (0,1)
        float x_normalized_01 = ( x_normalized + 1.0f) / 2.0f;
        float y_normalized_01 = (-y_normalized + 1.0f) / 2.0f;

        int draw_x = (int)( 0.5f + ((float)w() - 1.0f) * x_normalized_01 );
        int draw_y = (int)( 0.5f + ((float)h() - 1.0f) * y_normalized_01 );

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
        float err_nearest = 1.0e10f;
        float depth_have  =
            hypotf( lat_rad - view_lat_rad, lon_rad - view_lon_rad ) *
            180.0f / (float)M_PI;
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

            float err = fabsf( depth_have - (float)db/255.0f );
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
    else
    {
      // perspective camera. First off, I compute the axes of my coordinate
      // system. Global coordinate system. x is lon=0, y is lon=90, z is due
      // north. xy is in the plane of the equator, z is along the axis of
      // rotation
      // float viewer_pos[] = { cosf(view_lat_rad)*cosf(view_lon_rad),
      //                        cosf(view_lat_rad)*sinf(view_lon_rad),
      //                        sinf(view_lat_rad) };
      // for( int i=0; i<3; i++ )
      //   viewer_pos[i] *= (Rearth + ele);
      fprintf(stderr, "not done yet\n");
      exit(1);
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
    struct poi_t* thispoi = &poi[ poi_indices[i] ];

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
}


void CvFltkWidget_annotated::drawLabel( const struct poi_t* poi )
{
  fl_xyline( x() + poi->draw_x - LABEL_CROSSHAIR_R, y() + poi->draw_y,
             x() + poi->draw_x + LABEL_CROSSHAIR_R );
  fl_yxline( x() + poi->draw_x, y() + poi->draw_y + LABEL_CROSSHAIR_R,
             y() + poi->draw_label_y - font_height );
  fl_yxline( x() + poi->draw_x, y() + poi->draw_y + LABEL_CROSSHAIR_R,
             y() + poi->draw_y - LABEL_CROSSHAIR_R );
  fl_draw( poi->name, x() + poi->draw_x, y() + poi->draw_label_y );
}

void CvFltkWidget_annotated::draw()
{
  CvFltkWidget::draw();

  // now draw the labels
  if( font_height <= 0 )
    return;

  int*          poi_indices;
  int           poi_N;
  struct poi_t* poi;
  getPOIs( &poi_indices, &poi_N, &poi);

  fl_push_clip(x(), y(), w(), h());
  fl_font( LABEL_FONT, LABEL_FONT_SIZE );
  fl_color( LABEL_COLOR );
  for( int i=0; i<poi_N; i++ )
  {
    // disabled POIs have been sorted to the back, so as soon as I see one, I'm
    // done
    if( poi[ poi_indices[i]].draw_x < 0 )
        break;
    drawLabel( &poi[ poi_indices[i]] );
  }

  // mark the cardinal directions
  fl_font( LABEL_FONT, DIRECTIONS_FONT_SIZE );
  fl_draw( "S", x() + 1,                                y() + h() - 2 );
  fl_draw( "W", x() + 1*w()/4 - DIRECTIONS_FONT_SIZE/2, y() + h() - 2 );
  fl_draw( "N", x() + 2*w()/4 - DIRECTIONS_FONT_SIZE/2, y() + h() - 2 );
  fl_draw( "E", x() + 3*w()/4 - DIRECTIONS_FONT_SIZE/2, y() + h() - 2 );
  fl_draw( "S", x() + w() - DIRECTIONS_FONT_SIZE,       y() + h() - 2 );


#if 0
  // testing code to save an annotated image to a file

  if( w() > 0 && h() > 0 )
  {
    // for some reason I can't ask for the full width (fl_read_image() returns
    // NULL), so I ask for less...
    int w = 1800;
    int h = 300;

    uchar* img = fl_read_image(NULL, 0,0, w, h );
    if( img )
    {
      FILE* fp = fopen( "img.ppm", "w+");
      fprintf(fp, "P6\n%d %d\n%d\n", w,h,255);
      fwrite(img, 1, w*h*3, fp);
      fclose(fp);
      exit(1);
    }
  }
#endif

  fl_pop_clip();
}
