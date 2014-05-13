/* -*- c -*- */

#version 110

varying float channel_dist, channel_elevation, channel_griddist;
void main(void)
{
    gl_FragColor = vec4(channel_dist,
                        channel_elevation / 3500.0,
                        degrees(channel_griddist),
                        0.0);
}
