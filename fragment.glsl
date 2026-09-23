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

// Real land-cover class (see landcover.h), from pre-baked ESA WorldCover /
// IGN OCS GE / CORINE Land Cover tiles (build-landcover-tiles.py), sampled
// per vertex on the CPU and passed through unblended (see the flat
// qualifier in geometry.glsl). 0 means no real data was available at this
// point: falls back to the procedural elevation/slope classification below
flat in float landcover_class_fragment;

// 0 at znear_color, 1 at zfar_color (see vertex.glsl)
in float atmo_t_fragment;

const float SNOW_LINE_M   = 2800.0; // above this: snow, any slope
const float TREE_LINE_M   = 1800.0; // below (and not snow/rock): forest
const float SLOPE_ROCK_NZ = 0.55;   // normal.z under this: bare rock

const vec3 COLOR_FOREST = vec3(0.35, 0.42, 0.30);
const vec3 COLOR_GRASS  = vec3(0.55, 0.58, 0.38);
const vec3 COLOR_ROCK   = vec3(0.50, 0.47, 0.43);
const vec3 COLOR_SNOW   = vec3(0.95, 0.96, 0.98);
const vec3 COLOR_WATER  = vec3(0.28, 0.38, 0.45);

// Finer nuances only IGN OCS GE (not WorldCover) can tell apart: cooler,
// darker green for conifers; warmer, lighter green for deciduous; alpine
// heath/scrub (also WorldCover's Shrubland) between grass and rock
const vec3 COLOR_FOREST_CONIFER   = vec3(0.28, 0.37, 0.27);
const vec3 COLOR_FOREST_DECIDUOUS = vec3(0.42, 0.48, 0.28);
const vec3 COLOR_SHRUB            = vec3(0.50, 0.52, 0.32);

// Must match horizonator_landcover_class_t in landcover.h
#define LANDCOVER_UNKNOWN            0.0
#define LANDCOVER_FOREST             1.0
#define LANDCOVER_GRASS              2.0
#define LANDCOVER_ROCK               3.0
#define LANDCOVER_SNOWICE            4.0
#define LANDCOVER_WATER              5.0
#define LANDCOVER_FOREST_DECIDUOUS   6.0
#define LANDCOVER_FOREST_CONIFER     7.0
#define LANDCOVER_SHRUB              8.0

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

// Purely procedural fallback, used wherever no real land-cover data was
// baked in for this point (landcover_class_fragment == LANDCOVER_UNKNOWN):
// no aerial imagery or land-cover data involved, just elevation (snow
// line) and slope steepness (bare rock), with forest below the tree line
// and alpine grass above it otherwise
vec3 material_color_procedural(float elevation, float slope_nz)
{
    if(elevation > SNOW_LINE_M)
        return COLOR_SNOW;
    if(slope_nz < SLOPE_ROCK_NZ)
        return COLOR_ROCK;
    if(elevation > TREE_LINE_M)
        return COLOR_GRASS;
    return COLOR_FOREST;
}

// landcover_class is a float holding one of the LANDCOVER_* integer codes
// above (from a GL_UNSIGNED_BYTE vertex attribute, so it arrives here as an
// exact integral value, not needing to be rounded). LANDCOVER_UNKNOWN falls
// back to the procedural elevation/slope classification: real data is
// preferred wherever build-landcover-tiles.py has prepared it, but a point
// outside that coverage should still get an approximate material, not the
// flat legacy gray
//
// A real, planimetric land-cover source (OCS GE/WorldCover, both derived
// from an overhead view) can't be trusted on a near-vertical slope: the
// DEM itself can only represent a cliff as a steep ramp spanning several
// cells, never a true vertical face, so the sample taken there can land
// on the horizontal footprint of neighboring vegetated terrain instead of
// the cliff's actual rock. This shows up as green cliffs, and is far more
// visible in a grazing panorama view than it would be looking straight
// down at a map. So: same slope_nz<SLOPE_ROCK_NZ rule as the procedural
// fallback below, applied here too, but only to the vegetation classes --
// LANDCOVER_ROCK/SNOWICE/WATER stay as classified, since a steep slope is
// unremarkable for bare rock and can be entirely legitimate for a couloir/
// icefall or a cliff behind a lake
vec3 material_color(float landcover_class, float elevation, float slope_nz)
{
    bool steep = slope_nz < SLOPE_ROCK_NZ;
    if(landcover_class == LANDCOVER_FOREST)           return steep ? COLOR_ROCK : COLOR_FOREST;
    if(landcover_class == LANDCOVER_GRASS)            return steep ? COLOR_ROCK : COLOR_GRASS;
    if(landcover_class == LANDCOVER_ROCK)             return COLOR_ROCK;
    if(landcover_class == LANDCOVER_SNOWICE)          return COLOR_SNOW;
    if(landcover_class == LANDCOVER_WATER)            return COLOR_WATER;
    if(landcover_class == LANDCOVER_FOREST_DECIDUOUS) return steep ? COLOR_ROCK : COLOR_FOREST_DECIDUOUS;
    if(landcover_class == LANDCOVER_FOREST_CONIFER)   return steep ? COLOR_ROCK : COLOR_FOREST_CONIFER;
    if(landcover_class == LANDCOVER_SHRUB)            return steep ? COLOR_ROCK : COLOR_SHRUB;
    return material_color_procedural(elevation, slope_nz);
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
                          material_color(landcover_class_fragment, elevation_fragment, n.z),
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
