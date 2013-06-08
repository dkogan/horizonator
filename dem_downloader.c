#include <stdio.h>
#include <stdlib.h>

const char* getDEM_filename( int demfileN, int demfileW )
{
  static char filename[1024];
  snprintf(filename, sizeof(filename), "data/N%dW%d.srtm3.hgt", demfileN, demfileW);
  return filename;

  // downloading not yet implemented
}

