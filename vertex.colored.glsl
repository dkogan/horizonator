/* -*- c -*- */

#version 110

uniform float view_z;
uniform float renderStartN, renderStartE;
uniform float DEG_PER_CELL;
uniform float view_lon, view_lat;
uniform int   view_i, view_j;
uniform float sin_view_lat, cos_view_lat;
uniform float aspect;
varying float channel_dist, channel_elevation, channel_griddist;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;

// these define the front and back clipping planes, in meters
const float znear  = 10.0, zfar = 300000.0;

// Past this distance the render color doesn't change, in meters
const float zfar_color = 40000.0;


void main(void)
{
  /* gl_Vertex is (i,j,height) */
  bool at_left_seam  = false;
  bool at_right_seam = false;
  vec3 vin = gl_Vertex.xyz;

  // first I check for my hard-coded coords
  if( vin.x < 0.0 && vin.y < 0.0 )
  {
    // x and y <0 means this is either the bottom-left of screen or bottom-right
    // of screen. The choice between these two is the sign of vin.z
    if( vin.z < 0.0 )
    {
      gl_Position = vec4( -1.0, -1.0,
                          -1.0, 1.0 ); // is this right? not at all sure the
                                       // last 2 args are correct
    }
    else
    {
      gl_Position = vec4( +1.0, -1.0,
                          -1.0, 1.0 );
    }
    channel_dist      = 0.0;
    channel_elevation = 0.5;
  }
  else
  {
    if( vin.x < 0.0 )
    {
      vin.x *= -1.0;
      at_left_seam = true;
    }
    else if( vin.y < 0.0 )
    {
      vin.y = -vin.y - 1.0; // extra 1 because I can't assume that -0 < 0
      at_right_seam = true;
    }

    float lon = radians( float(renderStartE) + vin.x * DEG_PER_CELL );
    float lat = radians( float(renderStartN) + vin.y * DEG_PER_CELL );

    // If this point is of a cell directly adjacent to the viewer, I move it to
    // actually lie next to the viewer. This makes this point less visible,
    // which is what I want, since nearly points are generally observed to be
    // very large and the low spatial resolution is very acutely visible
    if( (int(vin.x) == view_i || int(vin.x) == view_i+1) &&
        (int(vin.y) == view_j || int(vin.y) == view_j+1) )
    {
        lat += 0.9 * (view_lat - lat);
        lon += 0.9 * (view_lon - lon);
    }

    // Here I compute 4 sin/cos. Previously I was sending sincos( view_lat/lon) as
    // a uniform, so no trig was needed here. I think this may have been causing
    // roundoff issues, so I'm not doing that anymore. Specifically, sin(+eps) was
    // being slightly negative
    float sin_dlat = sin( lat - view_lat );
    float cos_dlat = cos( lat - view_lat );
    float sin_dlon = sin( lon - view_lon );
    float cos_dlon = cos( lon - view_lon );

    float sin_lat  = sin( lat );
    float cos_lat  = cos( lat );

    // Convert current point being rendered into the coordinate system centered on
    // the viewpoint. The axes are (east,north,height). I implicitly divide all 3
    // by the height of the observation point
    float east   = cos_lat * sin_dlon;
    float north  = sin_dlat*cos_dlon + sin_lat*cos_view_lat*(1.0 - cos_dlon);
    float height = cos_dlat*cos_dlon + sin_lat*sin_view_lat*(1.0 - cos_dlon)
      - (Rearth + view_z) / (Rearth + vin.z);

    float len_ne = length(vec2(east, north ));
    float zeff  = (Rearth + vin.z)*len_ne;
    float az    = atan(east, north) / pi;
    float el    = atan( height, len_ne ) / pi;

    if     ( at_left_seam )  az -= 2.0;
    else if( at_right_seam ) az += 2.0;

    // coloring by...
    channel_dist = clamp( (zfar_color - zeff) / (zfar_color - znear ),
                          0.0, 1.0 ); // ... distance from camera
    channel_elevation = vin.z;        // ... elevation
    channel_griddist = length(vec2(lon - view_lon, lat - view_lat));

    const float A = (zfar + znear) / (zfar - znear);
    gl_Position = vec4( az * zeff,
                        el * zeff * aspect,
                        mix(zfar, zeff, A),
                        zeff );
  }
}
