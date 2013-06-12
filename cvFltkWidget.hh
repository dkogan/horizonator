#ifndef __CV_FLTK_WIDGET_HH__
#define __CV_FLTK_WIDGET_HH__

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_RGB_Image.H>

#include <opencv2/core/core_c.h>

#include <string.h>

// this class is designed for simple visualization of data passed into the class from the
// outside. Examples of data sources are cameras, video files, still images, processed data, etc.

// Both color and grayscale displays are supported. Incoming data is assumed to be of the desired
// type
enum CvFltkWidget_ColorChoice  { WIDGET_COLOR, WIDGET_GRAYSCALE };

class CvFltkWidget : public Fl_Widget
{
    CvFltkWidget_ColorChoice  colorMode;

    Fl_RGB_Image*  flImage;
    IplImage*      cvImage;

    void cleanup(void)
    {
        if(flImage != NULL)
        {
            delete flImage;
            flImage = NULL;
        }

        if(cvImage == NULL)
        {
            cvReleaseImage(&cvImage);
            cvImage = NULL;
        }
    }

    // called by FLTK to alert this widget about an event. An upper level callback can be triggered
    // here
    virtual int handle(int event)
    {
        // I handle FL_LEAVE even if the mouse isn't in the window right now because I want to make
        // absolutely sure I don't miss this event. Missing it could mean wrongly latching in the
        // mouse-over-widget state
        if(event == FL_LEAVE)
        {
            do_callback();
            return 1;
        }

        // I ignore this event if the mouse is not in the image window. I ask FLTK and ALSO, look at
        // the image dimensions. I do both of those because FLTK may resize the widget, but the
        // rendering of the widget does not respect that resizing
        if(Fl::event_inside(this) &&
           Fl::event_x() >= x() && Fl::event_x() < x() + cvImage->width &&
           Fl::event_y() >= y() && Fl::event_y() < y() + cvImage->height )
        {
            switch(event)
            {
            case FL_PUSH:
            case FL_DRAG:
            case FL_RELEASE:
            case FL_MOVE:
            case FL_ENTER:
                do_callback();
                return 1;

            default: ;
            }
        }

        return Fl_Widget::handle(event);
    }

public:
    CvFltkWidget(int x, int y, int w, int h,
                 CvFltkWidget_ColorChoice  _colorMode)
        : Fl_Widget(x,y,w,h),
          colorMode(_colorMode),
          flImage(NULL), cvImage(NULL)
    {
        int numChannels = (colorMode == WIDGET_COLOR) ? 3 : 1;

        cvImage = cvCreateImage(cvSize(w,h), IPL_DEPTH_8U, numChannels);
        if(cvImage == NULL)
            return;

        flImage = new Fl_RGB_Image((unsigned char*)cvImage->imageData,
                                   w, h, numChannels, cvImage->widthStep);
        if(flImage == NULL)
        {
            cleanup();
            return;
        }
    }

    virtual ~CvFltkWidget()
    {
        cleanup();
    }

    operator IplImage*()
    {
        return cvImage;
    }

    void draw()
    {
        // this is the FLTK draw-me-now callback
        flImage->draw(x(), y());
    }

    // Used to trigger a redraw if out drawing buffer was already updated.
    void redrawNewFrame(void)
    {
        flImage->uncache();
        redraw();

        // If we're drawing from a different thread, FLTK needs to be woken up to actually do
        // the redraw
        Fl::awake();
    }
};

#endif
