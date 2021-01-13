/* -*- c -*- */

#version 110

varying vec3 rgb;

void main(void)
{
    gl_FragColor = vec4(rgb, 1.0);
}
