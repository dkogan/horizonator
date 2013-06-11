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

#ifndef _ORB_SEMAPHORE_HPP
#define _ORB_SEMAPHORE_HPP

#include <boost/interprocess/sync/interprocess_semaphore.hpp>

class orb_semaphore
{
    public:
        orb_semaphore(int count);
        ~orb_semaphore();

        int wait();
        int post();
        int trywait();
        int count(int &scount);

    private:
        boost::interprocess::interprocess_semaphore m_sem;
        int m_count;
};

class orb_mutex
{
    public:
        orb_mutex() : m_sem(1) {;};
        ~orb_mutex() {;};

        int acquire();
        int release();

    private:
        orb_semaphore m_sem;
};

class orb_mutexlocker
{
    public:
        orb_mutexlocker(orb_mutex &mutex) : m_mutex(mutex) {
            m_mutex.acquire();
        };
        ~orb_mutexlocker() {
            m_mutex.release();
        }

    private:
        orb_mutex &m_mutex;
};

#endif // _ORB_SEMAPHORE_HPP

