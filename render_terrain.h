#pragma once

#include <stdbool.h>

// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* render_to_image(float viewer_lat, float viewer_lon,

                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1,

                      int width, int height,
                      const char* dir_dems);

bool render_to_window( float viewer_lat, float viewer_lon,

                       // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                       // edges lie at the edges of the image. So for an image that's
                       // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                       // elevation extents will be chosen to keep the aspect ratio
                       // square.
                       float az_deg0, float az_deg1,
                       const char* dir_dems);
