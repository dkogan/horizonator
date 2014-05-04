/* -*- c -*- */

#version 110

uniform sampler2D tex;
varying float red, green;
void main(void)
{
    vec4 texcolor     = texture2D( tex, gl_TexCoord[0].xy);
    vec4 shadingcolor = vec4(red, green ,0.0, 0.0);

    gl_FragColor = 0.7*texcolor + 0.3*shadingcolor;
}
