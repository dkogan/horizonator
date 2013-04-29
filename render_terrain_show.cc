#include <stdio.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include "fltk_scroll_wheelable.hh"
#include "fltk_annotated_image.hh"

extern "C"
{
  #include "render_terrain.h"
}

  // two different viewpoints for testing
#if 0
  // Chilao camp
  const float   view_lat = 34.3252f;
        float   view_lon = -118.02f;
#else
  // peak of Iron Mt
  const float   view_lat = 34.2883f;
        float   view_lon = -117.7128f;
#endif



int main(int argc, char** argv)
{
  int doOnscreen = 0;
  char* outfile = NULL;

  struct option long_options[] =
    {
      {"onscreen",   no_argument,       &doOnscreen, 1 },
      {"save",       required_argument, NULL,        's' },
      {}
    };

  int getopt_res;
  do
  {
    getopt_res = getopt_long(argc, argv, "", long_options, NULL);
    switch( getopt_res )
    {
    case 's':
      outfile = optarg;
      break;

    case '?':
      fprintf(stderr, "Unknown cmdline option encountered\n");
      return 1;

    default: ;
    }
  } while(getopt_res != -1);

  float elevation;
  IplImage* img;

  if( doOnscreen )
  {
    render_terrain_to_window( view_lat, view_lon );
    return 0;
  }
  img = render_terrain( view_lat, view_lon, &elevation,
                        outfile != NULL /* BGR is we're writing to a file;
                                           cvSaveImage() expects */
                        );

  if( outfile )
  {
    cvSaveImage( outfile, img, 0 );
    return 0;
  }

  // start up the GUI
  Fl_Double_Window*       window      = new Fl_Double_Window( 800, 600, "Photo annotator" );
  Fl_Scroll_Wheelable*    scroll      = new Fl_Scroll_Wheelable( 0, 0, window->w(), window->h() );
  scroll->begin();
  CvFltkWidget_annotated* widgetImage = new CvFltkWidget_annotated(0, 0, img->width, img->height,
                                                                   WIDGET_COLOR);
  cvCopy( img, (IplImage*)*widgetImage, NULL );
  scroll->end();

  window->resizable(window);
  window->end();
  window->show();

  // set up the labels
  widgetImage->setTransformation( view_lat * M_PI / 180.0,
                                  view_lon * M_PI / 180.0,
                                  elevation,
                                  mercator,
                                  0,0,0,0 );

  while (Fl::wait())
  {
  }

  delete window;

  cvReleaseImage( &img );
  return 0;
}
