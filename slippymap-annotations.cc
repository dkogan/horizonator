#include <math.h>
#include <FL/fl_draw.H>
#include "slippymap-annotations.hh"
#include "util.h"

// this or less means "no pick". Duplicated in .hh
#define MIN_VALID_ANGLE -1000.0f

void SlippymapAnnotations::draw(const orb_viewport &viewport)
{
    fl_color( FL_BLUE );

    orb_point<unsigned int> px;
    orb_viewport::gps2px(viewport.z(), orb_point<double>(view->lon, view->lat), px);

    int x = px.get_x() - viewport.x();
    int y = px.get_y() - viewport.y();

    // left/right are in degrees. They indicate where the azimuth of the edge of
    // the window is (assuming the full 360-degree panorama doesn't fit into the
    // window. The angle is from south to west to north to east to south. Thus in
    // our slippy map we have y = cos(th), x = -sin(th)

    fl_line( x, y,
             x + 10000*sinf( (view->az_center_deg - view->az_radius_deg) * M_PI / 180.0 ),
             y - 10000*cosf( (view->az_center_deg - view->az_radius_deg) * M_PI / 180.0 ) );
    fl_line( x, y,
             x + 10000*sinf( (view->az_center_deg + view->az_radius_deg) * M_PI / 180.0 ),
             y - 10000*cosf( (view->az_center_deg + view->az_radius_deg) * M_PI / 180.0 ) );

    fl_color( FL_BLACK );

    fl_line( x, y,
             x + 10000*sinf( view->az_center_deg * M_PI / 180.0 ),
             y - 10000*cosf( view->az_center_deg * M_PI / 180.0 ) );

    if( pick_lat > MIN_VALID_ANGLE )
    {
        fl_color( FL_RED );
        orb_viewport::gps2px(viewport.z(), orb_point<double>(pick_lon, pick_lat), px);
        x = px.get_x() - viewport.x();
        y = px.get_y() - viewport.y();
        fl_begin_polygon();
        fl_circle( x, y, 5 );
        fl_end_polygon();
    }
}


