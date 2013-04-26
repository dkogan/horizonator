#ifndef FLTK_ANNOTATED_IMAGE_HH
#define FLTK_ANNOTATED_IMAGE_HH

#include <cvFltkWidget.hh>

extern "C" {
#include "points_of_interest.h"
}

enum cameraType_t { perspective, mercator };

class CvFltkWidget_annotated : public CvFltkWidget
{
  void drawLabel( const struct poi_t* poi );

public:
  CvFltkWidget_annotated( int x, int y, int w, int h, CvFltkWidget_ColorChoice color )
    : CvFltkWidget( x,y,w,h,color )
  {
  }

  // arguments are position and orientation of the viewer
  // az is from north towards east
  // el is from horizontal
  // yaw is clockwise-positive
  void setTransformation( float view_lat, float view_lon, float view_ele_m,

                          enum cameraType_t cameraType,

                          // these are used only for perspective cameras
                          float az,  float el,  float yaw,
                          float horizontal_fov
                         );

  void draw();
};

#endif
