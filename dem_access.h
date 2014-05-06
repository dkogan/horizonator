#pragma once

#include <stdbool.h>
#include <stdint.h>

// Each SRTM file is a grid of 1201x1201 samples; last row/col overlap in neighboring DEMs
#define WDEM          1201
#define CELLS_PER_DEG (WDEM - 1) /* -1 because of the overlapping DEM edges */


// This library abstracts access to a set of DEM tiles, allowing the user to
// treat the whole set as one large DEM

bool dem_init(// output
              int* view_i, int* view_j,
              float* renderStartN, float* renderStartE,

              // input
              float view_lat, float view_lon, int R_RENDER );

void dem_deinit(void);
int16_t sampleDEMs(int i, int j);
float getViewerHeight(int i, int j);
