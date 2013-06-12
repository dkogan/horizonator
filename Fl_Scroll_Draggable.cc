#include <FL/Fl.H>
#include "Fl_Scroll_Draggable.hh"

int Fl_Scroll_Draggable::handle(int event)
{
  int x,y,w,h;
  bbox(x,y,w,h); // inside-scroll-area geometry


  // if any event at all happens, I update the slippy map with the current
  // observable world. If it's different enough, I redraw that layer
  {
    // I look through all my children to find the opencv widget child
    if( render == NULL )
    {
      int N = children();
      for( int i=0; i<N; i++ )
      {
        render = dynamic_cast<CvFltkWidget*>( child(i) );
        if( render != NULL )
          break;
      }
    }
    if( render != NULL )
    {
      int left  = x - render->x();
      if( left < 0 )
        left = 0;

      int right = x + w-1 - render->x();
      if( right >= render->w() )
        right = render->w()-1;

      if( renderviewlayer->setview( (float)left  / (float)(render->w()-1) * 360.0,
                                    (float)right / (float)(render->w()-1) * 360.0 ) )
      {
        mapctrl->redraw();
      }
    }
  }



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
