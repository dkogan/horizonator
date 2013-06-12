#include <FL/fl_draw.H>
#include "orb_renderviewlayer.hh"

orb_renderviewlayer::orb_renderviewlayer()
  : lat(-1000.0), lon(-1000.0)
{
  name(std::string("Render-view layer"));
};

void orb_renderviewlayer::draw(const orb_viewport &viewport)
{
  if( lat < -500 || lon < -500 )
    return;

  fl_color( FL_BLUE );

  orb_point<unsigned int> px;
  orb_viewport::gps2px(viewport.z(), orb_point<double>(lon, lat), px);

  int x = px.get_x() - viewport.x();
  int y = px.get_y() - viewport.y();

  fl_line( x-10, y, x+10, y );
  fl_line( x, y-10, x, y+10 );
}
