#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Gl_Window.H>
#include <GL/gl.h>

#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"

extern "C"
{
#include "horizonator.h"
#include "util.h"
}


#define WINDOW_W 800
#define WINDOW_H 600

static Fl_Double_Window* g_window;

static void redraw_slippymap( void* mapctrl )
{
    reinterpret_cast<orb_mapctrl*>(mapctrl)->redraw();
}

class GLWidget : public Fl_Gl_Window
{
    horizonator_context_t m_ctx;

    GLenum m_winding;
    int    m_polygon_mode_idx;
    float  az_center_deg;
    float  az_radius_deg;


public:
    GLWidget(int x, int y, int w, int h) :
        Fl_Gl_Window(x, y, w, h)
    {
        mode(FL_RGB8 | FL_DOUBLE | FL_OPENGL3);
        m_ctx = (horizonator_context_t){};


        m_winding          = GL_CCW;
        m_polygon_mode_idx = 0;

        // Look North initially, with some arbitrary field of view
        az_center_deg = 0.0f;
        az_radius_deg = 30.0f;
    }

    void draw(void)
    {
        if(m_ctx.Ntriangles == 0)
        {
            // Docs say to init this here. I don't know why.
            // https://www.fltk.org/doc-1.3/opengl.html
            if(!horizonator_init1( &m_ctx,

                                   false,
                                   34.2884, -117.7134,

                                   ".", "/home/dima/.horizonator/tiles",
                                   true))
            {
                MSG("horizonator_init1() failed. Giving up");
                exit(1);
            }

            if(!horizonator_zoom(&m_ctx,
                                 az_center_deg - az_radius_deg,
                                 az_center_deg + az_radius_deg))
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

            const float pixels_to_move_rad = 100.0f;
            az_center_deg += az_radius_deg * (float)Fl::event_dx() / pixels_to_move_rad;

            const float pixels_to_double = 20.0f;
            float r = exp2((float)Fl::event_dy() / pixels_to_double);
            az_radius_deg *= r;
            if     (az_radius_deg < 1.0f)  az_radius_deg = 1.0f;
            else if(az_radius_deg > 179.f) az_radius_deg = 179.f;
            if(!horizonator_zoom(&m_ctx,
                                 az_center_deg - az_radius_deg,
                                 az_center_deg + az_radius_deg))
            {
                MSG("horizonator_zoom() failed. Giving up");
                delete g_window;
            }
            redraw();
            return 1;

        // case  FL_PUSH:
        //     // click
        //     updatething();
        //     redraw();
        //     return 1;

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
    orb_mapctrl*            slippymap;
    GLWidget*               gl_widget __attribute__((unused));

    g_window = new Fl_Double_Window( WINDOW_W, WINDOW_H, "Horizonator" );

    const int map_h = g_window->h()/2;

    {
        slippymap = new orb_mapctrl( 0, 0, g_window->w(), map_h, "horizonator!" );
        slippymap->box(FL_NO_BOX);
        slippymap->color(FL_BACKGROUND_COLOR);
        slippymap->selection_color(FL_BACKGROUND_COLOR);
        slippymap->labeltype(FL_NORMAL_LABEL);
        slippymap->labelfont(0);
        slippymap->labelsize(14);
        slippymap->labelcolor(FL_FOREGROUND_COLOR);
        slippymap->align(Fl_Align(FL_ALIGN_CENTER));

        orb_osmlayer* osmlayer = new orb_osmlayer;
        osmlayer->callback( &redraw_slippymap, slippymap );

        layers.push_back(osmlayer);

        slippymap->layers(layers);
    }
    {
        gl_widget = new GLWidget(0, map_h, g_window->w(), g_window->h()-map_h);
    }

    g_window->resizable(g_window);
    g_window->end();
    g_window->show();

    Fl::run();

    return 0;
}
