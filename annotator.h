#pragma once


typedef struct
{
  const char* name;
  float lat, lon, ele_m;
} poi_t;

typedef struct
{
  int    poi_index; // index into the pois[] array that was passed in
  double x,y;        // screen position in the render
  double range;      // distance from the viewer, in meters
} visible_poi_t;

// Figures out which of pois[] are actually visible in the given render (not
// occluded by closer terrain), same logic annotate() uses internally to
// place labels. Returns the count of visible POIs, with that many entries
// filled in in visible[] (which must have room for Npois entries)
int find_visible_pois(// output: room for Npois entries
                      visible_poi_t* visible,

                      // input
                      const float* range_image,
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

                      // POIs farther than this are never returned. Pass the
                      // same zfar used for the render: there's no point
                      // considering something farther than what was
                      // actually rendered
                      const double max_marker_dist_m);

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
              const double max_marker_dist_m,

              // Label text height, in real typographic points (1/72in),
              // as it'll appear in the output PDF/SVG
              const double label_font_size_pt);
