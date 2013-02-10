/* -*- c -*- */

/* Makefile includes vertex_header.glsl here */


void main(void)
{
  /* gl_Vertex is (i,j,height) */
  bool at_left_seam  = false;
  bool at_right_seam = false;
  vec3 vin = gl_Vertex.xyz;
  if( vin.x < 0.0 )
  {
    vin.x += float(2*WDEM);
    at_left_seam = true;
  }
  else if( vin.y < 0.0 )
  {
    vin.y += float(2*WDEM);
    at_right_seam = true;
  }

  float lon = radians( float(-demfileW)     + vin.x/float(WDEM-1) );
  float lat = radians( float( demfileN + 1) - vin.y/float(WDEM-1) );

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


  vec3 v = vec3( (Rearth + vin.z) * ( cos_lat * sin_dlon ),
                 (Rearth + vin.z) * ( cos_dlat*cos_dlon + sin_lat*sin_view_lat*(1.0 - cos_dlon) ),
                 (Rearth + vin.z) * ( sin_dlat*cos_dlon + sin_lat*cos_view_lat*(1.0 - cos_dlon)) );
  /* this is bad for roundoff error */
  v.y -= Rearth + view_z;

  float zeff  = length(vec2(v.x, v.z));
  float angle = atan(v.x, v.z) / pi;
  if     ( at_left_seam )  angle -= 2.0;
  else if( at_right_seam ) angle += 2.0;

  red = zeff / 10000.0;

  const float A = (zfar + znear) / (zfar - znear);
  gl_Position = vec4( angle * zeff,
                      v.y / pi * aspect,
                      mix(zfar, zeff, A),
                      zeff );
}
