/* -*- c -*- */

#version 420

layout (location = 0) in vec3 vertex;

// We receive these from the CPU code
uniform float viewer_cell_i, viewer_cell_j;
uniform float viewer_z;
uniform float DEG_PER_CELL;
uniform float sin_viewer_lat, cos_viewer_lat;
uniform float aspect;
uniform float az_deg0, az_deg1;

// We send these to the fragment shader
out vec3 rgb;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;

// these define the front and back clipping planes, in meters
const float znear = 100.0;
const float zfar  = 40000.0;

// Past this distance the render color doesn't change, in meters
const float zfar_color = 20000.0;

// Unwraps an angle x to lie within pi of an angle near. All angles in radians
float unwrap_near_rad(float x, float near)
{
    float d = (x - near) / (2.*pi);
    return (d - round(d)) * 2.*pi + near;
}

void main(void)
{
    /*
      I do this in the tangent plane, ignoring the spherical (and even
      ellipsoidal) nature of the Earth. It is close-enough. Python script to
      confirm:

        import numpy as np

        d = 20000.
        R = 6371000.0

        th = d/R
        s  = np.sin(th)
        c  = np.cos(th)

        x_plane  = np.array((d,R))
        x_sphere = np.array((s,c))*R

        print(x_plane - x_sphere)

      says: [ 0.03284909 31.39222034]. So at 20km out, this planar assumption
      produces 30m of error, primarily in the vertical direction. This
      admittedly is 1.5mrad. Which is marginally too much. But 20km is quite a
      lot. At 10km the error is 7.8m, which is 0.78mrad. I should do something
      better, but in the near-term this is more than good-enough.
     */

    float distance_ne;

    // Several different paths exist for the data processing, with different
    // amounts of the math done in the CPU or GPU. The corresponding path must
    // be selected in the CPU code in init.c
    if(false)
    {
        distance_ne = vertex.z;
        gl_Position = vec4( vertex.x,
                            vertex.y * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else if(false)
    {
        distance_ne = length(vertex.xy);
        gl_Position = vec4( atan(vertex.x, vertex.y) / pi,
                            atan(vertex.z, distance_ne) / pi * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else
    {
        float i = vertex.x;
        float j = vertex.y;

        vec2 en =
            vec2( (i - viewer_cell_i) * DEG_PER_CELL * Rearth * pi/180. * cos_viewer_lat,
                  (j - viewer_cell_j) * DEG_PER_CELL * Rearth * pi/180. );

        distance_ne = length(en);
        float az_rad = atan(en.x, en.y);

        // az = 0:     North
        // az = 90deg: East

        float az_rad0 = radians(az_deg0);
        float az_rad1 = radians(az_deg1);

        // az_rad1 should be within 2pi of az_rad0 and az_rad1 > az_rad0
        az_rad1 = unwrap_near_rad(az_rad1-az_rad0, pi) + az_rad0;

        // in [0,2pi]
        float az_rad_center = (az_rad0 + az_rad1)/2.;

        az_rad = unwrap_near_rad(az_rad, az_rad_center);

        float az_ndc_per_rad = 2.0 / (az_rad1 - az_rad0);

        float az_ndc = (az_rad - az_rad_center) * az_ndc_per_rad;
        float el_ndc = atan((vertex.z - viewer_z), distance_ne) * aspect * az_ndc_per_rad;
        gl_Position = vec4( az_ndc, el_ndc,
                            ((distance_ne - znear) / (zfar - znear) * 2. - 1.),
                            1.0 );
    }

    rgb.r = (distance_ne - znear) / (zfar_color - znear);
    rgb.g = 0.;
    rgb.b = 0.;
}
