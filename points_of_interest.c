#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "points_of_interest.h"

struct poi_t pois[] = {
#include "features_generated.h"
};

#define MIN_MARKER_DIST 20000.0 /* 20km max dist */
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



  return arclen( pois[ipoi].lat, pois[ipoi].lon ) < MIN_MARKER_DIST;
}

static void addToActive( int ipoi )
{
  active_pois[N_active_pois++] = ipoi;
}

void initPOIs( float lat, float lon )
{
  if( active_pois )
    free(active_pois);

  lat0         = lat;
  lon0         = lon;
  cos_lat0_sq  = cosf( lon );
  cos_lat0_sq *= cos_lat0_sq;

  // alloc an upper-bound amount of memory. It's probably too much, but I know
  // we'll never run out
  int N_pois = sizeof(pois) / sizeof(pois[0]);
  active_pois = malloc(Npois * sizeof(active_pois[0]) );
  N_active_pois = 0;

  for( int i=0; i<Npois; i++ )
    if( within( int i ) )
      addToActive( i );
}

const struct poi_i* getPOI( int idx )
{
  if( idx < 0 || idx >= N_active_pois )
    return NULL;

  return &pois[ active_pois[idx] ];
}

