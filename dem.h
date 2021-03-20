#pragma once

#include <stdbool.h>
#include <stdint.h>

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

    // How many DEMs, in each direction
    int            Ndems_ij          [2];

    // Copy of RENDER_RADIUS
    int radius_cells;

    int cells_per_deg;
} horizonator_dem_context_t;


// Prepare square set of cells for rendering. We abstract a set of DEM tiles,
// allowing the user to treat the whole set as one large DEM.
//
// There are (2*radius_cells)**2 cells in the render. This may encompass
// multiple DEMs. The data is prepared by calling this function, and can the be
// queries by horizonator_dem_sample(), which is agnostic about the multiple DEMs being
// sampled
//
// The grid starts at the SW corner. DEM tiles are named from the SW point
//
// The viewer sits between cell radius_cells-1 and radius_cells
bool horizonator_dem_init(// output
              horizonator_dem_context_t* ctx,

              // input
              float viewer_lat,
              float viewer_lon,

              // We will have 2*radius_cells per side
              int radius_cells,
              const char* datadir,

              bool SRTM1);

void horizonator_dem_deinit( horizonator_dem_context_t* ctx );

// Given coordinates index cells, in respect to the origin cell
int16_t horizonator_dem_sample(const horizonator_dem_context_t* ctx,
                   // Positive = towards East
                   int i,
                   // Positive = towards North
                   int j);

void horizonator_dem_bounds_latlon_deg(const horizonator_dem_context_t* ctx,
                                       float* lat0, float* lon0,
                                       float* lat1, float* lon1);
