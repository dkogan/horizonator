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

#include <cerrno>
#include <iostream>

#include "orb_semaphore.hpp"

orb_semaphore::orb_semaphore(int count) :
    m_sem(count),
    m_count(count)
{
    ;
};

orb_semaphore::~orb_semaphore()
{
    ;
};

int orb_semaphore::wait() 
{
    m_sem.wait();
    m_count--;
    return 0;
}

int orb_semaphore::post() 
{
    m_sem.post();
    m_count++;
    return 0;
}

int orb_semaphore::trywait() 
{
    bool rc = m_sem.try_wait();
    if (rc)
        m_count--;

    return (rc) ? 0 : 1;
}

int orb_semaphore::count(int &scount) 
{
    scount = m_count;
    return 0;
}

int orb_mutex::acquire()
{
    return (m_sem.wait()) ? 0 : 1;
}

int orb_mutex::release()
{
    return (m_sem.post()) ? 0 : 1;
}


