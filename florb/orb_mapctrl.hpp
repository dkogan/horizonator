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

#ifndef _ORB_MAPCTRL_HPP
#define _ORB_MAPCTRL_HPP

#include <vector>
#include <string>
#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include "orb_viewport.hpp"
#include "orb_point.hpp"
#include "orb_layer.hpp"

class orb_mapctrl : public Fl_Widget 
{
    public:
        orb_mapctrl(int x, int y, int w, int h, const char *label);
        ~orb_mapctrl();

        virtual int handle(int event);
        virtual void resize(int x, int y, int w, int h);

        int mousegps(orb_point<double> &gps);
        int center_at( float lat, float lon );

        int zoom_get(unsigned int &z);
        int zoom_set(unsigned int z);

        int layers(std::vector<orb_layer*> &layers);
        int refresh();

    private:
        orb_point<int> m_mousepos;
        orb_viewport *m_viewport;
        std::vector<orb_layer*> m_layers;

    protected:
        void draw();
};

#endif // _ORB_MAPCTRL_HPP

