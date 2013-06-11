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
#include <iostream>
#include <FL/fl_draw.H>
#include <FL/x.H>
#include "orb_settings.hpp"
#include "orb_mapctrl.hpp"

orb_mapctrl::orb_mapctrl(int x, int y, int w, int h, const char *label) : 
    Fl_Widget(x, y, w, h, label),
    m_mousepos(0, 0)
{
    m_viewport = new orb_viewport();
}

orb_mapctrl::~orb_mapctrl()
{
    delete m_viewport;
}

int orb_mapctrl::layers(std::vector<orb_layer*> &layers)
{
    m_layers = layers;
    refresh();
    return 0;
}

int orb_mapctrl::zoom_get(unsigned int &z)
{
    z = m_viewport->z();
    return 0;
}

int orb_mapctrl::zoom_set(unsigned int z)
{
    m_viewport->z(z, m_viewport->w()/2, m_viewport->h()/2);
    
    redraw();
    do_callback();
    return 0;
}

int orb_mapctrl::mousegps(orb_point<double> &gps)
{
    unsigned int dpx = 0, dpy = 0;
    if (w() > (int)m_viewport->w())
        dpx = (w() - (int)m_viewport->w())/2;
    if (h() > (int)m_viewport->h())
        dpy = (h() - (int)m_viewport->h())/2;

    unsigned int px = m_mousepos.get_x();
    unsigned int py = m_mousepos.get_y();

    if (dpx > px)
        px = 0;
    else
        px -= dpx;
    if (dpy > py)
        py = 0;
    else
        py -= dpy;

    if (px >= m_viewport->w())
        px = m_viewport->w()-1;
    if (py >= m_viewport->h())
        py = m_viewport->h()-1;

    orb_viewport::px2gps(
            m_viewport->z(), 
            orb_point<unsigned int>(m_viewport->x()+px, m_viewport->y()+py), 
            gps);

    return 0;
}

int orb_mapctrl::refresh()
{
    // Make sure the current zoomlevel is valid
    m_viewport->z(m_viewport->z(), m_viewport->w()/2, m_viewport->h()/2);

    do_callback();
    redraw();
    return 0;
}

int orb_mapctrl::handle(int event) 
{
    switch (event) {
        case FL_MOVE:
            m_mousepos.set_x(Fl::event_x()-x());
            m_mousepos.set_y(Fl::event_y()-y());
            return 1;
        case FL_ENTER:
            fl_cursor(FL_CURSOR_HAND);
            return 1;
        case FL_LEAVE:
            fl_cursor(FL_CURSOR_DEFAULT);
            return 1;
        case FL_PUSH:
            if (Fl::event_button() == FL_RIGHT_MOUSE)
              do_callback();
            return 1;
        case FL_RELEASE: 
            return 1;
        case FL_DRAG: 
            {
                if (!Fl::event_inside(this))
                    break;

                int dx = m_mousepos.get_x() - (Fl::event_x()-x());
                int dy = m_mousepos.get_y() - (Fl::event_y()-y());
                m_mousepos.set_x(Fl::event_x()-x());
                m_mousepos.set_y(Fl::event_y()-y());

                m_viewport->move(dx, dy); 
                redraw();

                return 1;
            }
        case FL_MOUSEWHEEL:
            if (!Fl::event_inside(this))
                break;

            // Prevent integer underflow
            if ((Fl::event_dy() > 0) && (m_viewport->z() == 0))
                return 1;

            // The image of the viewport might be smaller then our current
            // client area. We need to take this delta into account.
            int dpx = 0, dpy = 0;
            if (w() > (int)m_viewport->w())
                dpx = (w() - (int)m_viewport->w())/2;
            if (h() > (int)m_viewport->h())
                dpy = (h() - (int)m_viewport->h())/2;

            int px = Fl::event_x()- x() - dpx;
            int py = Fl::event_y()- y() - dpy;

            m_viewport->z(m_viewport->z()-Fl::event_dy(), px, py);
            redraw();
            return 1;
    }

    return Fl_Widget::handle(event);
}

void orb_mapctrl::draw() 
{
    // Resize the viewport before drawing
    m_viewport->w(w());
    m_viewport->h(h());

    if ((damage() & FL_DAMAGE_ALL) == 0) 
        return;

    // Fill the area which the viewport does not cover
    fl_rectf(x(), y(), w(), h(), 80, 80, 80);

    // Create an offscreen drawing buffer and send all subsequent commands there
    Fl_Offscreen offscreen;
    offscreen = fl_create_offscreen(m_viewport->w(), m_viewport->h());
    fl_begin_offscreen(offscreen);

    fl_rectf(0, 0, m_viewport->w(), m_viewport->h(), 80, 80, 80);

    // Draw all the layers
    for (std::vector<orb_layer*>::iterator iter=m_layers.begin();iter!=m_layers.end();++iter)
        (*iter)->draw(*m_viewport);

    fl_end_offscreen();
   
    // Blit the generated viewport bitmap onto the widget (centered)
    int dpx = 0, dpy = 0;
    if (w() > (int)m_viewport->w())
        dpx = (w() - (int)m_viewport->w())/2;
    if (h() > (int)m_viewport->h())
        dpy = (h() - (int)m_viewport->h())/2;

    fl_copy_offscreen(x()+dpx, y()+dpy, m_viewport->w(), m_viewport->h(), offscreen, 0, 0);
    fl_delete_offscreen(offscreen);
}

void orb_mapctrl::resize(int x, int y, int w, int h)
{
    Fl_Widget::resize(x, y, w, h);
}

