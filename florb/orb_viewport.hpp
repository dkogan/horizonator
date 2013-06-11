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

#ifndef _ORB_VIEWPORT_HPP
#define _ORB_VIEWPORT_HPP

#include "orb_point.hpp"

class orb_viewport
{
    public:
        orb_viewport();
        ~orb_viewport();

        static int dim(unsigned int z, unsigned int *d);
        static int gps2merc(const orb_point<double> &gps, orb_point<double> &merc);
        static int merc2px(unsigned int z,const orb_point<double> &merc, orb_point<unsigned int> &px);
        static int gps2px(unsigned int z, const orb_point<double> &gps, orb_point<unsigned int> &px);
        static int px2gps(unsigned int z, const orb_point<unsigned int> &px, orb_point<double> &gps);

        unsigned int x() const { return m_x; };
        int x(unsigned int x);

        unsigned int y() const { return m_y; };
        int y(unsigned int y);

        unsigned int z() const { return m_z; };
        int z(unsigned int z, unsigned int refx, unsigned int refy);

        unsigned int w() const { return m_w; };
        int w(unsigned int w);

        unsigned int h() const { return m_h; };
        int h(unsigned int h);

        int move(int dx, int dy);

    private:
        int assertpos();

        unsigned int m_z;
        unsigned int m_x;
        unsigned int m_y;
        unsigned int m_w;
        unsigned int m_h;
};

#endif // _ORB_VIEWPORT_HPP

