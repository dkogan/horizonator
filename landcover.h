#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dem.h"

// Per-vertex land-cover classification, sampled from pre-baked tiles built
// offline by build-landcover-tiles.py (ESA WorldCover base layer, IGN OCS
// GE overlay in France, CORINE Land Cover glacier overlay everywhere --
// see that script and README.org for details).
//
// This mirrors dem.h/dem.c on purpose: same 1-degree tile grid, same
// mmap-on-demand loading, same missing-tile tolerance. The tile geometry
// (origin, cell counts) is taken directly from an already-initialized
// horizonator_dem_context_t rather than recomputed, so the two grids can
// never disagree with each other
//
// Values are a stored byte format (baked into '.landcover' files on disk,
// see build-landcover-tiles.py), so existing values must never be
// renumbered or repurposed -- only append new ones after the last one, so
// tiles baked by an older version of the script keep meaning what they
// always meant
typedef enum
{
    LANDCOVER_UNKNOWN = 0, // no data here: caller should fall back to the
                           // procedural elevation/slope classification
    LANDCOVER_FOREST  = 1, // forest, subtype unknown -- WorldCover (which
                           // can't distinguish deciduous/conifer) and OCS
                           // GE's "mixed stand" class both land here
    LANDCOVER_GRASS   = 2,
    LANDCOVER_ROCK    = 3,
    LANDCOVER_SNOWICE = 4,
    LANDCOVER_WATER   = 5,

    // OCS GE-only distinctions (WorldCover has no equivalent class, so
    // these never come from that source): a real land-cover source with
    // per-country resolution better than WorldCover's should be free to
    // add more nuance than the 6 classes above, without disturbing them
    LANDCOVER_FOREST_DECIDUOUS = 6,
    LANDCOVER_FOREST_CONIFER   = 7,
    LANDCOVER_SHRUB            = 8, // alpine heath/scrub -- WorldCover's
                                    // Shrubland also maps here
} horizonator_landcover_class_t;

typedef struct
{
    unsigned char* tiles     [max_Ndems_ij][max_Ndems_ij];
    size_t         mmap_sizes[max_Ndems_ij][max_Ndems_ij];
    int            mmap_fd   [max_Ndems_ij][max_Ndems_ij];

    // Borrowed from the horizonator_dem_context_t this was init'd from: the
    // two grids share this geometry exactly, so it isn't recomputed here
    int origin_dem_lon_lat[2];
    int origin_dem_cellij [2];
    int Ndems_ij          [2];
    int cells_per_deg;
} horizonator_landcover_context_t;

// dem_ctx must already be initialized (horizonator_dem_init()). Its grid
// geometry is reused as-is. Missing '.landcover' tile files are tolerated
// (same as dem.c tolerates a missing '.hgt': a warning, and samples in that
// tile read back as LANDCOVER_UNKNOWN)
bool horizonator_landcover_init(// output
                                horizonator_landcover_context_t* ctx,

                                // input
                                const horizonator_dem_context_t* dem_ctx,
                                const char* datadir);

void horizonator_landcover_deinit( horizonator_landcover_context_t* ctx );

// Same (i,j) coordinate space as horizonator_dem_sample(): cell indices
// relative to the render origin
uint8_t horizonator_landcover_sample(const horizonator_landcover_context_t* ctx,
                                     int i,
                                     int j);
