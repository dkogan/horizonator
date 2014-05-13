#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Double_Window.H>

#include "Fl_Scroll_Draggable.hh"
#include "fltk_annotated_image.hh"
#include "orb_osmlayer.hpp"
#include "orb_renderviewlayer.hh"
#include "orb_mapctrl.hpp"

#include "render_terrain.h"


// window geometry
#define WINDOW_W 800
#define WINDOW_H 600

static Fl_Scroll_Draggable*    render_scroll;
static CvFltkWidget_annotated* widgetImage;

static void cb_slippymap( Fl_Widget* widget, void* cookie );
static void cb_render_scroll_clicked(Fl_Widget* widget,
                                     void*      cookie );
static void redraw_slippymap( void* mapctrl );

int main(int argc, char** argv)
{
  int   doBatch         = 0;
  int   render_width    = -1;
  float render_fovy_deg = -1.0f;
  char* batchFileOutput = NULL;

  float batch_view_lat, batch_view_lon;

  const char* usage =
    "%s [--batch lat,lon] [--output file.png] [--width w] [--fov angle]\n"
    "   [--help]\n"
    "  --batch just does a render and exits. No annotations.\n"
    "  If --output is also given, the render goes to a file; otherwise to a window.\n"
    "  If specified, --width and --fov control the render parameters.\n"
    "  Angle is the vertical view angle, in degrees.\n"
    "  With no --batch, the full FLTK app is launched; annotations and all\n";

  struct option long_options[] =
    {
      {"batch",    required_argument, NULL, 'b' },
      {"width",    required_argument, NULL, 'w' },
      {"fov",      required_argument, NULL, 'f' },
      {"output",   required_argument, NULL, 'o' },
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

    case 'b':
      doBatch = 1;
      lat = strtok( optarg, "," );
      if( lat == NULL )
      {
        fprintf(stderr, "Couldn't parse lat,lon;\n" );
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      batch_view_lat = atof( lat );

      lon = strtok( NULL,   "," );
      if( lon == NULL )
      {
        fprintf(stderr, "Couldn't parse lat,lon;\n" );
        fprintf(stderr, usage, argv[0]);
        return 1;
      }
      batch_view_lon = atof( lon );
      break;

    case 'o':
      batchFileOutput = optarg;
      break;

    case 'w':
      render_width = atoi(optarg);
      break;

    case 'f':
      render_fovy_deg = atof(optarg);
      break;

    default: ;
    }
  } while(getopt_res != -1);

  if( batchFileOutput && !doBatch )
  {
    fprintf(stderr, "--output makes sense ONLY with --batch. Giving up\n");
    return 1;
  }

  if( doBatch )
  {
    if( !batchFileOutput )
      render_terrain_to_window( batch_view_lat, batch_view_lon );
    else
    {
      IplImage* img = render_terrain( batch_view_lat, batch_view_lon, NULL,
                                      render_width, render_fovy_deg,
                                      true );
      cvSaveImage( batchFileOutput, img );
      cvReleaseImage( &img );
    }
    return 0;
  }









  Fl::lock();

  std::vector<orb_layer*> layers;
  orb_mapctrl*            mapctrl;
  orb_renderviewlayer*    renderviewlayer;

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

    renderviewlayer        = new orb_renderviewlayer;
    orb_osmlayer* osmlayer = new orb_osmlayer;
    osmlayer->callback( &redraw_slippymap, mapctrl );

    layers.push_back(osmlayer);
    layers.push_back(renderviewlayer);

    mapctrl->layers(layers);

    // callback for the right mouse button
    mapctrl->callback( &cb_slippymap, renderviewlayer );
  }
  {
    render_scroll = new Fl_Scroll_Draggable( 0, map_h, window->w(), window->h() - map_h,
                                             renderviewlayer, mapctrl );
    render_scroll->end();
    static void* cookie[] = {renderviewlayer, mapctrl};
    render_scroll->callback( &cb_render_scroll_clicked, cookie );
  }

  window->resizable(window);
  window->end();
  window->show();


  Fl::run();

  return 0;
}

static void redraw_slippymap( void* mapctrl )
{
  reinterpret_cast<orb_mapctrl*>(mapctrl)->redraw();
}


static void cb_slippymap(Fl_Widget* widget,
                         void*      cookie )
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

    orb_renderviewlayer* renderviewlayer = reinterpret_cast<orb_renderviewlayer*>(cookie);
    renderviewlayer->setlatlon( view_lat, view_lon );

    float elevation;

    printf("rendering latlon: %f/%f\n", view_lat, view_lon);
    IplImage* img = render_terrain( view_lat, view_lon, &elevation,
                                    -1, -1.0f,
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

static void cb_render_scroll_clicked(Fl_Widget* widget,
                                     void*      cookie)
{
    orb_renderviewlayer* renderviewlayer = reinterpret_cast<orb_renderviewlayer*>( ((void**)cookie)[0] );
    orb_mapctrl*         mapctrl         = reinterpret_cast<orb_mapctrl*>        ( ((void**)cookie)[1] );


    Fl_Scroll* scroll = reinterpret_cast<Fl_Scroll*>(widget);
    if( Fl::event()        == FL_PUSH &&
        Fl::event_button() == FL_RIGHT_MOUSE )
    {
        float lon, lat;
        if( render_pick( &lon, &lat,
                         Fl::event_x() - scroll->x() + scroll->xposition(),
                         Fl::event_y() - scroll->y() + scroll->yposition() ) )
        {
            renderviewlayer->set_pick( lon, lat);
        }
        else
            renderviewlayer->unset_pick();

        mapctrl->redraw();
    }
}
