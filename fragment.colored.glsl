/* -*- c -*- */

#version 420

layout(location = 0) out vec4 frag_color;
in vec3 rgb_fragment;

void main(void)
{
    frag_color = vec4(rgb_fragment, 1.0);
}

