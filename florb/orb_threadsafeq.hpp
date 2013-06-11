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

#ifndef _ORB_THREADSAFEQ_HPP
#define _ORB_THREADSAFEQ_HPP

#include <vector>
#include "orb_semaphore.hpp"

template <class T>
class orb_threadsafeq {
    public:
        static const int Q_FRONT = 0;
        static const int Q_BACK = 1;

        orb_threadsafeq() : m_counter(0) {;};
        ~orb_threadsafeq() {;};

        int qput(const T &item) {
            orb_mutexlocker lock(m_protector);
            m_container.push_back(item);
            m_counter.post();

            return 0;
        };
        int qget(T &item, int from) {
            m_counter.wait();
            orb_mutexlocker lock(m_protector);

            if (from == Q_FRONT) {
                item = m_container.front();
                m_container.erase(m_container.begin());
                return 0;
            }
            if (from == Q_BACK) {
                item = m_container.back();
                m_container.erase(--m_container.end());
                return 0;
            }
            
            return 1;
        }

    private:
        std::vector<T> m_container;
        orb_semaphore m_counter;
        orb_mutex m_protector;
};

#endif // _ORB_THREADSAFEQ_HPP

