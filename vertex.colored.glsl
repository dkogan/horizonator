/* -*- c -*- */

#version 110

// We receive these from the CPU code
uniform float viewer_cell_i, viewer_cell_j;
uniform float viewer_z;
uniform float DEG_PER_CELL;
uniform float view_lon, view_lat;
uniform float sin_viewer_lat, cos_viewer_lat;
uniform float aspect;

// We send these to the fragment shader
varying float channel_distance, channel_elevation, channel_griddist;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;

// these define the front and back clipping planes, in meters
const float znear = 100.0;
const float zfar  = 20000.0;

// Past this distance the render color doesn't change, in meters
const float zfar_color = 40000.0;

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
        distance_ne = gl_Vertex.z;
        gl_Position = vec4( gl_Vertex.x,
                            gl_Vertex.y * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else if(false)
    {
        distance_ne = length(gl_Vertex.xy);
        gl_Position = vec4( atan(gl_Vertex.x, gl_Vertex.y) / pi,
                            atan(gl_Vertex.z, distance_ne) / pi * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else
    {
        vec2 en =
            vec2( (gl_Vertex.x - viewer_cell_i) * DEG_PER_CELL * Rearth * pi/180. * cos_viewer_lat,
                  (gl_Vertex.y - viewer_cell_j) * DEG_PER_CELL * Rearth * pi/180. );

        distance_ne = length(en);
        gl_Position = vec4( atan(en.x, en.y) / pi,
                            atan(gl_Vertex.z - viewer_z, distance_ne) / pi * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }

    channel_distance  = distance_ne / zfar;
    channel_griddist  = 0.0;

}
