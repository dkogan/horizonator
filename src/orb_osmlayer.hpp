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

#ifndef _ORB_OSMLAYER_HPP
#define _ORB_OSMLAYER_HPP

#include <string>
#include <orb_layer.hpp>
#include "orb_viewport.hpp"
#include "orb_tileserver.hpp"
#include "orb_tilecache.hpp"

class orb_osmlayer : public orb_layer
{
    public:
        orb_osmlayer();
        ~orb_osmlayer();

        void draw(const orb_viewport &viewport);

    private:
        struct osmlayer_tilepos {
            int z;
            int x;
            int y;
        };

        static void tileserver_callback(const orb_tileserver::tileserver_response &r, void *userdata);
        static void tileserver_callback_main(void *userdata);

        int download_tile(int z, int x, int y); 
        int newtile_buffer(const orb_tileserver::tileserver_response &r);
        int newtile_cache();

        orb_tileserver *m_tileserver;
        orb_tilecache *m_tilecache;
        orb_threadsafeq<orb_tileserver::tileserver_response> m_newtiles;
        std::vector<osmlayer_tilepos> m_requested;
        bool m_dodownload;
};

#endif // _ORB_OSMLAYER_HPP

