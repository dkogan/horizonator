#include <math.h>
#include <FL/fl_draw.H>
#include "orb_renderviewlayer.hh"

orb_renderviewlayer::orb_renderviewlayer()
  : lat(-1000.0), lon(-1000.0), left(-1000.0), right(-1000.0)
{
  name(std::string("Render-view layer"));
};

void orb_renderviewlayer::draw(const orb_viewport &viewport)
{
  if( lat < -500 || lon < -500 || left < -500 || right < -500 )
    return;

  fl_color( FL_BLUE );

  orb_point<unsigned int> px;
  orb_viewport::gps2px(viewport.z(), orb_point<double>(lon, lat), px);

  int x = px.get_x() - viewport.x();
  int y = px.get_y() - viewport.y();

  // left/right are in degrees. They indicate where the azimuth of the edge of
  // the window is (assuming the full 360-degree panorama doesn't fit into the
  // window. The angle is from south to west to north to east to south. Thus in
  // our slippy map we have y = cos(th), x = -sin(th)

  float mid = (left + right) / 2.0f;

  fl_line( x, y,
           x - 1000*sinf( left * M_PI / 180.0 ),
           y + 1000*cosf( left * M_PI / 180.0 ) );
  fl_line( x, y,
           x - 1000*sinf( right * M_PI / 180.0 ),
           y + 1000*cosf( right * M_PI / 180.0 ) );

  fl_color( FL_BLACK );

  fl_line( x, y,
           x - 1000*sinf( mid * M_PI / 180.0 ),
           y + 1000*cosf( mid * M_PI / 180.0 ) );
}

bool orb_renderviewlayer::setview( float _left, float _right )
{
  float dl = left  - _left;
  float dr = right - _right;

  // if there's a large-enough change then redraw and update the saved state
  if( fabsf(dl) > 0.1 || fabsf(dr > 0.1) )
  {
    left  = _left;
    right = _right;
    return true;
  }

  return false;
}
