#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <string.h>
#include <assert.h>
using namespace std;

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Value_Slider.H>

#include <cvFltkWidget.hh>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

static CvFltkWidget* widgetImage;
static Fl_Value_Slider* thresh1;
static Fl_Value_Slider* thresh2;
static Fl_Value_Slider* canny_width;
static Fl_Value_Slider* presmooth;
static IplImage* img;

static void touchedThresh(Fl_Widget* widget __attribute__((unused)),
                          void* cookie __attribute__((unused)) )
{
  static CvMat* smoothed;
  if(! smoothed )
    smoothed = cvCreateMat(img->height, img->width, CV_8UC1);

  int smooth_width = (int)(presmooth->value()/2)*2+1;
  CvMat* src = (CvMat*)img;
  if( smooth_width > 1 )
  {
    src = smoothed;
    cvSmooth(img, smoothed, CV_GAUSSIAN,
             smooth_width, smooth_width,
             0.0, 0.0);
  }

  cvCanny(src, *widgetImage,
          thresh1->value(),
          thresh2->value(),
          (int)(canny_width->value()/2)*2 + 1);
  widgetImage->redrawNewFrame();
}

int main(int argc, char* argv[])
{
    Fl::lock();
    Fl::visual(FL_RGB);

    assert( argc == 2 );
    img = cvLoadImage( argv[1], CV_LOAD_IMAGE_GRAYSCALE );
    assert(img);

    Fl_Window window(img->width, img->height + 400);
    widgetImage = new CvFltkWidget(0, 0, img->width, img->height,
                                   WIDGET_GRAYSCALE);

    window.resizable(window);
    thresh1     = new Fl_Value_Slider(  0, img->height,    150, 20, "thresh1");
    thresh2     = new Fl_Value_Slider(150, img->height,    150, 20, "thresh2");
    canny_width = new Fl_Value_Slider(0,   img->height+40, 150, 20, "width");
    presmooth   = new Fl_Value_Slider(0,   img->height+80, 150, 20, "presmooth");

#define setupslider(slider,min,max)             \
    slider->callback(&touchedThresh, NULL);     \
    slider->bounds(min,max);                    \
    slider->type(FL_HORIZONTAL);                \
    slider->value(min)

    setupslider(thresh1,     0, 1000);
    setupslider(thresh2,     0, 1000);
    setupslider(canny_width, 3, 7);
    setupslider(presmooth,   1, 31);

    window.end();
    window.show();

    touchedThresh(NULL, NULL);

    while (Fl::wait())
    {
    }

    Fl::unlock();

    return 0;
}
