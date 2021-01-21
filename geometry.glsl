/* -*- c -*- */

#version 420

layout (triangles) in;
layout (triangle_strip, max_vertices=3) out;

in  vec3 rgb[];
out vec3 rgb_fragment;
in  vec2 tex[];
out vec2 tex_fragment;

void main()
{
    // The azimuth is gl_Position.x. Any triangles on the seam (some vertices
    // off on the left, and some off on the right) need to be thrown out
    if( (gl_in[0].gl_Position.x >=  1. ||
         gl_in[1].gl_Position.x >=  1. ||
         gl_in[2].gl_Position.x >=  1.) &&
        (gl_in[0].gl_Position.x <= -1. ||
         gl_in[1].gl_Position.x <= -1. ||
         gl_in[2].gl_Position.x <= -1.) )
        return;

    for(int i=0; i<3; i++)
    {
        rgb_fragment = rgb[i];
        tex_fragment = tex[i];
        gl_Position  = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
