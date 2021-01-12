#pragma once

#include <stdbool.h>
#include <stdint.h>


// Each SRTM file is a grid of 1201x1201 samples; last row/col overlap in neighboring DEMs
#define WDEM          1201
#define CELLS_PER_DEG (WDEM - 1) /* -1 because of the overlapping DEM edges */


// at most I allow a grid of this many DEMs. I can malloc the exact number, but
// this is easier
#define max_Ndems_ij 4

typedef struct
{
    unsigned char* dems      [max_Ndems_ij][max_Ndems_ij];
    size_t         mmap_sizes[max_Ndems_ij][max_Ndems_ij];
    int            mmap_fd   [max_Ndems_ij][max_Ndems_ij];

    // Which DEM contains the SW corner of the render data
    int            origin_dem_lon_lat[2];

    // Which cell in the origin DEM contains the SW corner of the render data
    int            origin_dem_cellij [2];

    // The lon/lat of the origin cell. This is quantized to the DEM cells
    float          origin_lon_lat    [2];

    // The lon/lat of the center cell. This is quantized to the DEM cells
    int            center_ij         [2];

    // How many DEMs, in each direction
    int            Ndems_ij          [2];
} dem_context_t;


// This library abstracts access to a set of DEM tiles, allowing the user to
// treat the whole set as one large DEM

bool dem_init(// output
              dem_context_t* ctx,

              // input
              float view_lat,
              float view_lon,

              // We will have 2*radius_cells per side
              int radius_cells );

void dem_deinit( dem_context_t* ctx );

// Given coordinates index cells, in respect to the origin cell
int16_t dem_sample(const dem_context_t* ctx,
                   // Positive = towards East
                   int i,
                   // Positive = towards North
                   int j);

float dem_elevation_at_center(const dem_context_t* ctx);

