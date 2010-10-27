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

#ifndef _ORB_SETTINGS_HPP
#define _ORB_SETTINGS_HPP

#include <string>
#include <tinyxml.h>
#include <iostream>
#include <sstream>
#include <map>

class orb_settings
{
    public:
        ~orb_settings();
        static orb_settings& get_instance();
        int serialize();

        template <class T>
        int getopt(const std::string& key, T& t)
        {
            std::map<std::string,std::string>::iterator it;
        
            // Try to find the key
            it = m_settings.find(key);
            if (it == m_settings.end())
                return 1;
        
            std::istringstream iss((*it).second);
            return (iss >> t).fail() ? 1 : 0;
        }
        
        template <class T>
        int setopt(const std::string& key, const T& t)
        {
            std::ostringstream oss;
            oss << t;
        
            // Delete item if it already exists
            std::map<std::string,std::string>::iterator it;
            it = m_settings.find(key);
            if (it != m_settings.end())
                m_settings.erase(it);
        
            // Insert new item
            m_settings.insert(std::pair<std::string,std::string>(key, oss.str()));
        
            return 0;
        }

    private:
        orb_settings();
        orb_settings(const orb_settings&);

        int marshal();
        int parsetree(TiXmlNode *parent);

        std::string m_cfgdir;
        std::map<std::string, std::string> m_settings;
};

#endif // _ORB_SETTINGS_HPP

