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
#include <FL/x.H>
#include <FL/fl_draw.H>
#include "orb_settings.hpp"
#include "orb_gpxlayer.hpp"

orb_gpxlayer::orb_gpxlayer(const std::string &path) :
    orb_layer()
{
    name(std::string("Unnamed GPX layer"));
    TiXmlDocument doc(path);
    if (!doc.LoadFile())
        return;

    parsetree(doc.RootElement());
};

orb_gpxlayer::~orb_gpxlayer()
{
    ;
};

void orb_gpxlayer::draw(const orb_viewport &viewport)
{
    unsigned int lastx = 0, lasty = 0;
    orb_settings &settings = orb_settings::get_instance();

    // Get track color and pixel width from the settings
    int trk_color, trk_width;
    settings.getopt(std::string("gpx::linecolor"), trk_color);
    settings.getopt(std::string("gpx::linewidth"), trk_width);

    // Set track color and width
    Fl_Color old_color = fl_color();
    fl_color(((trk_color>>16) & 0xff), ((trk_color>>8) & 0xff), trk_color & 0xff);
    fl_line_style(FL_SOLID, trk_width, NULL);

    // This implements the simple aproach to drawing a track. We simply
    // inerconnect all the trackpoints and let FLTK take care of the clipping.
    // Depending on the size of the track this might raise significant
    // performance issues but I'm lazy and my machine is fast;) It's on my todo
    // list, promised!
    for (std::vector< orb_point<double> >::iterator iter=m_trkpts.begin();iter!=m_trkpts.end();++iter) {

        orb_point<unsigned int> px;
        orb_viewport::merc2px(viewport.z(), *iter, px);

        if (iter == m_trkpts.begin()) {
            lastx = px.get_x();
            lasty = px.get_y();
        }

        // If either the last point or the current point are inside the viewport
        // area then draw
        if (((lastx >= viewport.x()) && (lastx < (viewport.x()+viewport.w())) && 
             (lasty >= viewport.y()) && (lasty < (viewport.y()+viewport.h()))) ||
             ((px.get_x() >= viewport.x()) && (px.get_x() < (viewport.x()+viewport.w())) && 
              (px.get_y() >= viewport.y()) && (px.get_y() < (viewport.y()+viewport.h())))) {
            fl_line(
                    (int)(lastx-viewport.x()), 
                    (int)(lasty-viewport.y()), 
                    (int)(px.get_x()-viewport.x()), 
                    (int)(px.get_y()-viewport.y()));
        }

        lastx = px.get_x();
        lasty = px.get_y();
    }

    // Reset line style and color
    fl_line_style(0);
    fl_color(old_color);
}

int orb_gpxlayer::parsetree(TiXmlNode *parent)
{
    int t = parent->Type();

    if (t != TiXmlNode::ELEMENT) {
        return 0;
    }

    std::string val(parent->Value());

    // Handle trackpoint
    if (val.compare("trkpt") == 0) {
        double lat, lon;
        parent->ToElement()->Attribute("lat", &lat);
        parent->ToElement()->Attribute("lon", &lon);

        orb_point<double> merc;
        orb_viewport::gps2merc(orb_point<double>(lon, lat), merc);

        m_trkpts.push_back(merc);
    } 
    // Handle trackname
    else if (val.compare("name") == 0) {
        name(std::string(parent->ToElement()->GetText()));
    }

    // Recurse the rest of the subtree
    TiXmlNode *child;
    for (child = parent->FirstChild(); child != NULL; child = child->NextSibling()) {
        parsetree(child);
    }

    return 0;
}

