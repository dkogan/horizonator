#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "dem_access.h"
#include "dem_downloader.h"

// at most I allow a grid of this many DEMs. I can malloc the exact number, but
// this is easier
#define max_Ndems_ij 4

static unsigned char* dems      [max_Ndems_ij][max_Ndems_ij];
static size_t         mmap_sizes[max_Ndems_ij][max_Ndems_ij];
static int            mmap_fd   [max_Ndems_ij][max_Ndems_ij];
static int            renderStartDEMcoords_i, renderStartDEMcoords_j;
static int            renderStartDEMfileE,    renderStartDEMfileN;
static int            Ndems_i, Ndems_j;

static bool inited = false;

bool dem_init(// output
              int* view_i, int* view_j,
              float* renderStartN, float* renderStartE,

              // input
              float view_lat, float view_lon, int R_RENDER )
{
    if( inited )
        return false;

    // I render a square with radius R_RENDER centered at the view point. There
    // are (2*R_RENDER)**2 cells in the render. In all likelihood this will
    // encompass multiple DEMs. The base DEM is the one that contains the
    // viewpoint. I compute the latlon coords of the base DEM origin and of the
    // render origin. I also compute the grid coords of the base DEM origin (grid
    // coords of the render origin are 0,0 by definition)
    //
    // grid starts at the NW corner, and traverses along the latitude first.
    // DEM tile is named from the SW point
    // latlon of the render origin
    float renderStartE_unaligned = view_lon - (float)R_RENDER/CELLS_PER_DEG;
    float renderStartN_unaligned = view_lat - (float)R_RENDER/CELLS_PER_DEG;

    renderStartDEMfileE = floor(renderStartE_unaligned);
    renderStartDEMfileN = floor(renderStartN_unaligned);

    renderStartDEMcoords_i = round( (renderStartE_unaligned - renderStartDEMfileE) * CELLS_PER_DEG );
    renderStartDEMcoords_j = round( (renderStartN_unaligned - renderStartDEMfileN) * CELLS_PER_DEG );

    *renderStartE = renderStartDEMfileE + (float)renderStartDEMcoords_i / (float)CELLS_PER_DEG;
    *renderStartN = renderStartDEMfileN + (float)renderStartDEMcoords_j / (float)CELLS_PER_DEG;

    // 2*R_RENDER - 1 is the last cell.
    int renderEndDEMfileE = renderStartDEMfileE + (renderStartDEMcoords_i + 2*R_RENDER-1 ) / CELLS_PER_DEG;
    int renderEndDEMfileN = renderStartDEMfileN + (renderStartDEMcoords_j + 2*R_RENDER-1 ) / CELLS_PER_DEG;

    // If the last cell is the first on in a DEM, I can stay at the previous
    // DEM, since there's 1 row/col overlap between each adjacent pairs of DEMs
    if( (renderStartDEMcoords_i + 2*R_RENDER-1) % CELLS_PER_DEG == 0 )
        renderEndDEMfileE--;
    if( (renderStartDEMcoords_j + 2*R_RENDER-1) % CELLS_PER_DEG == 0 )
        renderEndDEMfileN--;

    Ndems_i = renderEndDEMfileE - renderStartDEMfileE + 1;
    Ndems_j = renderEndDEMfileN - renderStartDEMfileN + 1;
    if( Ndems_i > max_Ndems_ij || Ndems_j > max_Ndems_ij )
        return false;

    *view_i = floor( ( view_lon - (float)(*renderStartE)) * CELLS_PER_DEG );
    *view_j = floor( ( view_lat - (float)(*renderStartN)) * CELLS_PER_DEG );


    // I now load my DEMs. Each dems[] is a pointer to an mmap-ed source file.
    // The ordering of dems[] is increasing latlon, with lon varying faster
    memset( dems, 0, sizeof(dems) );
    for( int j = 0; j < Ndems_j; j++ )
    {
        for( int i = 0; i < Ndems_i; i++ )
        {
            // This function will try to download the DEM if it's not found
            const char* filename = dem_downloader_get_filename( j + renderStartDEMfileN,
                                                                i + renderStartDEMfileE);
            if( filename == NULL )
                return false;

            struct stat sb;
            mmap_fd[i][j] = open( filename, O_RDONLY );
            if( mmap_fd[i][j] <= 0 )
            {
                dem_deinit();
                fprintf(stderr, "couldn't open DEM file '%s'\n", filename );
                return false;
            }

            assert( fstat(mmap_fd[i][j], &sb) == 0 );

            dems      [i][j] = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, mmap_fd[i][j], 0);
            mmap_sizes[i][j] = sb.st_size;

            if( dems[i][j] == MAP_FAILED )
            {
                dem_deinit();
                return false;
            }

            if( WDEM*WDEM*2 != sb.st_size )
            {
                dem_deinit();
                return false;
            }
        }
    }

    inited = true;
    return true;
}

void dem_deinit(void)
{
    for( int i=0; i<max_Ndems_ij; i++)
    {
        for( int j=0; j<max_Ndems_ij; j++)
        {
            if( dems[i][j] != NULL && dems[i][j] != MAP_FAILED )
            {
                munmap( dems[i][j], mmap_sizes[i][j] );
                dems[i][j] = NULL;
            }
            if( mmap_fd[i][j] > 0 )
            {
                close( mmap_fd[i][j] );
                mmap_fd[i][j] = 0;
            }
        }
    }
    inited = false;
}

// These functions take in coordinates INSIDE THEIR SPECIFIC DEM
int16_t sampleDEMs(int i, int j)
{
    if( !inited )
        return -1;

    int i_dem = (i + renderStartDEMcoords_i) % CELLS_PER_DEG;
    int j_dem = (j + renderStartDEMcoords_j) % CELLS_PER_DEG;
    int DEMfileN = renderStartDEMfileN + ((j + renderStartDEMcoords_j) / CELLS_PER_DEG);
    int DEMfileE = renderStartDEMfileE + ((i + renderStartDEMcoords_i) / CELLS_PER_DEG);

    int demIndex_i = DEMfileE - renderStartDEMfileE;
    int demIndex_j = DEMfileN - renderStartDEMfileN;
    if( demIndex_i < 0 || demIndex_i >= Ndems_i ||
        demIndex_j < 0 || demIndex_j >= Ndems_j )
    {
        return -1;
    }

    const unsigned char* dem = dems[demIndex_i][demIndex_j];

    // The DEMs are organized north to south, so I flip around the j accessor to
    // keep all accesses ordered by increasing lat, lon
    uint32_t p = i_dem + (WDEM-1 - j_dem )*WDEM;
    int16_t  z = (int16_t) ((dem[2*p] << 8) | dem[2*p + 1]);
    return (z < 0) ? 0 : z;
}

// These functions take in coordinates INSIDE THEIR SPECIFIC DEM
float getViewerHeight(int i, int j)
{
    if( !inited )
        return -1.0f;

    float z = -1e20f;

    for( int di=-1; di<=1; di++ )
        for( int dj=-1; dj<=1; dj++ )
        {
            if( i+di >= 0 && i+di < WDEM &&
                j+dj >= 0 && j+dj < WDEM )
                z = fmax(z, (float) sampleDEMs(i+di, j+dj) );
        }

    return z;
}
