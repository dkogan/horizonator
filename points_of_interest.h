#pragma once

struct poi_t
{
  const char* name;
  float lat, lon, ele_m;
};

void initPOIs( float lat, float lon );
const struct poi_i* getPOI( int idx );
