/* -*- c -*- */

#version 420

layout(location = 0) out vec4 frag_color;
in vec3 rgb_fragment;
in vec2 tex_fragment;
uniform sampler2D tex;

uniform int NtilesX, NtilesY;

// shading_scale is 0.0 (disabled: legacy unshaded rendering) or 1.0
// (enabled). sun_dir is a unit vector in the same (east,north,height)
// frame as normal_fragment, pointing FROM the terrain TOWARDS the sun
uniform float shading_scale;
uniform vec3  sun_dir;
in vec3 normal_fragment;


void main(void)
{
    // normal_fragment is linearly interpolated (by the rasterizer) from
    // the 3 per-vertex normals of the enclosing triangle, so it isn't
    // unit length in general; renormalize before using it
    float ndotl = max(dot(normalize(normal_fragment), sun_dir), 0.0);
    // Ambient floor of 0.5: a slope facing away from the sun is dimmer,
    // not black
    float light = mix(0.5, 1.0, ndotl);
    // shading_scale==0 must reproduce the old, unshaded look exactly
    float light_scaled = mix(1.0, light, shading_scale);

    vec3 rgb_shaded = rgb_fragment * light_scaled;

    if(NtilesX == 0)
        frag_color = vec4(rgb_shaded, 1.0);
    else
    {
        vec4 texcolor     = texture( tex, tex_fragment.xy);
        vec4 shadingcolor = vec4(rgb_shaded, 0.0);
        frag_color = 0.7*texcolor + 0.3*shadingcolor;
    }
}
