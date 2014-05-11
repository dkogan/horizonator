/* -*- c -*- */

#version 110

varying float channel_dist, channel_elevation;
void main(void)
{
    gl_FragColor = vec4(channel_dist,
                        channel_elevation,
                        0.0, 0.0);
}
