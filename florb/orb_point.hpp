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

#ifndef _ORB_POINT_HPP
#define _ORB_POINT_HPP

template <class T>
class orb_point
{
    public:
        orb_point() :
            m_x(0), m_y(0) {;};
        orb_point(T x, T y) :
            m_x(x), m_y(y) {;};
        orb_point(const orb_point &point) : 
            m_x(point.get_x()), m_y(point.get_y()) {;};

        T get_x() const { return m_x; };
        T get_y() const { return m_y; };

        void set_x(T x) { m_x = x; };
        void set_y(T y) { m_y = y; };

        void add(const T &other) {
            m_x += other.get_x();
            m_y += other.get_y();
        }

    private:
        T m_x;
        T m_y;
};

template <class T>
class orb_area
{
    public:
        orb_area(T w, T h) :
            m_w(w), m_h(h) {;};
        orb_area(const orb_area &area) : 
            m_w(area.get_w()), m_h(area.get_h()) {;};

        T get_w() const { return m_w; };
        T get_h() const { return m_h; };

        void set_w(T w) { m_w = w; };
        void set_h(T h) { m_h = h; };

    private:
        T m_w;
        T m_h;
};

#endif // _ORB_POINT_HPP

