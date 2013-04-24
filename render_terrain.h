#pragma once

#include <stdbool.h>
#include <opencv2/core/core_c.h>

IplImage* render_terrain( float view_lat, float view_lon );
bool render_terrain_to_window( float view_lat, float view_lon );

