#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dem.h"

typedef struct
{
    int Ntriangles;
    bool render_texture;

    // These are int32_t, but should be GLint. I don't want to #include <GL.h>.
    // I will static_assert() this in the .c to make sure they are compatible
    int32_t uniform_aspect, uniform_az_deg0, uniform_az_deg1;
    int32_t uniform_viewer_cell_i;
    int32_t uniform_viewer_cell_j;
    int32_t uniform_viewer_z;
    int32_t uniform_viewer_lat;
    int32_t uniform_cos_viewer_lat;
    int32_t uniform_texturemap_lon0;
    int32_t uniform_texturemap_lon1;
    int32_t uniform_texturemap_dlat0;
    int32_t uniform_texturemap_dlat1;
    int32_t uniform_texturemap_dlat2;

    uint32_t program;

    float viewer_lat, viewer_lon;

    horizonator_dem_context_t dems;
} horizonator_context_t;


__attribute__((unused))
static bool horizonator_context_isvalid(const horizonator_context_t* ctx)
{
    return ctx->Ntriangles > 0;
}

// If using GLUT, call this as part of the initialization sequence
bool horizonator_init0_glut(bool double_buffered);

// Call this after horizonator_init0_glut() to initialize. If not using GLUT,
// this is the only init function. We load the DEMs around the viewer. The
// viewer ends up at the center of the DEMs. They can then be moved around
// within the same data by calling horizonator_move_viewer_keep_data()
bool horizonator_init1( // output
                       horizonator_context_t* ctx,

                       // input
                       bool render_texture,

                       float viewer_lat, float viewer_lon,

                       const char* dir_dems,
                       const char* dir_tiles,
                       bool allow_downloads);

void horizonator_resized(const horizonator_context_t* ctx, int width, int height);

// Must be called at least once before horizonator_redraw()
bool horizonator_pan_zoom(const horizonator_context_t* ctx,
                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1);

// Called after horizonator_init1(). Moves the viewer around in the space of
// loaded DEMs. If the viewer moves a LOT, new DEMs should be loaded, and this
// function is no longer appropriate
void horizonator_move_viewer_keep_data(horizonator_context_t* ctx,
                                       float viewer_lat, float viewer_lon);

void horizonator_redraw(const horizonator_context_t* ctx);

// returns true if an intersection is found
bool horizonator_pick(const horizonator_context_t* ctx,

                      // output
                      float* lat, float* lon,

                      // input
                      // pixel coordinates in the render
                      int x, int y );

/////////////// The horizonator_allinone_...() functions are to be used
/////////////// standalone. No other init functions should be called

// returns true on success. The image and ranges buffers must be large-enough to
// contain packed 24-bits-per-pixel BGR data and 32-bit floats respectively. The
// images are returned using the OpenGL convention: bottom row is stored first.
// This is opposite of the usual image convention: top row is first.
// Invisible points have ranges <0
bool horizonator_allinone_render_to_image(// output
                                          // either may be NULL
                                          char* image, float* ranges,
                                          bool render_texture,

                                          // inputs
                                          float viewer_lat, float viewer_lon,

                                          // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                                          // edges lie at the edges of the image. So for an image that's
                                          // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                                          // elevation extents will be chosen to keep the aspect ratio
                                          // square.
                                          float az_deg0, float az_deg1,

                                          int width, int height,
                                          const char* dir_dems,
                                          const char* dir_tiles,
                                          bool allow_downloads);

bool horizonator_allinone_glut_loop( bool render_texture,
                                     float viewer_lat, float viewer_lon,

                                     // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                                     // edges lie at the edges of the image. So for an image that's
                                     // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                                     // elevation extents will be chosen to keep the aspect ratio
                                     // square.
                                     float az_deg0, float az_deg1,
                                     const char* dir_dems,
                                     const char* dir_tiles,
                                     bool allow_downloads);
