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

#include <cmath>
#include <FL/fl_draw.H>
#include "Fl/Fl_PNG_Memory_Image.hpp"
#include <FL/fl_ask.H>
#include "orb_osmlayer.hpp"
#include <iostream>

orb_osmlayer::orb_osmlayer() :
    orb_layer(),
    m_dodownload(true)
{
    name("Basemap");
    m_tileserver = new orb_tileserver(tileserver_callback, this);
    m_tilecache = new orb_tilecache();
};

orb_osmlayer::~orb_osmlayer()
{
    delete m_tileserver;
    delete m_tilecache;
};

void orb_osmlayer::draw(const orb_viewport &viewport)
{
    // Calculate the width and height of the current map
    unsigned int dimxy;
    orb_viewport::dim(viewport.z(), &dimxy);

    // Determine the starting tile and it's x and y offset for drawing an image
    // of the viewport
    unsigned int tidxx = viewport.x() / 256;
    unsigned int tidxy = viewport.y() / 256;
    int tdx = (int)(viewport.x() % 256);
    int tdy = (int)(viewport.y() % 256);

    int offsetx = -tdx;
    int offsety = -tdy;
    unsigned int draww = viewport.w(); 
    unsigned int drawh = viewport.h();

    // Fill the background with a pattern for when map < viewport or
    // tiles are missing 
    fl_rectf(0, 0, viewport.w(), viewport.h(), 80, 80, 80);

    // That's it, we got all the data, let's start drawing tiles
    while (offsetx < (int)draww) {
        int offsetytmp = offsety;
        unsigned int tidxytmp = tidxy;

        while (offsetytmp < (int)drawh) {
            int rc;
            unsigned char *tbuf;

            // Get the tile
            rc = m_tilecache->get(viewport.z(), tidxx, tidxytmp, &tbuf, NULL);

            // Tile has expired or does not exist
            if (rc != 0)
                download_tile(viewport.z(), tidxx, tidxytmp);

            // If OK, draw the tile onto the viewport bitmap
            if ((rc == 0) || (rc == 2)) {
                Fl_PNG_Memory_Image img(tbuf);
                img.draw(offsetx, offsetytmp);
                delete[] tbuf;
            }
            offsetytmp += 256;
            tidxytmp += 1;
        }

        offsetx += 256;
        tidxx += 1;
    }
}

void orb_osmlayer::tileserver_callback(const orb_tileserver::tileserver_response &r, void *userdata)
{
    orb_osmlayer *osmlayer = reinterpret_cast<orb_osmlayer*>(userdata);
    
    // Queue this response
    int rc = osmlayer->newtile_buffer(r);
    if (rc != 0)
        return;

    // Make FLTK call us back from the main thread
    Fl::lock();
    Fl::awake(tileserver_callback_main, userdata); 
    Fl::unlock();
}

void orb_osmlayer::tileserver_callback_main(void *userdata)
{
    orb_osmlayer *osml = reinterpret_cast<orb_osmlayer*>(userdata);

    // Access the tilecache from the main thread
    int rc = osml->newtile_cache();
    if (rc == 0)
        osml->do_callback();
}

int orb_osmlayer::newtile_buffer(const orb_tileserver::tileserver_response &r)
{
    int rc = m_newtiles.qput(r);

    // If we failed to buffer the tile we need to make sure the buffer it
    // occcupies is freed.
    if (rc != 0) {
        delete[] r.buf;
        return 1;
    }

    return 0;
}

int orb_osmlayer::newtile_cache()
{
    // Get a response from the queue
    orb_tileserver::tileserver_response r;
    m_newtiles.qget(r, orb_threadsafeq<orb_tileserver::tileserver_response>::Q_FRONT);

    // Check whether the tile has been successfully downloaded, if not alert the
    // user. This is probably not the right place to do it though, this should
    // much rather happen in the map control.
    if (r.rc != 0) {
        int rc = fl_choice(
                "Tile download failed. Retry?",
                "No",
                "Yes",
                NULL);
        if (rc != 1)
            m_dodownload = false;
        else
            m_dodownload = true;
    } else {
        m_tilecache->put(r.z, r.x, r.y, (void*)r.buf, r.nbytes, r.expires);
    }

    // Free the tile buffer
    delete[] r.buf;

    // Remove the corresponding entry from the list of pending requests
    for (std::vector<osmlayer_tilepos>::iterator iter=m_requested.begin();iter!=m_requested.end();++iter) {
        if (((*iter).z == r.z) && ((*iter).x == r.x) && ((*iter).y == r.y)) {
            m_requested.erase(iter);
            break;
        }
    }

    return 0;
}

int orb_osmlayer::download_tile(int z, int x, int y)
{
    // Check whether we're supposed to download tiles at all
    if (!m_dodownload)
        return 0;

    // Check whether we have sent that request already
    for (std::vector<osmlayer_tilepos>::const_iterator iter=m_requested.begin();iter!=m_requested.end();++iter) {
        if (((*iter).z == z) && ((*iter).x == x) && ((*iter).y == y))
            return 0;
    }

    // Request has not been sent, do it now
    int rc = m_tileserver->request(z, x, y);
    if (rc != 0)
        return 1;

    osmlayer_tilepos pos;
    pos.z = z; pos.x = x; pos.y = y;
    m_requested.push_back(pos);

    return 0;
}

