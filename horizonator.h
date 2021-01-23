#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    int Ntriangles;
    bool render_texture;

    // These are GLint. I don't want to #include <GL.h>. I will static_assert()
    // this in the .c to make sure they are compatible
    int32_t uniform_aspect, uniform_az_deg0, uniform_az_deg1;
} horizonator_context_t;


bool horizonator_init0_glut(bool double_buffered);
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
bool horizonator_zoom(const horizonator_context_t* ctx,
                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1);

void horizonator_redraw(const horizonator_context_t* ctx);


// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* horizonator_allinone_render_to_image(bool render_texture,
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
