#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// tries to read a DEM at the path. Returns true/false based on whether it looks
// like a good DEM
static int works( const char* path )
{
  // I should be able to stat() the DEM
  struct stat buf;
  if( stat( path, &buf ) )
    return 0;

  // The DEM should be the right size
  if( buf.st_size != 1201*1201*2 )
    return 0;

  // The DEM should be a "regular" file
  if( !S_ISREG(buf.st_mode) )
    return 0;

  // The DEM should be readable
  int fd = open( path, O_RDONLY );
  close(fd);
  if( fd <= 0 )
    return 0;

  // All good
  return 1;
}

const char* getDEM_filename( int demfileN, int demfileE )
{
  static char path[1024];
  char filename[1024];

  if     ( demfileN >= 0 && demfileE >= 0 )
    snprintf(filename, sizeof(filename), "N%.2dE%.3d.hgt", demfileN, demfileE);
  else if( demfileN >= 0 && demfileE <  0 )
    snprintf(filename, sizeof(filename), "N%.2dW%.3d.hgt", demfileN, -demfileE);
  else if( demfileN  < 0 && demfileE >= 0 )
    snprintf(filename, sizeof(filename), "S%.2dE%.3d.hgt", -demfileN, demfileE);
  else
    snprintf(filename, sizeof(filename), "S%.2dW%.3d.hgt", -demfileN, -demfileE);


  snprintf(path, sizeof(path), "%s", filename);
  if( works( path ) )
    return path;

  snprintf(path, sizeof(path), "DEMs_SRTM3/%s", filename);
  if( works( path ) )
    return path;

  snprintf(path, sizeof(path), "%s/.horizonator/DEMs_SRTM3/%s", getenv("HOME"), filename);
  if( works( path ) )
    return path;

  return NULL;
  // downloading not yet implemented
}

