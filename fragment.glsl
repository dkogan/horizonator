/* -*- c -*- */

#version 420

layout(location = 0) out vec4 frag_color;
in vec2 tex_fragment;
uniform sampler2D tex;

uniform int NtilesX, NtilesY;

// shading_scale is 0.0 (disabled: legacy unshaded rendering) or 1.0
// (enabled). sun_dir is a unit vector in the same (east,north,height)
// frame as normal_fragment, pointing FROM the terrain TOWARDS the sun
uniform float shading_scale;
uniform vec3  sun_dir;
in vec3 normal_fragment;

// materials_scale is 0.0 (disabled: no material tint) or 1.0 (enabled).
// A first, purely procedural land-cover approximation -- no aerial
// imagery or land-cover data involved -- classifying each fragment by
// elevation (snow line) and slope steepness (bare rock), with forest
// below the tree line and alpine grass above it otherwise
uniform float materials_scale;
in float elevation_fragment;

// 0 at znear_color, 1 at zfar_color (see vertex.glsl)
in float atmo_t_fragment;

const float SNOW_LINE_M   = 2800.0; // above this: snow, any slope
const float TREE_LINE_M   = 1800.0; // below (and not snow/rock): forest
const float SLOPE_ROCK_NZ = 0.55;   // normal.z under this: bare rock

const vec3 COLOR_FOREST = vec3(0.35, 0.42, 0.30);
const vec3 COLOR_GRASS  = vec3(0.55, 0.58, 0.38);
const vec3 COLOR_ROCK   = vec3(0.50, 0.47, 0.43);
const vec3 COLOR_SNOW   = vec3(0.95, 0.96, 0.98);

// Used as the near (atmo_t==0) color when materials_scale==0: this is
// the plain-grayscale look (no material data), a dark neutral gray purely
// for visual weight/contrast -- not a claim about what's really there.
// When materials ARE on, the material's own true color is used as the
// near color instead (see main() below): a real land-cover color
// shouldn't ALSO be darkened by this legacy gray, or the two darkening
// effects compound (near-dark-gray times a dim material times slope
// shading was crushing near, shaded forest to near black)
const vec3 COLOR_NEAR_DEFAULT = vec3(0.30, 0.30, 0.30);

// The far (atmo_t==1) color, regardless of materials: a pale blue-gray
// haze, the way atmospheric scattering tints distant relief in a real
// photo. This one genuinely is distance-dependent physics (haze), unlike
// COLOR_NEAR_DEFAULT above, so it applies the same whether or not
// materials are on
const vec3 COLOR_FAR = vec3(0.80, 0.84, 0.92);

vec3 material_color(float elevation, float slope_nz)
{
    if(elevation > SNOW_LINE_M)
        return COLOR_SNOW;
    if(slope_nz < SLOPE_ROCK_NZ)
        return COLOR_ROCK;
    if(elevation > TREE_LINE_M)
        return COLOR_GRASS;
    return COLOR_FOREST;
}

void main(void)
{
    // normal_fragment is linearly interpolated (by the rasterizer) from
    // the 3 per-vertex normals of the enclosing triangle, so it isn't
    // unit length in general; renormalize before using it
    vec3 n = normalize(normal_fragment);

    float ndotl = max(dot(n, sun_dir), 0.0);
    // Ambient floor of 0.5: a slope facing away from the sun is dimmer,
    // not black
    float light = mix(0.5, 1.0, ndotl);
    // shading_scale==0 must reproduce the old, unshaded look exactly
    float light_scaled = mix(1.0, light, shading_scale);

    // materials_scale==0 must reproduce the old, untinted look exactly:
    // mix(COLOR_NEAR_DEFAULT, material_color(...), 0.0) == COLOR_NEAR_DEFAULT
    vec3 near_color = mix(COLOR_NEAR_DEFAULT,
                          material_color(elevation_fragment, n.z),
                          materials_scale);

    // Atmospheric haze: blend towards the far color with distance. This
    // is the ONLY distance-dependent darkening/tinting left (on top of
    // whichever near_color was picked above) -- unlike before, materials
    // no longer also get multiplied by a separate near/far gray gradient
    vec3 rgb = mix(near_color, COLOR_FAR, atmo_t_fragment);

    vec3 rgb_shaded = rgb * light_scaled;

    if(NtilesX == 0)
        frag_color = vec4(rgb_shaded, 1.0);
    else
    {
        vec4 texcolor     = texture( tex, tex_fragment.xy);
        vec4 shadingcolor = vec4(rgb_shaded, 0.0);
        frag_color = 0.7*texcolor + 0.3*shadingcolor;
    }
}
