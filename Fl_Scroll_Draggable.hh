#ifndef FL_SCROLL_DRAGGABLE_HH
#define FL_SCROLL_DRAGGABLE_HH

#include <FL/Fl_Scroll.H>
#include "cvFltkWidget.hh"
#include "orb_renderviewlayer.hh"
#include "orb_mapctrl.hpp"

class Fl_Scroll_Draggable : public Fl_Scroll
{
  int                  last_x, last_y;
  const CvFltkWidget*  render;
  orb_renderviewlayer* renderviewlayer;
  orb_mapctrl*         mapctrl;

public:

  Fl_Scroll_Draggable( int x, int y, int w, int h,
                       orb_renderviewlayer* _renderviewlayer,
                       orb_mapctrl*         _mapctrl )
    : Fl_Scroll(x,y,w,h,NULL),
      last_x(-1),
      render(NULL),
      renderviewlayer(_renderviewlayer),
      mapctrl(_mapctrl)
  {
  }

  int handle(int event);
};

#endif
