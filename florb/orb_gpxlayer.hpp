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

#ifndef _ORB_GPXLAYER_HPP
#define _ORB_GPXLAYER_HPP

#include <string>
#include <iostream>
#include <tinyxml.h>
#include <vector>
#include <orb_layer.hpp>
#include "orb_point.hpp"
#include "orb_viewport.hpp"

class orb_gpxlayer : public orb_layer
{
    public:
        orb_gpxlayer(const std::string &path);
        ~orb_gpxlayer();

        void draw(const orb_viewport &viewport);

    private:
        int parsetree(TiXmlNode *parent);

        std::vector< orb_point<double> > m_trkpts;
};

#endif // _ORB_GPXLAYER_HPP

