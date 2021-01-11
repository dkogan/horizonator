#pragma once

#include <stdbool.h>

// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* render_to_image(// output
                      int* image_width, int* image_height,

                      // input
                      float view_lat, float view_lon,
                      int width, float fovy_deg // render parameters. negative to take defaults
                      );

bool render_to_window( float view_lat, float view_lon );
