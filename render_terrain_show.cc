#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include "fltk_annotated_image.hh"
#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"

#include "render_terrain.h"


// window geometry
#define WINDOW_W 800
#define WINDOW_H 600

static Fl_Scroll*              render_scroll;
static CvFltkWidget_annotated* widgetImage;

static void cb_slippymap( Fl_Widget* widget, void* cookie );

int main(int argc, char** argv)
{
  Fl::lock();

  int   doGlonly = 0;
  float glonly_view_lat, glonly_view_lon;

  const char* usage =
    "%s [--glonly lat,lon] [--help]\n"
    "  --glonly renders to a OpenGL window. No annotations\n"
    "  otherwise the full FLTK app is launched; annotations and all\n";

  struct option long_options[] =
    {
      {"glonly",   required_argument, NULL, 'g' },
      {"help",     no_argument,       NULL, 'h' },
      {}
    };

  int getopt_res;
  do
  {
    char* lat;
    char* lon;
    getopt_res = getopt_long(argc, argv, "", long_options, NULL);
    switch( getopt_res )
    {
    case '?':
      fprintf(stderr, "Unknown cmdline option encountered\nUsage: ");
      fprintf(stderr, usage, argv[0]);
      return 1;

    case 'h':
      fprintf(stdout, usage, argv[0]);
      return 0;

    case 'g':
      doGlonly = 1;
      lat = strtok( optarg, "," );
      if( lat == NULL )
      {
        fprintf(stderr, "Couldn't parse lat,lon;\n" );
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      glonly_view_lat = atof( lat );

      lon = strtok( NULL,   "," );
      if( lon == NULL )
      {
        fprintf(stderr, "Couldn't parse lat,lon;\n" );
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      glonly_view_lon = atof( lon );
      break;

    default: ;
    }
  } while(getopt_res != -1);

  if( doGlonly )
  {
    render_terrain_to_window( glonly_view_lat, glonly_view_lon );
    return 0;
  }

  // start up the GUI
  orb_mapctrl* mapctrl;
  orb_layer*   layer;

  Fl_Double_Window* window = new Fl_Double_Window( WINDOW_W, WINDOW_H, "Horizonator" );
  const int         map_h  = window->h()/2;

  {
    mapctrl = new orb_mapctrl( 0, 0, window->w(), map_h, "Slippy Map" );
    mapctrl->box(FL_NO_BOX);
    mapctrl->color(FL_BACKGROUND_COLOR);
    mapctrl->selection_color(FL_BACKGROUND_COLOR);
    mapctrl->labeltype(FL_NORMAL_LABEL);
    mapctrl->labelfont(0);
    mapctrl->labelsize(14);
    mapctrl->labelcolor(FL_FOREGROUND_COLOR);
    mapctrl->align(Fl_Align(FL_ALIGN_CENTER));

    // Create the OSM base layer
    layer = new orb_osmlayer();

    std::vector<orb_layer*> layers;
    layers.push_back(layer);

    // Set the OSM base layer
    mapctrl->layers(layers);

    // callback for the right mouse button
    mapctrl->callback( &cb_slippymap, NULL );
  }
  {
    render_scroll = new Fl_Scroll( 0, map_h, window->w(), window->h() - map_h );
    render_scroll->end();
  }

  window->resizable(window);
  window->end();
  window->show();


  Fl::run();

  delete window;
  delete layer;

  return 0;
}


static void cb_slippymap(Fl_Widget* widget,
                         void*      cookie __attribute__((unused)) )
{
  if( Fl::event()        == FL_PUSH &&
      Fl::event_button() == FL_RIGHT_MOUSE )
  {
    orb_point<double> gps;

    if( reinterpret_cast<orb_mapctrl*>(widget)->mousegps( gps ) != 0 )
    {
      fprintf( stderr, "couldn't get mouse click latlon position for some reason...\n" );
      return;
    }

    float view_lat = (float)gps.get_y();
    float view_lon = (float)gps.get_x();

    float elevation;
    IplImage* img = render_terrain( view_lat, view_lon, &elevation,
                                    false /* BGR is we're writing to a file;
                                             cvSaveImage() expects */
                                    );
    if( img == NULL )
    {
      fl_alert( "Error rendering the terrain....\n");
      return;
    }

    if( !widgetImage )
    {
      render_scroll->begin();
      widgetImage = new CvFltkWidget_annotated(render_scroll->x(), render_scroll->y(), img->width, img->height,
                                               WIDGET_COLOR);
      render_scroll->end();
    }

    cvCopy( img, (IplImage*)*widgetImage, NULL );
    cvReleaseImage( &img );

    // set up the labels
    widgetImage->setTransformation( view_lat * M_PI / 180.0,
                                    view_lon * M_PI / 180.0,
                                    elevation,
                                    mercator,
                                    0,0,0,0 );
    widgetImage->redrawNewFrame();
  }
}
