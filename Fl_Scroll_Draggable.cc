#include <FL/Fl.H>
#include "Fl_Scroll_Draggable.hh"

int Fl_Scroll_Draggable::handle(int event)
{
  int x,y,w,h;
  bbox(x,y,w,h); // inside-scroll-area geometry

  switch( event )
  {
  case FL_PUSH:
    if( Fl::event_x() >= x && Fl::event_x() < x+w &&
        Fl::event_y() >= y && Fl::event_y() < y+h )
    {
      last_x = Fl::event_x();
      last_y = Fl::event_y();

      return 1; // don't pass this event on
    }
    else
      last_x = -1;
    break;

  case FL_RELEASE:
    last_x = -1;
    break;

  case FL_DRAG:
    if( last_x >= 0 )
    {
      int dx = Fl::event_x() - last_x;
      int dy = Fl::event_y() - last_y;


      int targetx = 0;
      int targety = 0;

      if( hscrollbar.visible() )
      {
        targetx = xposition() - dx;

        if( targetx < hscrollbar.minimum() ) targetx = hscrollbar.minimum();
        if( targetx > hscrollbar.maximum() ) targetx = hscrollbar.maximum();
      }

      if( scrollbar.visible() )
      {
        targety = yposition() - dy;

        if( targety < scrollbar.minimum() ) targety = scrollbar.minimum();
        if( targety > scrollbar.maximum() ) targety = scrollbar.maximum();
      }

      scroll_to( targetx, targety );

      last_x += dx;
      last_y += dy;
      return 1; // don't pass this event on
    }

    break;
  }

  return Fl_Scroll::handle(event);
}
