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

#define MSG(fmt, ...) fprintf(stderr, "%s(%d): " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

static
const bool dem_filename(// output
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
              float view_lat, float view_lon, int radius )
{
    memset( ctx, 0, sizeof(*ctx) );

    // I render a square with the given radius, centered at the view point.
    // There are (2*radius)**2 cells in the render. In all likelihood this will
    // encompass multiple DEMs. The base DEM is the one that contains the
    // viewpoint. I compute the latlon coords of the base DEM origin and of the
    // render origin. I also compute the grid coords of the base DEM origin
    // (grid coords of the render origin are 0,0 by definition)
    //
    // grid starts at the NW corner, and traverses along the latitude first.
    // DEM tile is named from the SW point
    // latlon of the render origin
    float renderStartE_unaligned = view_lon - (float)radius/CELLS_PER_DEG;
    float renderStartN_unaligned = view_lat - (float)radius/CELLS_PER_DEG;

    ctx->renderStartDEMfileE = floor(renderStartE_unaligned);
    ctx->renderStartDEMfileN = floor(renderStartN_unaligned);

    ctx->renderStartDEMcoords_i = round( (renderStartE_unaligned - ctx->renderStartDEMfileE) * CELLS_PER_DEG );
    ctx->renderStartDEMcoords_j = round( (renderStartN_unaligned - ctx->renderStartDEMfileN) * CELLS_PER_DEG );

    ctx->renderStartE = ctx->renderStartDEMfileE + (float)ctx->renderStartDEMcoords_i / (float)CELLS_PER_DEG;
    ctx->renderStartN = ctx->renderStartDEMfileN + (float)ctx->renderStartDEMcoords_j / (float)CELLS_PER_DEG;

    // 2*radius - 1 is the last cell.
    int renderEndDEMfileE = (ctx->renderStartDEMfileE * CELLS_PER_DEG + ctx->renderStartDEMcoords_i + 2*radius-1 ) / CELLS_PER_DEG;
    int renderEndDEMfileN = (ctx->renderStartDEMfileN * CELLS_PER_DEG + ctx->renderStartDEMcoords_j + 2*radius-1 ) / CELLS_PER_DEG;

    // If the last cell is the first on in a DEM, I can stay at the previous
    // DEM, since there's 1 row/col overlap between each adjacent pairs of DEMs
    if( (ctx->renderStartDEMcoords_i + 2*radius-1) % CELLS_PER_DEG == 0 )
        renderEndDEMfileE--;
    if( (ctx->renderStartDEMcoords_j + 2*radius-1) % CELLS_PER_DEG == 0 )
        renderEndDEMfileN--;

    ctx->Ndems_i = renderEndDEMfileE - ctx->renderStartDEMfileE + 1;
    ctx->Ndems_j = renderEndDEMfileN - ctx->renderStartDEMfileN + 1;
    if( ctx->Ndems_i > max_Ndems_ij || ctx->Ndems_j > max_Ndems_ij )
    {
        dem_deinit(ctx);
        return false;
    }

    ctx->view_i = floor( ( view_lon - ctx->renderStartE) * CELLS_PER_DEG );
    ctx->view_j = floor( ( view_lat - ctx->renderStartN) * CELLS_PER_DEG );


    // I now load my DEMs. Each dems[] is a pointer to an mmap-ed source file.
    // The ordering of dems[] is increasing latlon, with lon varying faster
    for( int j = 0; j < ctx->Ndems_j; j++ )
    {
        for( int i = 0; i < ctx->Ndems_i; i++ )
        {
            char filename[1024];
            if( !dem_filename( filename, sizeof(filename),
                               j + ctx->renderStartDEMfileN,
                               i + ctx->renderStartDEMfileE) )
            {
                dem_deinit(ctx);
                MSG("couldn't construct DEM filename" );
                return false;
            }

            struct stat sb;
            ctx->mmap_fd[i][j] = open( filename, O_RDONLY );
            if( ctx->mmap_fd[i][j] <= 0 )
            {
                dem_deinit(ctx);
                MSG("couldn't open DEM file '%s'", filename );
                return false;
            }

            assert( fstat(ctx->mmap_fd[i][j], &sb) == 0 );

            ctx->dems      [i][j] = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, ctx->mmap_fd[i][j], 0);
            ctx->mmap_sizes[i][j] = sb.st_size;

            if( ctx->dems[i][j] == MAP_FAILED )
            {
                dem_deinit(ctx);
                return false;
            }

            if( WDEM*WDEM*2 != sb.st_size )
            {
                dem_deinit(ctx);
                return false;
            }
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

// These functions take in coordinates INSIDE THEIR SPECIFIC DEM
int16_t dem_sample(const dem_context_t* ctx,
                   int i, int j)
{
    int i_dem = (i + ctx->renderStartDEMcoords_i) % CELLS_PER_DEG;
    int j_dem = (j + ctx->renderStartDEMcoords_j) % CELLS_PER_DEG;
    int DEMfileN = ctx->renderStartDEMfileN + ((j + ctx->renderStartDEMcoords_j) / CELLS_PER_DEG);
    int DEMfileE = ctx->renderStartDEMfileE + ((i + ctx->renderStartDEMcoords_i) / CELLS_PER_DEG);

    int demIndex_i = DEMfileE - ctx->renderStartDEMfileE;
    int demIndex_j = DEMfileN - ctx->renderStartDEMfileN;
    if( demIndex_i < 0 || demIndex_i >= ctx->Ndems_i ||
        demIndex_j < 0 || demIndex_j >= ctx->Ndems_j )
    {
        return -1;
    }

    const unsigned char* dem = ctx->dems[demIndex_i][demIndex_j];

    // The DEMs are organized north to south, so I flip around the j accessor to
    // keep all accesses ordered by increasing lat, lon
    uint32_t p = i_dem + (WDEM-1 - j_dem )*WDEM;
    int16_t  z = (int16_t) ((dem[2*p] << 8) | dem[2*p + 1]);
    return (z < 0) ? 0 : z;
}

// These functions take in coordinates INSIDE THEIR SPECIFIC DEM
float dem_elevation_at_center(const dem_context_t* ctx)
{
#define MAX(_a,_b) ({__auto_type a = _a; __auto_type b = _b; a > b ? a : b; })
    return MAX( MAX( MAX( dem_sample( ctx, ctx->view_i,   ctx->view_j   ),
                          dem_sample( ctx, ctx->view_i+1, ctx->view_j   )),
                          dem_sample( ctx, ctx->view_i,   ctx->view_j+1 )),
                          dem_sample( ctx, ctx->view_i+1, ctx->view_j+1 ));
#undef MAX
}
