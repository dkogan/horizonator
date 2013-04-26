#pragma once

struct poi_t
{
  const char* name;
  float lat, lon, ele_m;

  int draw_x, draw_y, draw_label_y;
};

void initPOIs( float lat, float lon );

// caller may modify the indices
void getPOIs( int** indices, int* N,
              struct poi_t** pois);
