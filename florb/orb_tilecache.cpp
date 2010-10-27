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
#include <cstring>
#include <iostream>
#include "orb_settings.hpp"
#include "orb_tilecache.hpp"

const std::string orb_tilecache::stmt_checknew = "\
    SELECT name FROM sqlite_master \
    WHERE type='table' \
    AND name='tiles';";
const std::string orb_tilecache::stmt_createschema = "\
    CREATE TABLE tiles( \
    z INTEGER, \
    x INTEGER, \
    y INTEGER, \
    data BLOB, \
    expires INTEGER, \
    PRIMARY KEY (z, x, y));";
const std::string orb_tilecache::stmt_insert = "\
    INSERT INTO tiles (z, x, y, data, expires) \
    VALUES (?1, ?2, ?3, ?4, ?5);";
const std::string orb_tilecache::stmt_get = "\
    SELECT data,expires FROM tiles \
    WHERE z = '%d' AND x = '%d' AND y = '%d';";
const std::string orb_tilecache::stmt_exists = "\
    SELECT COUNT(*) FROM tiles \
    WHERE z = '%d' AND x = '%d' AND y = '%d';";
const std::string orb_tilecache::stmt_delete = "\
    DELETE FROM tiles \
    WHERE z = '%d' AND x = '%d' AND y = '%d';";

orb_tilecache::orb_tilecache()
{
    // get the cache db location from the settings
    orb_settings &settings = orb_settings::get_instance();
    
    std::string dbpath;
    settings.getopt(std::string("osm::tilecache"), dbpath);
    
    // Create/open the database
    int rc = sqlite3_open(dbpath.c_str(), &m_db);

    if (rc != SQLITE_OK) {
        m_db = NULL;
        return;
    }

    // Check whether the database schema has previously been created
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare(m_db, stmt_checknew.c_str(), stmt_checknew.length(), &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(m_db);
        m_db = NULL;
        return;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        sqlite3_finalize(stmt); 
        return;
    }

    sqlite3_finalize(stmt);

    // Schema not present, create
    rc = sqlite3_prepare(m_db, stmt_createschema.c_str(), stmt_createschema.length(), &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(m_db);
        m_db = NULL;
        return;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt); 
        sqlite3_close(m_db);
        m_db = NULL;
        return;
    }

    sqlite3_finalize(stmt);
};

orb_tilecache::~orb_tilecache()
{
    if (m_db)
        sqlite3_close(m_db);
};

int orb_tilecache::put(int z, int x, int y, void *buf, size_t nbytes, time_t expires)
{
    if (!m_db)
        return 1;
    if (buf == NULL)
        return 1;
    if (nbytes < 1)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;

    sqlite3_stmt *stmt;

    char *query = sqlite3_mprintf(stmt_delete.c_str(), z, x, y);
    if (!query)
        return 1;

    // Drop the tile if it already exists
    int rc = sqlite3_prepare(m_db, query, strlen(query), &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_free(query);
        sqlite3_finalize(stmt);
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_free(query);
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_free(query);
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare(m_db, stmt_insert.c_str(), stmt_insert.length(), &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 1;
    }

    for (;;) {
        rc = sqlite3_bind_int(stmt, 1, z);
        if (rc != SQLITE_OK) break;
        rc = sqlite3_bind_int(stmt, 2, x);
        if (rc != SQLITE_OK) break;
        rc = sqlite3_bind_int(stmt, 3, y);
        if (rc != SQLITE_OK) break;
        rc = sqlite3_bind_blob(stmt, 4, buf, (int)nbytes, SQLITE_STATIC);
        if (rc != SQLITE_OK) break;
        rc = sqlite3_bind_int(stmt, 5, expires);
        if (rc != SQLITE_OK) break;

        break;
    }
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt); 
        return 1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt); 
        return 1;
    }

    sqlite3_finalize(stmt);

    return 0;
}

int orb_tilecache::get(int z, int x, int y, unsigned char **buf, size_t *nbytes)
{
    if (!m_db)
        return 1;
    if (buf == NULL)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;

    char *query = sqlite3_mprintf(stmt_get.c_str(), z, x, y);
    if (!query)
        return 1;

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare(m_db, query, strlen(query), &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 1;
    }

    // Execute the statement and see if we have the requested tile
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_free(query);
        return 1;
    }

    // Return the data
    int msize = (size_t)sqlite3_column_bytes(stmt, 0);
    unsigned char *mbuf = new unsigned char[msize];
    memcpy((void*)mbuf, sqlite3_column_blob(stmt, 0), msize);
    *buf = mbuf;
    if (nbytes != NULL)
        *nbytes = (size_t)msize;

    time_t expires = sqlite3_column_int64(stmt, 1);

    sqlite3_finalize(stmt);
    sqlite3_free(query);

    // Check wheter this tile has expired
    time_t now = time(NULL);
    if (now > expires)
        return 2;

    return 0;
}

