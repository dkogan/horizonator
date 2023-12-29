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

#include "orb_thread.hpp"

orb_thread::orb_thread(threadfct_t fct) :
    m_fct(fct)
{
    ;
}

orb_thread::~orb_thread()
{
    if (m_thread)
        delete m_thread;
}

int orb_thread::run(void *userdata) 
{
    m_thread = new boost::thread(
            boost::bind(m_fct, userdata));

    return 0;
}

int orb_thread::cancel() 
{
    if (!m_thread)
        return 1;

    m_thread->join();

    return 0;
}

