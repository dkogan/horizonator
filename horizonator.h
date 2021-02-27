#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dem.h"

typedef struct
{
    int Ntriangles;
    bool render_texture, use_glut;

    // meaningful only if use_glut. 0 means "invalid" or "closed"
    int glut_window;

    // These should be GLint, but I don't want to #include <GL.h>.
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
    int32_t uniform_znear, uniform_zfar;
    int32_t uniform_znear_color, uniform_zfar_color;

    uint32_t program;

    float viewer_lat, viewer_lon;

    horizonator_dem_context_t dems;

    struct
    {
        bool inited;
        // These should be GLuint, but I don't want to #include <GL.h>.
        // I will static_assert() this in the .c to make sure they are compatible
        uint32_t frameBufID;
        uint32_t renderBufID;
        uint32_t depthBufID;

        int width, height;
    } offscreen;
} horizonator_context_t;


__attribute__((unused))
static bool horizonator_context_isvalid(const horizonator_context_t* ctx)
{
    return ctx->Ntriangles > 0;
}

// The main init routine. We support 3 modes:
//
// - GLUT: static window    (use_glut = true, offscreen_width <= 0)
// - GLUT: offscreen render (use_glut = true, offscreen_width > 0)
// - no GLUT: higher-level application (use_glut = false)
//
// This routine loads the DEMs around the viewer (viewer is at the center of the
// DEMs). The render can then be updated by calling any of
// - horizonator_move()
// - horizonator_pan_zoom()
// - horizonator_resized()
// and then
// - horizonator_redraw()
//
// If rendering off-screen, horizonator_resized() is not allowed.
// horizonator_pan_zoom() must be called to update the azimuth extents.
// Completely arbitrarily, these are set to -45deg - 45deg initially
bool horizonator_init( // output
                       horizonator_context_t* ctx,

                       // input
                       float viewer_lat, float viewer_lon,
                       int offscreen_width, int offscreen_height,
                       int render_radius_cells,

                       // rendering and color-coding boundaries. Set to <=0 for
                       // defaults
                       float znear,       float zfar,
                       float znear_color, float zfar_color,

                       bool use_glut,
                       bool render_texture,
                       const char* dir_dems,
                       const char* dir_tiles,
                       bool allow_downloads);

void horizonator_deinit( horizonator_context_t* ctx );

bool horizonator_resized(const horizonator_context_t* ctx, int width, int height);

// Must be called at least once before horizonator_redraw()
bool horizonator_pan_zoom(const horizonator_context_t* ctx,
                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1);

// Called after horizonator_init(). Moves the viewer around in the space of
// loaded DEMs. If the viewer moves a LOT, new DEMs should be loaded, and this
// function is no longer appropriate
bool horizonator_move(horizonator_context_t* ctx,
                      float viewer_lat, float viewer_lon);

// set the position of the clipping planes. The horizontal distance from the
// viewer is compared against these positions. Only points in [znear,zfar] are
// rendered. The render is color-coded by this distance, using znear_color and
// zfar_color as the bounds for the color-coding
//
// Any value <0 is untouched by this call
bool horizonator_set_zextents(horizonator_context_t* ctx,
                              float znear,       float zfar,
                              float znear_color, float zfar_color);

bool horizonator_redraw(const horizonator_context_t* ctx);

// returns true if an intersection is found
bool horizonator_pick(const horizonator_context_t* ctx,

                      // output
                      float* lat, float* lon,

                      // input
                      // pixel coordinates in the render
                      int x, int y );


// Renders a given scene to an RGB image and/or a range image.
// horizonator_init() must have been called first with use_glut=true and
// offscreen_width,height > 0. Then the viewer and camera must have been
// configured with horizonator_move() and horizonator_pan_zoom()
//
// Returns true on success. The image and ranges buffers must be large-enough to
// contain packed 24-bits-per-pixel BGR data and 32-bit floats respectively. The
// images are returned using the usual convention: the top row is stored first.
// This is opposite of the OpenGL convention: bottom row is first. Invisible
// points have ranges <0
bool horizonator_render_offscreen(const horizonator_context_t* ctx,

                                  // output
                                  // either may be NULL
                                  char* image, float* ranges);

/////////////// The horizonator_allinone_...() functions are to be used
/////////////// standalone. No other init functions should be called

bool horizonator_allinone_glut_loop( bool render_texture,
                                     float viewer_lat, float viewer_lon,

                                     // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                                     // edges lie at the edges of the image. So for an image that's
                                     // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                                     // elevation extents will be chosen to keep the aspect ratio
                                     // square.
                                     float az_deg0, float az_deg1,
                                     int render_radius_cells,

                                     // rendering and color-coding boundaries. Set to <=0 for
                                     // defaults
                                     float znear,       float zfar,
                                     float znear_color, float zfar_color,

                                     const char* dir_dems,
                                     const char* dir_tiles,
                                     bool allow_downloads);
