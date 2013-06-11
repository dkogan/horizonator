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

#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <curl/curl.h>
#include <curl/easy.h>

#include "orb_settings.hpp"
#include "orb_tileserver.hpp"

orb_tileserver::orb_tileserver(void (*callback)(const tileserver_response &, void*), void* userdata) :
    m_callback(callback), 
    m_userdata(userdata)
{
    // Init curl
    curl_global_init(CURL_GLOBAL_ALL);

    // Create and run worker thread
    m_thread = new orb_thread(wrapper);
    if (m_thread)
        m_thread->run(this);
}

#include <iostream>

orb_tileserver::~orb_tileserver()
{
    if (!m_thread)
        return;

    // Put a dummy request into the queue to unblock the worker thread.
    request(-1, -1, -1);

    // Don't actually block waiting for the thread to exit
    //m_thread->cancel();

    delete(m_thread);
}

int orb_tileserver::request(int z, int x, int y)
{
    // We need to allow negative values for cancelation request messages
    //if ((x < 0) || (y < 0) || (z < 0))
    //    return 1;

    // Create a download request
    tileserver_request r;
    r.z = z;
    r.x = x;
    r.y = y;

    // Try to queue the request
    int rc = m_q.qput(r);
    return (rc == 0) ? 0 : 1;
}

void orb_tileserver::entry()
{
    for(;;) {

        // Check for requests and always treat the most recent one first
        tileserver_request r;
        int rc = m_q.qget(r, orb_threadsafeq<tileserver_request>::Q_BACK);
        if (rc != 0)
            continue;
 
        // Check whether this is a dummy request and we need to terminate
        if (r.x < 0)
            return;

        orb_settings &settings = orb_settings::get_instance();

        // Get the tileserver URL from the settings
        std::string tileserver;
        settings.getopt(std::string("osm::tileserver"), tileserver);

        std::cout << "downloading " << tileserver << r.z << "/" << r.x << "/" << r.y << ".png" << std::endl;

        // Construct URL
        std::ostringstream ss;
        ss << tileserver << r.z << "/" << r.x << "/" << r.y << ".png";

        // This piece of code will come in handy if we should ever go unicode
#if 0
        char asciiurl[256];
        wcstombs(asciiurl, ss.str().c_str(), 255);
        asciiurl[255] = '\0';
#endif

        // Setup CURL
        CURL *curl_handle = curl_easy_init();
        curl_easy_setopt(curl_handle, CURLOPT_URL, ss.str().c_str());
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_cbdata);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, curl_cbheader);

        curl_userdata ud;
        ud.buf = new unsigned char[50000];
        ud.free = 50000;
        ud.nbytes = 0;

        // Set expiry to one week in the future in case the tileserver does not
        // send a corresponding header
        time_t now = time(NULL);
        ud.expires = now + (7*24*60*60);

        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&ud);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEHEADER, (void*)&ud); 
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "florb/0.1");

        // Perform download
        rc = curl_easy_perform(curl_handle);
        curl_easy_cleanup(curl_handle);

        // Call back to the application
        tileserver_response res;
        res.rc = (rc == 0) ? 0 : 1;
        res.x = r.x;
        res.y = r.y;
        res.z = r.z;
        res.buf = ud.buf;
        res.nbytes = ud.nbytes;
        res.expires = ud.expires;

        m_callback(res, m_userdata);
    }
}

size_t orb_tileserver::curl_cbdata(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct curl_userdata *ud = (struct curl_userdata*)data;

    if (realsize > ud->free)
        return 0;

    memcpy((void*)&ud->buf[ud->nbytes], ptr, realsize);
    ud->free -= realsize;
    ud->nbytes += realsize;

    return realsize;
}

size_t orb_tileserver::curl_cbheader(void *ptr, size_t size, size_t nmemb, void *data) 
{
    size_t realsize = size * nmemb;
    struct curl_userdata *ud = (struct curl_userdata*)data;
    std::string line;

    // Construct the whole line
    for (unsigned int i=0;i<realsize;i++)
        line += ((char*)ptr)[i];
    
    // Not an expires header
    if (line.find("Expires: ") == std::string::npos)
        return realsize;

    // Extract the date string from the expires header
    line = line.substr(strlen("Expires: "), line.size()-strlen("Expires: ")-1);    

    // Convert string to time_t
    ud->expires = curl_getdate(line.c_str(), NULL);

    return realsize;
}

void *orb_tileserver::wrapper(void *userdata)
{
    reinterpret_cast<orb_tileserver*>(userdata)->entry();
    return (void*)0;
}

