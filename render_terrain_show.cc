#include <stdio.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include "fltk_annotated_image.hh"

extern "C"
{
  #include "render_terrain.h"
}

  // two different viewpoints for testing
#if 0
  // Chilao camp
  const float   view_lat = 34.3252f;
  const float   view_lon = -118.02f;
#else
  // peak of Iron Mt
  const float   view_lat = 34.2883f;
  const float   view_lon = -117.7128f;
#endif



int main(int argc, char** argv)
{
  int   doGlonly = 0;
  char* outfile  = NULL;

  const char* usage =
    "%s [--glonly] [--save render.png] [--help]\n"
    "  --glonly renders to a OpenGL window. No annotations\n"
    "  --save renders directly to an image. No annotations\n"
    "  otherwise the full FLTK app is launched; annotations and all\n";

  struct option long_options[] =
    {
      {"glonly",   no_argument,       &doGlonly, 1 },
      {"help",     no_argument,       NULL,      'h' },
      {"save",     required_argument, NULL,      's' },
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
      fprintf(stderr, "Unknown cmdline option encountered\nUsage: ");
      fprintf(stderr, usage, argv[0]);
      return 1;

    case 'h':
      fprintf(stdout, usage, argv[0]);
      return 0;

    default: ;
    }
  } while(getopt_res != -1);

  float elevation;
  IplImage* img;

  if( doGlonly )
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
  Fl_Scroll*              scroll      = new Fl_Scroll( 0, 0, window->w(), window->h() );
  CvFltkWidget_annotated* widgetImage = new CvFltkWidget_annotated(0, 0, img->width, img->height,
                                                                   WIDGET_COLOR);
  cvCopy( img, (IplImage*)*widgetImage, NULL );

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
