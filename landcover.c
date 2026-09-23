#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "landcover.h"
#include "util.h"

static
bool landcover_filename(// output
                        char* path, int bufsize,

                        // input
                        int demfileN, int demfileE,
                        const char* datadir )
{
    char ns;
    char we;

    if     ( demfileN >= 0 && demfileE >= 0 )
    {
        ns = 'N'; we = 'E';
    }
    else if( demfileN >= 0 && demfileE <  0 )
    {
        ns = 'N'; we = 'W';
        demfileE *= -1;
    }
    else if( demfileN  < 0 && demfileE >= 0 )
    {
        ns = 'S'; we = 'E';
        demfileN *= -1;
    }
    else
    {
        ns = 'S'; we = 'W';
        demfileN *= -1;
        demfileE *= -1;
    }

    if(datadir[0] == '~' && datadir[1] == '/' )
    {
        const char* home = getenv("HOME");
        if(home == NULL)
        {
            MSG("User asked for ~, but the 'HOME' env var isn't defined");
            return false;
        }
        if( snprintf(path, bufsize, "%s/%s/%c%.2d%c%.3d.landcover",
                     home,
                     &datadir[2],
                     ns, demfileN, we, demfileE) >= bufsize )
            return false;
    }
    else
    {
        if( snprintf(path, bufsize, "%s/%c%.2d%c%.3d.landcover",
                     datadir,
                     ns, demfileN, we, demfileE) >= bufsize )
            return false;
    }
    return true;
}

bool horizonator_landcover_init(// output
                                horizonator_landcover_context_t* ctx,

                                // input
                                const horizonator_dem_context_t* dem_ctx,
                                const char* datadir)
{
    *ctx = (horizonator_landcover_context_t){};

    // Borrow the DEM grid geometry outright: the two tile sets are keyed by
    // the exact same viewer/radius/SRTM-resolution choice, so recomputing
    // this here could only ever introduce a mismatch, never fix one
    memcpy(ctx->origin_dem_lon_lat, dem_ctx->origin_dem_lon_lat, sizeof(ctx->origin_dem_lon_lat));
    memcpy(ctx->origin_dem_cellij,  dem_ctx->origin_dem_cellij,  sizeof(ctx->origin_dem_cellij));
    memcpy(ctx->Ndems_ij,           dem_ctx->Ndems_ij,           sizeof(ctx->Ndems_ij));
    ctx->cells_per_deg = dem_ctx->cells_per_deg;

    const int expected_file_size = (ctx->cells_per_deg+1) * (ctx->cells_per_deg+1);

    for( int j = 0; j < ctx->Ndems_ij[1]; j++ )
        for( int i = 0; i < ctx->Ndems_ij[0]; i++ )
        {
            char filename[1024];
            if( !landcover_filename( filename, sizeof(filename),
                                     j + ctx->origin_dem_lon_lat[1],
                                     i + ctx->origin_dem_lon_lat[0],
                                     datadir) )
            {
                horizonator_landcover_deinit(ctx);
                MSG("Couldn't construct landcover filename" );
                return false;
            }

            struct stat sb;
            ctx->mmap_fd[i][j] = open( filename, O_RDONLY );
            if( ctx->mmap_fd[i][j] <= 0 )
            {
                // Not fatal: no real land-cover data here just means every
                // sample in this tile reads back as LANDCOVER_UNKNOWN, and
                // the fragment shader falls back to the procedural
                // elevation/slope classification for those points. Missing
                // tiles are the expected state until build-landcover-tiles.py
                // has been run for this area, so this is quiet (no warning)
                ctx->tiles     [i][j] = NULL;
                ctx->mmap_sizes[i][j] = 0;
                ctx->mmap_fd   [i][j] = 0;
                continue;
            }

            int res = fstat(ctx->mmap_fd[i][j], &sb);
            assert( res == 0 );

            // A tile that's the wrong size (most likely: built with the
            // other --SRTM1 setting than what we're using now) is treated
            // the same as a missing one -- a warning, not a hard failure.
            // materials_scale users could easily have a mix of DEM
            // resolutions cached (SRTM1 for one area, SRTM3 elsewhere,
            // built at different times), so this must stay recoverable:
            // aborting the whole render over one stale tile would be a
            // much worse outcome than just falling back to the procedural
            // classification for that tile's area
            if(sb.st_size == 0 || sb.st_size != expected_file_size)
            {
                if(sb.st_size != 0)
                    MSG("The landcover file '%s' has unexpected size (%zu; expected %d) -- ignoring it (was it built with a different --SRTM1 setting?)",
                        filename, (size_t)sb.st_size, expected_file_size);
                close(ctx->mmap_fd[i][j]);

                ctx->tiles     [i][j] = NULL;
                ctx->mmap_sizes[i][j] = 0;
                ctx->mmap_fd   [i][j] = 0;
                continue;
            }

            ctx->tiles     [i][j] = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, ctx->mmap_fd[i][j], 0);
            ctx->mmap_sizes[i][j] = sb.st_size;

            if( ctx->tiles[i][j] == MAP_FAILED )
            {
                horizonator_landcover_deinit(ctx);
                MSG("Couldn't mmap the landcover file '%s'", filename );
                return false;
            }
        }

    return true;
}

void horizonator_landcover_deinit( horizonator_landcover_context_t* ctx )
{
    for( int i=0; i<max_Ndems_ij; i++)
        for( int j=0; j<max_Ndems_ij; j++)
        {
            if( ctx->tiles[i][j] != NULL && ctx->tiles[i][j] != MAP_FAILED )
            {
                munmap( ctx->tiles[i][j], ctx->mmap_sizes[i][j] );
                ctx->tiles[i][j] = NULL;
            }
            if( ctx->mmap_fd[i][j] > 0 )
            {
                close( ctx->mmap_fd[i][j] );
                ctx->mmap_fd[i][j] = 0;
            }
        }
}

uint8_t horizonator_landcover_sample(const horizonator_landcover_context_t* ctx,
                                     int i,
                                     int j)
{
    if(i < 0 || j < 0) return LANDCOVER_UNKNOWN;

    int cell_ij[2] = {
        i + ctx->origin_dem_cellij[0],
        j + ctx->origin_dem_cellij[1] };

    int dem_ij[2];
    for(int i=0; i<2; i++)
    {
        dem_ij[i]  = cell_ij[i] / ctx->cells_per_deg;
        cell_ij[i] -= dem_ij[i] * ctx->cells_per_deg;

        if(cell_ij[i] == 0)
        {
            dem_ij [i]--;
            cell_ij[i] = ctx->cells_per_deg;
        }

        // dem_ij[i] can go negative right here: see the identical check in
        // horizonator_dem_sample() (dem.c) for why
        if( dem_ij[i] < 0 || dem_ij[i] >= ctx->Ndems_ij[i] ) return LANDCOVER_UNKNOWN;
    }

    const unsigned char* tile = ctx->tiles[dem_ij[0]][dem_ij[1]];
    if(tile == NULL)
        return LANDCOVER_UNKNOWN;

    // Same SW-origin flip as horizonator_dem_sample(), one byte per cell (no
    // endianness to worry about)
    uint32_t p =
        cell_ij[0] +
        (ctx->cells_per_deg - cell_ij[1])*(ctx->cells_per_deg+1);

    return tile[p];
}
