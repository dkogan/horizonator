#include <stdio.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <cvFltkWidget.hh>

extern "C"
{
  #include "render_terrain.h"
}

  // two different viewpoints for testing
#if 1
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
  int doOnscreen   = 0;
  int doNoMercator = 0;

  struct option long_options[] =
    {
      {"onscreen",   no_argument, &doOnscreen,   1 },
      {"nomercator", no_argument, &doNoMercator, 1 },
      {}
    };

  int getopt_res;
  do
  {
    getopt_res = getopt_long(argc, argv, "", long_options, NULL);
    if( getopt_res == '?' )
    {
      fprintf(stderr, "Unknown cmdline option encountered\n");
      exit(1);
    }
  } while(getopt_res != -1);

  IplImage* img = render_terrain( view_lat, view_lon );

  // start up the GUI
  Fl_Double_Window* window =
    new Fl_Double_Window( img->width, img->height, "Photo annotator" );
  CvFltkWidget* widgetImage =
    new CvFltkWidget(0, 0, img->width, img->height,
                     WIDGET_COLOR);

  cvCopy( img, (IplImage*)*widgetImage, NULL );

  window->resizable(window);
  window->end();
  window->show();

  while (Fl::wait())
  {
  }

  delete window;

  cvReleaseImage( &img );
  return 0;
}
