// Copyright (c) 2010, Björn Rehm (bjoern@shugaa.de)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <cmath>
#include "orb_settings.hpp"
#include "orb_viewport.hpp"

orb_viewport::orb_viewport() :
  m_z(13),
  m_x(359941),
  m_y(835194),
  m_w(1916),
  m_h(1130)
{
    assertpos();
};

orb_viewport::~orb_viewport()
{
    ;
};

int orb_viewport::gps2merc(const orb_point<double> &gps, orb_point<double> &merc)
{
    if ((gps.get_x() > 180.0) || (gps.get_x() < -180.0))
        return 1;
    if ((gps.get_y() > 90.0) || (gps.get_y() < -90.0))
        return 1;

    // Transform GPS to mercator
    merc.set_x(180.0 + gps.get_x());
    merc.set_y(180.0 + (180.0/M_PI * log(tan(M_PI/4.0+gps.get_y()*(M_PI/180.0)/2.0))));

    return 0;
}

int orb_viewport::merc2px(unsigned int z, const orb_point<double> &merc, orb_point<unsigned int> &px)
{
    unsigned int dimxy;
    dim(z, &dimxy);

    // Get the pixel position on the map for the reference lat/lon
    px.set_x((unsigned int)(((double)dimxy/360.0) * merc.get_x()));
    px.set_y((unsigned int)(dimxy-(((double)dimxy/360.0) * merc.get_y())));

    return 0;
}

int orb_viewport::gps2px(unsigned int z, const orb_point<double> &gps, orb_point<unsigned int> &px)
{
    // Transform GPS to mercator
    orb_point<double> merc;
    int rc = gps2merc(gps, merc);
    if (rc != 0)
        return 1;

    // Tranxform mercator to pixel position
    merc2px(z, merc, px);

    return 0;
}

int orb_viewport::px2gps(unsigned int z, const orb_point<unsigned int> &px, orb_point<double> &gps)
{
    // Get map dimensions
    unsigned int dimxy;
    dim(z, &dimxy);

    // Make sure the coordinate is on the map
    if ((px.get_x() > dimxy) || (px.get_y() > dimxy))
        return 1;

    // Convert pixel position to mercator coordinate
    double mlon = (360.0/((double)dimxy/(double)px.get_x()));
    double mlat = (360.0/((double)dimxy/(double)(dimxy-px.get_y())));

    // Convert mercator to GPS coordinate
    gps.set_x(mlon - 180.0);
    gps.set_y(180.0/M_PI * (2.0 * atan(exp((mlat-180.0)*M_PI/180.0)) - M_PI/2.0));

    return 0;
}

int orb_viewport::dim(unsigned int z, unsigned int *d)
{
    *d = pow(2.0, z) * 256;
    return 0;
}

int orb_viewport::assertpos()
{
    int rc = 0;
    unsigned int dimxy;
    dim(m_z, &dimxy);

    // Make sure the viewport is not larger than the map
    if (m_w > dimxy) {
        m_w = dimxy;
        rc = 1;
    }
    if (m_h > dimxy) {
        m_h = dimxy;
        rc = 1;
    }

    // Make sure the vieport's x and y position is valid
    if ((m_x + m_w) > dimxy) {
        if (m_w <= dimxy)
            m_x = dimxy - m_w;
        else
            m_x = 0;

        rc = 1;
    }
    if ((m_y + m_h) > dimxy) {
        if (m_h <= dimxy)
            m_y = dimxy - m_h;
        else
            m_y = 0;

        rc = 1;
    }

    return rc;
} 

int orb_viewport::x(unsigned int x)
{
    m_x = x;
    return assertpos();
}

int orb_viewport::y(unsigned int y)
{
    m_y = y;
    return assertpos();
}

int orb_viewport::z(unsigned int z, unsigned int refx, unsigned int refy)
{
    orb_settings &settings = orb_settings::get_instance();
    
    unsigned int zoommin, zoommax;
    settings.getopt(std::string("osm::zoommin"), zoommin);
    settings.getopt(std::string("osm::zoommax"), zoommax);

    if (z < zoommin)
        z = zoommin;
    if (z > zoommax)
        z = zoommax;

    unsigned int dimnow, dimlater;
    dim(m_z, &dimnow);
    dim(z, &dimlater);

    // refx and refy are viewport-relative, Calculate the absolute pixel position
    // on the map. 
    double arefx = refx + m_x;
    double arefy = refy + m_y;

    // Calculate the absolute pixel position at the new zoomlevel
    double arefx_new = dimlater * (arefx/dimnow);
    double arefy_new = dimlater * (arefy/dimnow);

    // Check for integer overflow when repositioning the viewport
    if ((double)refx > arefx_new)
        m_x = 0;
    else
        m_x = (unsigned int)(arefx_new - refx);

    if ((double)refy > arefy_new)
        m_y = 0;
    else
        m_y = (unsigned int)(arefy_new - refy);
    
    m_z = z;
    return assertpos();
}

int orb_viewport::w(unsigned int w)
{
    unsigned int dx;

    // Keep the viewport centered when resizing
    if (w > m_w) {
        dx = (w - m_w)/2;

        if (dx > m_x)
            m_x = 0;
        else
            m_x -= dx;
    } else {
        dx = m_w - w;
        m_x += (dx/2);
    }

    m_w = w;
    return assertpos();
}

int orb_viewport::h(unsigned int h)
{
    unsigned int dy;

    // Keep the viewport centered when resizing
    if (h > m_h) {
        dy = (h - m_h)/2;

        if (dy > m_y)
            m_y = 0;
        else
            m_y -= dy;
    } else {
        dy = m_h - h;
        m_y += (dy/2);
    }

    m_h = h;
    return assertpos();
}

int orb_viewport::move(int dx, int dy)
{
    // Make sure we don't produce an integer overflow
    if ((dx < 0) && (-dx > (int)m_x))
        m_x = 0;
    else
        m_x += dx;

    if ((dy < 0) && (-dy > (int)m_y))
        m_y = 0;
    else
        m_y += dy;

    return assertpos();
}

