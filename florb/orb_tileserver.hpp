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

#ifndef _ORB_TILESERVER_HPP
#define _ORB_TILESERVER_HPP

#include <string>
#include <time.h>
#include "orb_threadsafeq.hpp"
#include "orb_thread.hpp"

class orb_tileserver
{
    public:
        struct tileserver_response {
            int rc;
            int x;
            int y;
            int z;
            unsigned char *buf;
            size_t nbytes;
            time_t expires;
        };

        orb_tileserver(void (*callback)(const tileserver_response&, void*), void* userdata);
        ~orb_tileserver();

        int request(int z, int x, int y);

    private:
        struct tileserver_request {
            unsigned char *buf;
            size_t *nbytes;
            int x;
            int y;
            int z;
        };
        struct curl_userdata {
            unsigned char *buf;
            size_t free;
            size_t nbytes;
            time_t expires;
        };

        static size_t curl_cbdata(void *ptr, size_t size, size_t nmemb, void *data);
        static size_t curl_cbheader(void *ptr, size_t size, size_t nmemb, void *data);
        static void *wrapper(void *userdata);
        void entry();

        void (*m_callback)(const tileserver_response&, void*);
        void *m_userdata;
        orb_thread *m_thread;
        orb_threadsafeq<tileserver_request> m_q;
};

#endif // _ORB_TILESERVER_HPP

