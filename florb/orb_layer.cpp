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

#include <string>
#include "orb_layer.hpp"

orb_layer::orb_layer() :
    m_callback(NULL),
    m_userdata(NULL),
    m_name("N/A")
{
    ;
};

orb_layer::~orb_layer()
{
    ;
};

const std::string& orb_layer::name()
{
    return m_name;
}

int orb_layer::status()
{
    return m_status;
}

void orb_layer::name(const std::string &name)
{
    m_name = name;
}

void orb_layer::status(int status)
{
    m_status = status;
}

void orb_layer::callback(void (*callback)(void*), void* userdata)
{
    m_callback = callback;
    m_userdata = userdata;
}

void orb_layer::do_callback()
{
    if (m_callback)
        m_callback(m_userdata);
}

