/* -*- c -*- */

#version 110

varying float channel_distance, channel_elevation, channel_griddist;

void main(void)
{
    gl_FragColor = vec4( channel_distance,
                         channel_elevation,
                         0.0,
                         1.0);
}
