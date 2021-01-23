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

#include "orb_tilecache.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>

static const char* basepath_homerelative = ".horizonator/tiles";
static char basepath[1024] = {'\0'};

static void make_basepath(void)
{
  if( !basepath[0] )
    snprintf(basepath, sizeof(basepath), "%s/%s", getenv("HOME"), basepath_homerelative );
}

int orb_tilecache::put(int z, int x, int y, void *buf, size_t nbytes,
                       time_t expires __attribute__((unused))
                       )
{
    if (buf == NULL)
        return 1;
    if (nbytes < 1)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;

    make_basepath();

    char path[1024];
    mkdir( basepath, 0777 );
    if(sizeof(path) <= (unsigned)snprintf(path, sizeof(path), "%s/%d", basepath, z))
    {
        fprintf(stderr, "snprintf out of bounds\n");
        return 1;
    }

    mkdir( path, 0777 );
    if(sizeof(path) <= (unsigned)snprintf(path, sizeof(path), "%s/%d/%d", basepath, z, x))
    {
        fprintf(stderr, "snprintf out of bounds\n");
        return 1;
    }

    mkdir( path, 0777 );
    if(sizeof(path) <= (unsigned)snprintf(path, sizeof(path), "%s/%d/%d/%d.png", basepath, z, x, y))
    {
        fprintf(stderr, "snprintf out of bounds\n");
        return 1;
    }


    int fd = open( path, O_CREAT | O_WRONLY, 0777  );
    if( fd <= 0 )
    {
      fprintf(stderr, "Couldn't open '%s' for writing\n", path );
      return 1;
    }

    write( fd, buf, nbytes );
    close(fd);

    return 0;
}

int orb_tilecache::get(int z, int x, int y, unsigned char **buf, size_t *nbytes)
{
    if (buf == NULL)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;

    make_basepath();

    char path[1024];
    if(sizeof(path) <= (unsigned)snprintf(path, sizeof(path), "%s/%d/%d/%d.png", basepath, z, x, y))
    {
        fprintf(stderr, "snprintf out of bounds\n");
        return 1;
    }

    struct stat filestat;
    if( stat( path, &filestat ) != 0 )
      return 1;

    *nbytes = filestat.st_size;

    int fd = open( path, O_RDONLY );
    if( fd <= 0 )
      return 1;

    *buf = (unsigned char*)mmap(NULL, filestat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( *buf == NULL || *buf == MAP_FAILED )
    {
      close(fd);
      return 1;
    }

    close(fd);
    return 0;
}

