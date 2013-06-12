#ifndef FL_SCROLL_DRAGGABLE_HH
#define FL_SCROLL_DRAGGABLE_HH

#include <FL/Fl_Scroll.H>

class Fl_Scroll_Draggable : public Fl_Scroll
{
  int last_x, last_y;


public:

  Fl_Scroll_Draggable( int x, int y, int w, int h, const char* label = NULL )
    : Fl_Scroll(x,y,w,h,label),
      last_x(-1)
  {
  }

  int handle(int event);
};

#endif
