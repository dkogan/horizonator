#pragma once

#include <stdbool.h>
#include <opencv2/core/core_c.h>

#ifdef __cplusplus
extern "C"
{
#endif

// returns an image. The caller is responsible for freeing the memory with
// cvReleaseImage. Returns NULL on error
IplImage* render_terrain( float view_lat, float view_lon, float* elevation,
                          int width, float fovy_deg, // render parameters. negative to take defaults
                          bool do_bgr );
bool render_terrain_to_window( float view_lat, float view_lon );


#ifdef __cplusplus
}
#endif
