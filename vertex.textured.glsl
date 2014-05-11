/* -*- c -*- */

#version 110

uniform float view_z;
uniform float renderStartN, renderStartE;
uniform float DEG_PER_CELL;
uniform float view_lon, view_lat;
uniform int   view_i, view_j;
uniform float sin_view_lat, cos_view_lat;

uniform float aspect;
varying float channel_dist, channel_elevation;

uniform float TEXTUREMAP_LON0;
uniform float TEXTUREMAP_LON1;
uniform float TEXTUREMAP_LAT0;
uniform float TEXTUREMAP_LAT1;
uniform float TEXTUREMAP_LAT2;
uniform int NtilesX, NtilesY;
uniform int start_osmTileX;
uniform int start_osmTileY;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;

// these define the front and back clipping planes, in meters
const float znear  = 10.0, zfar = 300000.0;

// Past this distance the render color doesn't change, in meters
const float zfar_color = 40000.0;


// OSM tiles (and everybody else's tool) use the spherical mercator
// projection to map corners of each tile to lat/lon coords. Inside each
// tile, the pixel coords are linear with lat/lon.
//
// Spherical mercator is linear in the longitude direction, so there's
// nothing interesting here. It is NOT linear in the latitude direction. I
// estimate it with 2nd-order interpolation. This is close-enough for my
// purposes
float get_xtexture( float lon )
{
    float x_texture = TEXTUREMAP_LON1 * lon + TEXTUREMAP_LON0;
    return (x_texture - float(start_osmTileX)) / float(NtilesX);
}
float get_ytexture( float dlat )
{
    float y_texture = dlat * (dlat*TEXTUREMAP_LAT2 + TEXTUREMAP_LAT1) + TEXTUREMAP_LAT0;
    return (y_texture - float(start_osmTileY)) / float(NtilesY);
}

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
      gl_TexCoord[0].xy = vec2(get_xtexture(view_lon), get_ytexture(0.0));
    }
    else
    {
      gl_Position = vec4( +1.0, -1.0,
                          -1.0, 1.0 );
      gl_TexCoord[0].xy = vec2(get_xtexture(view_lon), get_ytexture(0.0));
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

    float lon  = radians( float(renderStartE) + vin.x * DEG_PER_CELL );
    float lat  = radians( float(renderStartN) + vin.y * DEG_PER_CELL );

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

    float dlat = lat - view_lat;

    float x_texture = get_xtexture( lon );
    float y_texture = get_ytexture( dlat );
    gl_TexCoord[0].xy = vec2(x_texture, y_texture);

    // Here I compute 4 sin/cos. Previously I was sending sincos( view_lat/lon) as
    // a uniform, so no trig was needed here. I think this may have been causing
    // roundoff issues, so I'm not doing that anymore. Specifically, sin(+eps) was
    // being slightly negative
    float sin_dlat = sin( dlat );
    float cos_dlat = cos( dlat );
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
    channel_elevation = vin.z / 3500.0;  // ... elevation

    const float A = (zfar + znear) / (zfar - znear);
    gl_Position = vec4( az * zeff,
                        el * zeff * aspect,
                        mix(zfar, zeff, A),
                        zeff );
  }
}
