/* -*- c -*- */

#version 420

layout(location = 0) out vec4 frag_color;
in vec3 rgb_fragment;
in vec2 tex_fragment;
uniform sampler2D tex;

uniform int NtilesX, NtilesY;


void main(void)
{
    if(NtilesX == 0)
        frag_color = vec4(rgb_fragment, 1.0);
    else
    {
        vec4 texcolor     = texture( tex, tex_fragment.xy);
        vec4 shadingcolor = vec4(rgb_fragment, 0.0);
        frag_color = 0.7*texcolor + 0.3*shadingcolor;
    }
}
