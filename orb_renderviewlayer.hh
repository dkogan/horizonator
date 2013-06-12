#pragma once

#include <string>
#include "orb_viewport.hpp"
#include "orb_layer.hpp"

class orb_renderviewlayer : public orb_layer
{
public:
  orb_renderviewlayer();

  void draw(const orb_viewport &viewport);

private:
  float lat, lon;
  float left, right; // left and right edges of the viewable panorama; in
                     // degrees

public:

  void setlatlon( float _lat, float _lon )
  {
    lat = _lat;
    lon = _lon;
  }

  // return true if the new view is really new, and should be drawn
  bool setview( float _left, float _right );
};
