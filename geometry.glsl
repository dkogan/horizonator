/* -*- c -*- */

#version 420

layout (triangles) in;
layout (triangle_strip, max_vertices=3) out;

// 0 at znear_color, 1 at zfar_color -- how far towards the atmospheric
// haze color this point should be blended (see COLOR_NEAR_DEFAULT in
// fragment.glsl for why that blend happens there, not here)
in  float atmo_t[];
out float atmo_t_fragment;
in  vec2 tex[];
out vec2 tex_fragment;

// Per-vertex normal (world-space east/north/height frame), from
// vertex.glsl. Just passed through here, per vertex, so the fragment
// shader gets a value smoothly interpolated by the rasterizer across each
// triangle -- and so continuous across the edge between two triangles
// that share a vertex, unlike a flat per-triangle normal
in  vec3 normal[];
out vec3 normal_fragment;

// Raw DEM elevation, for the procedural material classification in
// fragment.glsl
in  float elevation_m[];
out float elevation_fragment;

void main()
{
    // The azimuth is gl_Position.x. Any triangles on the seam (some vertices
    // off on the left, and some off on the right) need to be thrown out. Those
    // triangle will have max-az > 1 and min-az < -1 for max-min > 2. But I can
    // be even more general. Any triangles that have max-min > 0.5 span more
    // that 1/4 of the width of the viewport. This is never what we want, so I
    // throw those out too.
    if( max(max(gl_in[0].gl_Position.x,
                gl_in[1].gl_Position.x),
            gl_in[2].gl_Position.x) -
        min(min(gl_in[0].gl_Position.x,
                gl_in[1].gl_Position.x),
            gl_in[2].gl_Position.x) > 0.5 )
        return;

    for(int i=0; i<3; i++)
    {
        atmo_t_fragment    = atmo_t[i];
        tex_fragment       = tex[i];
        normal_fragment    = normal[i];
        elevation_fragment = elevation_m[i];
        gl_Position        = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
