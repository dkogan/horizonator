#pragma once


typedef struct
{
  const char* name;
  float lat, lon, ele_m;
} poi_t;

bool annotate(// input
              const char* out_filename, // must be .pdf or .svg
              // assumed to be stored densely.
              const uint8_t* image_bgr,
              const float*   range_image,
              const int width,
              const int height,

              const poi_t* pois,
              const int Npois,
              const double lat,
              const double lon,
              const double az_deg0,
              const double az_deg1,
              const double ele_m);
