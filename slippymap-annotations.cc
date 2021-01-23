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



    fl_color( FL_RED );

    // The lat/lon of the first and last cells. These are INCLUSIVE
    float lat0, lon0, lat1, lon1;
    horizonator_dem_bounds_latlon_deg(&ctx->dems,
                                      &lat0, &lon0, &lat1, &lon1);

    orb_viewport::gps2px(viewport.z(), orb_point<double>(lon0, lat0), px);
    int x0 = px.get_x() - viewport.x();
    int y0 = px.get_y() - viewport.y();
    orb_viewport::gps2px(viewport.z(), orb_point<double>(lon1, lat1), px);
    int x1 = px.get_x() - viewport.x();
    int y1 = px.get_y() - viewport.y();

    fl_begin_loop();
    fl_vertex(x0, y0);
    fl_vertex(x0, y1);
    fl_vertex(x1, y1);
    fl_vertex(x1, y0);
    fl_end_loop();

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


