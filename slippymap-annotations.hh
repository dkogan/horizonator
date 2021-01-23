#pragma once

#include <string>
#include <stdbool.h>
#include "orb_viewport.hpp"
#include "orb_layer.hpp"

// A class to markup the slippy map. It draws
//
// - The view point
// - The bounds of the azimuth being viewed
// - The point picked by the user from the render


// this or less means "no pick". Duplicated in .cc
#define MIN_VALID_ANGLE -1000.0f

struct view_t
{
    float az_center_deg;
    float az_radius_deg;
    float lat;
    float lon;
};

class SlippymapAnnotations : public orb_layer
{
    const view_t* view;
    float pick_lat, pick_lon;

public:

    SlippymapAnnotations(const view_t* _view)
        : view(_view),
          pick_lat(MIN_VALID_ANGLE),
          pick_lon(MIN_VALID_ANGLE)
    {
        name(std::string("Slippy-map annotations"));
    }

    void set_pick( float lon, float lat )
    {
        pick_lon = lon;
        pick_lat = lat;
    }

    void unset_pick(void)
    {
        pick_lat = MIN_VALID_ANGLE;
    }

    // The one big-ish function. In the .cc file
    void draw(const orb_viewport &viewport);
};

#undef MIN_VALID_ANGLE
