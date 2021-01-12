#include <tgmath.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "dem.h"
#include "util.h"

static
bool dem_filename(// output
                  char* path, int bufsize,

                  // input
                  int demfileN, int demfileE )
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

    if( snprintf(path, bufsize, "%c%.2d%c%.3d.hgt", ns, demfileN, we, demfileE) >= bufsize )
        return false;

    return true;
}

bool dem_init(// output
              dem_context_t* ctx,

              // input
              float view_lat,
              float view_lon,

              // We will have 2*radius_cells per side
              int radius_cells )
{
    const float view_lon_lat[2] = {view_lon, view_lat};

    *ctx = (dem_context_t){};

    // I render a square with the given radius, centered at the view point.
    // There are (2*radius_cells)**2 cells in the render. In all likelihood this
    // will encompass multiple DEMs. The base DEM is the one that contains the
    // viewpoint. I compute the latlon coords of the base DEM origin and of the
    // render origin. I also compute the grid coords of the base DEM origin
    // (grid coords of the render origin are 0,0 by definition)
    //
    // grid starts at the SW corner. DEM tile is named from the SW point
    for(int i=0; i<2; i++)
    {
        float origin_lon_lat = view_lon_lat[i] - (float)radius_cells/CELLS_PER_DEG;

        // Which DEM contains the SW corner of the render data
        ctx->origin_dem_lon_lat[i] = (int)floor(origin_lon_lat);

        // Which cell in the origin DEM contains the SW corner of the render data
        ctx->origin_dem_cellij [i] = (int)round( (origin_lon_lat - ctx->origin_dem_lon_lat[i]) * CELLS_PER_DEG );

        // The lon/lat of the origin cell. This is origin_lon_lat, quantized to
        // the DEM cells
        ctx->origin_lon_lat[i] = ctx->origin_dem_lon_lat[i] + (float)ctx->origin_dem_cellij[i] / (float)CELLS_PER_DEG;

        // I will have 2*radius_cells
        int cellij_last = ctx->origin_dem_cellij[i] + radius_cells*2-1;
        int idem_last   = cellij_last / CELLS_PER_DEG;
        ctx->Ndems_ij[i] = idem_last + 1;
        if( cellij_last == idem_last*CELLS_PER_DEG )
        {
            // The last cell in my render is the first cell in the DEM. But
            // adjacent DEMs have one row/col of overlap, so I can use the last
            // row of the previous DEM
            ctx->Ndems_ij[i]--;
        }

        if( ctx->Ndems_ij[i] > max_Ndems_ij )
        {
            dem_deinit(ctx);
            MSG("Requested radius too large. Increase the compile-time-constant %d", max_Ndems_ij);
            return false;
        }

        ctx->center_ij[i] = radius_cells;
    }

    // I now load my DEMs. Each dems[] is a pointer to an mmap-ed source file.
    // The ordering of dems[] is increasing latlon, with lon varying faster
    for( int j = 0; j < ctx->Ndems_ij[1]; j++ )
        for( int i = 0; i < ctx->Ndems_ij[0]; i++ )
        {
            char filename[1024];
            if( !dem_filename( filename, sizeof(filename),
                               j + ctx->origin_dem_lon_lat[1],
                               i + ctx->origin_dem_lon_lat[0]) )
            {
                dem_deinit(ctx);
                MSG("Couldn't construct DEM filename" );
                return false;
            }

            struct stat sb;
            ctx->mmap_fd[i][j] = open( filename, O_RDONLY );
            if( ctx->mmap_fd[i][j] <= 0 )
            {
                dem_deinit(ctx);
                MSG("Couldn't open DEM file '%s'", filename );
                return false;
            }

            assert( fstat(ctx->mmap_fd[i][j], &sb) == 0 );

            ctx->dems      [i][j] = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, ctx->mmap_fd[i][j], 0);
            ctx->mmap_sizes[i][j] = sb.st_size;

            if( ctx->dems[i][j] == MAP_FAILED )
            {
                dem_deinit(ctx);
                MSG("Couldn't mmap the DEM file '%s'", filename );
                return false;
            }

            if( WDEM*WDEM*2 != sb.st_size )
            {
                dem_deinit(ctx);
                MSG("The DEM file '%s' has unexpected size. Is this a 3-arc-sec SRTM DEM?", filename );
                return false;
            }
        }

    return true;
}

void dem_deinit( dem_context_t* ctx )
{
    for( int i=0; i<max_Ndems_ij; i++)
        for( int j=0; j<max_Ndems_ij; j++)
        {
            if( ctx->dems[i][j] != NULL && ctx->dems[i][j] != MAP_FAILED )
            {
                munmap( ctx->dems[i][j], ctx->mmap_sizes[i][j] );
                ctx->dems[i][j] = NULL;
            }
            if( ctx->mmap_fd[i][j] > 0 )
            {
                close( ctx->mmap_fd[i][j] );
                ctx->mmap_fd[i][j] = 0;
            }
        }
}

// Given coordinates index cells, in respect to the origin cell
int16_t dem_sample(const dem_context_t* ctx,
                   // Positive = towards East
                   int i,
                   // Positive = towards North
                   int j)
{
    if(i < 0 || j < 0) return -1;

    // Cell coordinates inside my whole render area. Across multiple DEMs
    int cell_ij[2] = {
        i + ctx->origin_dem_cellij[0],
        j + ctx->origin_dem_cellij[1] };

    int dem_ij[2];
    for(int i=0; i<2; i++)
    {
        dem_ij[i]  = cell_ij[i] / CELLS_PER_DEG;

        // cell coordinates inside the one DEM containing the cell
        cell_ij[i] -= dem_ij[i] * CELLS_PER_DEG;

        // Adjacent DEMs have one row/col of overlap, so I can use the last row
        // of the previous DEM
        if(cell_ij[i] == 0)
        {
            dem_ij [i]--;
            cell_ij[i] = CELLS_PER_DEG;
        }

        if( dem_ij[i] >= ctx->Ndems_ij[i] ) return -1;
    }

    const unsigned char* dem = ctx->dems[dem_ij[0]][dem_ij[1]];

    uint32_t p =
        cell_ij[0] +
        // DEM starts at NW corner. I flip it around to start my data at the SW
        // corner
        (WDEM-1 - cell_ij[1])*WDEM;

    // Each value is big-endian
    int16_t  z = (int16_t) ((dem[2*p] << 8) | dem[2*p + 1]);
    return (z < 0) ? 0 : z;
}

float dem_elevation_at_center(const dem_context_t* ctx)
{
#define MAX(_a,_b) ({__auto_type a = _a; __auto_type b = _b; a > b ? a : b; })
    return MAX( MAX( MAX( dem_sample( ctx, ctx->center_ij[0],   ctx->center_ij[1]   ),
                          dem_sample( ctx, ctx->center_ij[0]+1, ctx->center_ij[1]   )),
                          dem_sample( ctx, ctx->center_ij[0],   ctx->center_ij[1]+1 )),
                          dem_sample( ctx, ctx->center_ij[0]+1, ctx->center_ij[1]+1 ));
#undef MAX
}
