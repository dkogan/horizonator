#include <math.h>
#include <stdlib.h>
#include "fltk_annotated_image.hh"

#define LABEL_COLOR       FL_BLACK
#define LABEL_CROSSHAIR_R 3
#define LABEL_FONT        FL_HELVETICA
#define LABEL_FONT_SIZE   8

#define TEXT_MARGIN       2

const float Rearth = 6371000.0;

static int font_height = -1;

static int compar_poi_x( const void* _idx0, const void* _idx1, void* cookie )
{
  const struct poi_t* poi  = (const struct poi_t*)cookie;
  const int*          idx0 = (const int*)_idx0;
  const int*          idx1 = (const int*)_idx1;

  if( poi[ *idx0 ].draw_x < poi[ *idx1 ].draw_x )
    return -1;
  else
    return 1;
}

void CvFltkWidget_annotated::setTransformation( float view_lat, float view_lon, float view_ele_m,

                                                enum cameraType_t cameraType,

                                                // these are used only for perspective cameras
                                                float az,  float el,  float yaw,
                                                float horizontal_fov
                                               )
{
  initPOIs( view_lat, view_lon );

  int*          poi_indices;
  int           poi_N;
  struct poi_t* poi;
  getPOIs( &poi_indices, &poi_N, &poi);

  // Now I compute all the crosshair positions (poi->draw_x, poi->draw_y)
  {
    if( cameraType == mercator )
    {
      // this is mostly lifted from vertex.glsl
      float cos_view_lat = cosf( view_lat );
      float sin_view_lat = sinf( view_lat );

      for( int i=0; i<poi_N; i++ )
      {
        float lat = poi[ poi_indices[i] ].lat;
        float lon = poi[ poi_indices[i] ].lon;
        float ele = poi[ poi_indices[i] ].ele_m;

        float sin_dlat = sinf( lat - view_lat );
        float cos_dlat = cosf( lat - view_lat );
        float sin_dlon = sinf( lon - view_lon );
        float cos_dlon = cosf( lon - view_lon );

        float sin_lat  = sinf( lat );
        float cos_lat  = cosf( lat );

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
        float x_normalized_01 = (x_normalized + 1.0f) / 2.0f;
        float y_normalized_01 = (-y_normalized + 1.0f) / 2.0f;

        poi[ poi_indices[i] ].draw_x = (int)( 0.5f + ((float)w() - 1.0f) * x_normalized_01 );
        poi[ poi_indices[i] ].draw_y = (int)( 0.5f + ((float)h() - 1.0f) * y_normalized_01 );
      }
    }
    else
    {
      // perspective camera. First off, I compute the axes of my coordinate
      // system. Global coordinate system. x is lon=0, y is lon=90, z is due
      // north. xy is in the plane of the equator, z is along the axis of
      // rotation
      // float viewer_pos[] = { cosf(view_lat)*cosf(view_lon),
      //                        cosf(view_lat)*sinf(view_lon),
      //                        sinf(view_lat) };
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

    int left  = thispoi->draw_x;
    int right = thispoi->draw_x + fl_width( thispoi->name );

    if( left <= overlapgroup_right )
    {
      // I overlap the previous. Thus draw the label a bit lower
      if( overlapgroup_right < right )
        overlapgroup_right = right;
    }
    else
    {
      // not overlapping. Draw label on top.
      current_y = 0;
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
    drawLabel( &poi[ poi_indices[i]] );
  fl_pop_clip();
}
