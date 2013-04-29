#ifndef FLTK_SCROLL_WHEELABLE_HH
#define FLTK_SCROLL_WHEELABLE_HH

#include <FL/Fl_Scroll.H>

class Fl_Scroll_Wheelable : public Fl_Scroll
{
public:
  Fl_Scroll_Wheelable( int x, int y, int w, int h )
    : Fl_Scroll( x,y,w,h )
  {
  }

  // called by FLTK to alert this widget about an event. An upper level callback can be triggered
  // here
  int handle(int event)
  {
    // I handle FL_LEAVE even if the mouse isn't in the window right now because I want to make
    // absolutely sure I don't miss this event. Missing it could mean wrongly latching in the
    // mouse-over-widget state
    if(event == FL_MOUSEWHEEL)
    {
      scroll_to( xposition() + 5*Fl::event_dx(),
                 yposition() + 5*Fl::event_dy() );

      return 1;
    }

    return Fl_Scroll::handle(event);
  }
};

#endif
