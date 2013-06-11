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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>

int orb_tilecache::put(int z, int x, int y, void *buf, size_t nbytes, time_t expires)
{
    if (buf == NULL)
        return 1;
    if (nbytes < 1)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;


    // not implemented yet
    return 0;
}

int orb_tilecache::get(int z, int x, int y, unsigned char **buf, size_t *nbytes)
{
    if (buf == NULL)
        return 1;
    if ((z < 0) || (x < 0) || (y < 0))
        return 1;

    char path[1024];
    snprintf(path, sizeof(path),
             "/home/dima/documents/n900/root/home/user/MyDocs/.maps/OpenStreetMap I/%d/%d/%d.png", z, x, y);

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

