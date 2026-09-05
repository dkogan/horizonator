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
              const int cut_off_bottom_px,

              const poi_t* pois,
              const int Npois,
              const double lat,
              const double lon,
              const double az_deg0,
              const double az_deg1,
              const double ele_m,

              // Must match whatever was passed to horizonator_set_curvature()
              // for this render, or POIs will be placed incorrectly and fail
              // to be found in the range image (looking occluded)
              const bool   curvature_enabled,
              const double refraction_k,

              // POIs farther than this are never labelled. Pass the same
              // zfar used for the render: there's no point labelling
              // something farther than what was actually rendered
              const double max_marker_dist_m);
