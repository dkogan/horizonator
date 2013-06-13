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

#include <cstdlib>
#include <sstream>
#include <iostream>
#include <boost/filesystem.hpp>
#include "orb_settings.hpp"

orb_settings::orb_settings()
{
    // Get the user's home directory
    char *home = getenv("HOME");

    // Construct florb directory path
    std::ostringstream oss;
    oss << home << "/.florb";
    m_cfgdir = oss.str();

    // Create ".florb" in the user's home directory if needed
    boost::filesystem::create_directory(m_cfgdir);

    // Set some important base options
    setopt("osm::tileserver", "http://tile.openstreetmap.org/");
    setopt("osm::tilecache", std::string(m_cfgdir + "/cache.db"));
    setopt("osm::zoommin", 0);
    setopt("osm::zoommax", 18);

    // Load config file overwriting any previously set defaults
    marshal();
};

orb_settings::~orb_settings()
{
    serialize();
};

int orb_settings::serialize()
{
    TiXmlDocument doc;
	TiXmlDeclaration *decl = new TiXmlDeclaration("1.0", "", "");
    doc.LinkEndChild(decl);

    TiXmlElement *settings = new TiXmlElement("settings");

    std::map<std::string,std::string>::iterator it; 
    for (std::map<std::string,std::string>::iterator it=m_settings.begin();it!=m_settings.end();++it) {
        TiXmlElement *opt = new TiXmlElement("option");
        TiXmlElement *key = new TiXmlElement("key");
        TiXmlElement *val = new TiXmlElement("value");

        TiXmlText *txt_key = new TiXmlText(it->first);
        TiXmlText *txt_val = new TiXmlText(it->second);
        
        key->LinkEndChild(txt_key);
        val->LinkEndChild(txt_val);

        opt->LinkEndChild(key);
        opt->LinkEndChild(val);
        settings->LinkEndChild(opt);
    }
    
    doc.LinkEndChild(settings);
	doc.SaveFile(m_cfgdir+"/settings.xml");
    return 0;    
}

int orb_settings::marshal()
{
    TiXmlDocument doc(m_cfgdir+"/settings.xml");
    if (!doc.LoadFile())
        return 1;

    parsetree(doc.RootElement());
    return 0;
}

int orb_settings::parsetree(TiXmlNode *parent)
{
    int t = parent->Type();

    if (t != TiXmlNode::TINYXML_ELEMENT) {
        return 0;
    }

    std::string val(parent->Value());

    // Handle config option tree
    if (val.compare("option") == 0) {
        std::string key, value;

        // Get key and value child elements
        TiXmlNode *child;
        for (child = parent->FirstChild(); child != NULL; child = child->NextSibling()) {
            if (std::string(child->ToElement()->Value()).compare("key") == 0)
                key = child->ToElement()->GetText();
            else if (std::string(child->ToElement()->Value()).compare("value") == 0)
                value = child->ToElement()->GetText();
        }

        // Delete item if it already exists
        std::map<std::string,std::string>::iterator it;
        it = m_settings.find(key);
        if (it != m_settings.end())
            m_settings.erase(it);

        // Insert new item
        m_settings.insert(std::pair<std::string,std::string>(key, value));
    } else {
        // Recurse non-"option" Subtree
        TiXmlNode *child;
        for (child = parent->FirstChild(); child != NULL; child = child->NextSibling())
            parsetree(child);
    }

    return 0;
}

orb_settings& orb_settings::get_instance()
{
    static orb_settings instance;
    return instance;
}

