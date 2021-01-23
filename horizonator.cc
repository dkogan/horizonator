#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Gl_Window.H>
#include <GL/gl.h>

#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"

#include "slippymap-annotations.hh"

extern "C"
{
#include "horizonator.h"
#include "util.h"
}


#define WINDOW_W 800
#define WINDOW_H 600

class GLWidget;



/////////// Globals
//// Widgets
static Fl_Double_Window*     g_window;
static orb_mapctrl*          g_slippymap;
static SlippymapAnnotations* g_slippymap_annotations;
static GLWidget*             g_gl_widget;

//// The observer state
// Look North initially, with some arbitrary field of view
static view_t g_view = {0.0f, 30.0f, -1000.f, -1000.f};





static void redraw_slippymap( void* mapctrl )
{
    reinterpret_cast<orb_mapctrl*>(mapctrl)->redraw();
}

static void newrender(float lat, float lon)
{
    g_view.lat = lat;
    g_view.lon = lon;
}

static void callback_slippymap(Fl_Widget* slippymap __attribute__((unused)),
                               void*      cookie    __attribute__((unused)) )
{
    // Something happened with the slippy-map. If it's a right-click then I do
    // stuff
    if(! (Fl::event()        == FL_PUSH &&
          Fl::event_button() == FL_RIGHT_MOUSE ))
        return;

    // right mouse button pressed
    orb_point<double> gps;
    if( g_slippymap->mousegps(gps) != 0 )
    {
        MSG("couldn't get mouse click latlon position for some reason...");
        return;
    }

    float lat = (float)gps.get_y();
    float lon = (float)gps.get_x();

    newrender(lat,lon);
}

class GLWidget : public Fl_Gl_Window
{
    horizonator_context_t m_ctx;

    GLenum m_winding;
    int    m_polygon_mode_idx;
    int    m_last_drag_update_xy[2];


    void clip_az_radius_deg(void)
    {
        if     (g_view.az_radius_deg < 1.0f)  g_view.az_radius_deg = 1.0f;
        else if(g_view.az_radius_deg > 179.f) g_view.az_radius_deg = 179.f;
    }

public:
    GLWidget(int x, int y, int w, int h) :
        Fl_Gl_Window(x, y, w, h)
    {
        mode(FL_RGB8 | FL_DOUBLE | FL_OPENGL3 | FL_DEPTH);
        m_ctx = (horizonator_context_t){};


        m_winding          = GL_CCW;
        m_polygon_mode_idx = 0;
    }

    void draw(void)
    {
        if(m_ctx.Ntriangles == 0)
        {
            // Docs say to init this here. I don't know why.
            // https://www.fltk.org/doc-1.3/opengl.html
            if(!horizonator_init1( &m_ctx,

                                   false,
                                   g_view.lat, g_view.lon,

                                   ".", "/home/dima/.horizonator/tiles",
                                   true))
            {
                MSG("horizonator_init1() failed. Giving up");
                exit(1);
            }

            if(!horizonator_zoom(&m_ctx,
                                 g_view.az_center_deg - g_view.az_radius_deg,
                                 g_view.az_center_deg + g_view.az_radius_deg))
            {
                MSG("horizonator_zoom() failed. Giving up");
                exit(1);
            }
        }

        if(!valid())
            horizonator_resized(&m_ctx, pixel_w(), pixel_h());
        horizonator_redraw(&m_ctx);
    }

    virtual int handle(int event)
    {
        switch(event)
        {
        case FL_SHOW:
            if(shown())
            {
                static bool done = false;
                if(!done)
                {
                    // Docs say to do this. Don't know why.
                    // https://www.fltk.org/doc-1.3/opengl.html
                    done = true;
                    make_current();
                }
            }
            break;

        case FL_FOCUS:
            return 1;

        case FL_KEYDOWN:

            make_current();
            if(!valid())
                valid(1);

            if(Fl::event_key('w'))
            {
                const GLenum polygon_modes[] = {GL_FILL, GL_LINE, GL_POINT};
                if(++m_polygon_mode_idx == sizeof(polygon_modes)/sizeof(polygon_modes[0]))
                    m_polygon_mode_idx = 0;
                glPolygonMode(GL_FRONT_AND_BACK, polygon_modes[ m_polygon_mode_idx ] );
                redraw();
                return 1;
            }
            else if(Fl::event_key('r'))
            {
                if (m_winding == GL_CCW) m_winding = GL_CW;
                else                   m_winding = GL_CCW;
                glFrontFace(m_winding);
                redraw();
                return 1;
            }
            else if(Fl::event_key('q'))
            {
                delete g_window;
                return 1;
            }

            break;

        case FL_MOUSEWHEEL:
            {
                // I pan and zoom with the horizontal/vertical mouse wheel
                const float pixels_to_move_rad = 100.0f;
                g_view.az_center_deg += g_view.az_radius_deg * (float)Fl::event_dx() / pixels_to_move_rad;

                const float pixels_to_double = 20.0f;
                const float r = exp2((float)Fl::event_dy() / pixels_to_double);
                g_view.az_radius_deg *= r;
                clip_az_radius_deg();
                if(!horizonator_zoom(&m_ctx,
                                     g_view.az_center_deg - g_view.az_radius_deg,
                                     g_view.az_center_deg + g_view.az_radius_deg))
                {
                    MSG("horizonator_zoom() failed. Giving up");
                    delete g_window;
                }
                redraw();
                g_slippymap->redraw();
                return 1;
            }

        case FL_PUSH:
            // I pan and zoom with left-click-and-drag
            if(Fl::event_button() == FL_LEFT_MOUSE)
            {
                m_last_drag_update_xy[0] = Fl::event_x();
                m_last_drag_update_xy[1] = Fl::event_y();
                return 1;
            }
            break;

        case FL_DRAG:
            // I pan and zoom with left-click-and-drag
            if(Fl::event_state() & FL_BUTTON1)
            {
                int dxy[] =
                    {
                        Fl::event_x() - m_last_drag_update_xy[0],
                        Fl::event_y() - m_last_drag_update_xy[1]
                    };

                m_last_drag_update_xy[0] = Fl::event_x();
                m_last_drag_update_xy[1] = Fl::event_y();

                const float deg_per_pixel = 2.f*g_view.az_radius_deg/(float)pixel_w();
                g_view.az_center_deg -= deg_per_pixel * (float)dxy[0];

                const float pixels_to_double = 100.0f;
                float r = exp2((float)dxy[1] / pixels_to_double);
                g_view.az_radius_deg *= r;
                clip_az_radius_deg();
                if(!horizonator_zoom(&m_ctx,
                                     g_view.az_center_deg - g_view.az_radius_deg,
                                     g_view.az_center_deg + g_view.az_radius_deg))
                {
                    MSG("horizonator_zoom() failed. Giving up");
                    delete g_window;
                }

                redraw();
                g_slippymap->redraw();
                return 1;
            }
            break;
        }

        return Fl_Gl_Window::handle(event);
    }
};

int main(int argc, char** argv)
{
    const char* usage =
        "%s [--help]\n";

    struct option long_options[] =
        {
            {"help",     no_argument,       NULL, 'h' },
            {}
        };

    int getopt_res;
    do
    {
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

        default: ;
        }
    } while(getopt_res != -1);

    Fl::lock();

    std::vector<orb_layer*> layers;

    g_window = new Fl_Double_Window( WINDOW_W, WINDOW_H, "Horizonator" );

    const int map_h = g_window->h()/2;

    {
        g_slippymap = new orb_mapctrl( 0, 0, g_window->w(), map_h, "horizonator!" );
        g_slippymap->box(FL_NO_BOX);
        g_slippymap->color(FL_BACKGROUND_COLOR);
        g_slippymap->selection_color(FL_BACKGROUND_COLOR);
        g_slippymap->labeltype(FL_NORMAL_LABEL);
        g_slippymap->labelfont(0);
        g_slippymap->labelsize(14);
        g_slippymap->labelcolor(FL_FOREGROUND_COLOR);
        g_slippymap->align(Fl_Align(FL_ALIGN_CENTER));

        orb_osmlayer* osmlayer  = new orb_osmlayer;
        g_slippymap_annotations = new SlippymapAnnotations(&g_view);
        osmlayer->callback( &redraw_slippymap, g_slippymap );

        layers.push_back(osmlayer);
        layers.push_back(g_slippymap_annotations);
        g_slippymap->layers(layers);

        g_slippymap->callback( &callback_slippymap, NULL );
    }
    {
        g_gl_widget = new GLWidget(0, map_h, g_window->w(), g_window->h()-map_h);
    }

    g_window->resizable(g_window);
    g_window->end();
    g_window->show();

    newrender(34.2884, -117.7134);

    Fl::run();

    return 0;
}
