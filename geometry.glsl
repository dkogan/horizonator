/* -*- c -*- */

#version 420

layout (triangles) in;
layout (triangle_strip, max_vertices=3) out;

in  vec3 rgb[];
out vec3 rgb_fragment;

void main()
{
    rgb_fragment = rgb[0];

    gl_Position = gl_in[0].gl_Position; EmitVertex();
    gl_Position = gl_in[1].gl_Position; EmitVertex();
    gl_Position = gl_in[2].gl_Position; EmitVertex();
    EndPrimitive();
}
