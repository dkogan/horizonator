#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "points_of_interest.h"

static struct poi_t pois[] = {
#include "features_generated.h"
};

#define MAX_MARKER_DIST 25000.0
#define MIN_MARKER_DIST 50.0

const float Rearth = 6371000.0;

static int  N_active_pois;
static int* active_pois = NULL;

static float lat0, lon0, cos_lat0_sq;

static bool within( int ipoi )
{
  float arclen( float lat1, float lon1 )
  {
    // On the surface of the earth the arclen is dtheta*Rearth
    //
    // Given v0,v1, |dth| ~ |sin(dth)| = | v0 x v1 |
    //
    // v = [ cos(lat) cos(lon)
    //       cos(lat) sin(lon)
    //       sin(lat) ]
    //
    // |v0 x v1| ~ sqrt( cos(lat0)^2 cos(lat1)^2 dlon^2 + dlat^2 )

    float cos_lat1_sq = cosf(lat1);
    cos_lat1_sq *= cos_lat1_sq;

    float dlat = lat1 - lat0;
    float dlon = lon1 - lon0;

    return Rearth * sqrtf( dlon*dlon * cos_lat0_sq * cos_lat1_sq +
                           dlat*dlat );
  }


  float len = arclen( pois[ipoi].lat, pois[ipoi].lon );
  return MIN_MARKER_DIST < len && len < MAX_MARKER_DIST;
}

static void addToActive( int ipoi )
{
  active_pois[N_active_pois++] = ipoi;
}

void initPOIs( float lat, float lon )
{
  // alloc an upper-bound amount of memory. It's probably too much, but I know
  // we'll never run out
  int Npois = sizeof(pois) / sizeof(pois[0]);

  if( active_pois == NULL )
    active_pois = malloc(Npois * sizeof(active_pois[0]) );

  lat0         = lat;
  lon0         = lon;
  cos_lat0_sq  = cosf( lon );
  cos_lat0_sq *= cos_lat0_sq;

  N_active_pois = 0;

  for( int i=0; i<Npois; i++ )
    if( within( i ) )
      addToActive( i );
}

// caller may modify the indices and the poi internals
void getPOIs( int** _indices, int* _N,
              struct poi_t** _pois)
{
  *_indices = active_pois;
  *_N       = N_active_pois;
  *_pois    = pois;
}

