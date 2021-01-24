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
static view_t g_view =
    { // default az_center_deg, az_radius_deg. Can be set on the commandline
      0.0f, 30.0f,

      // default lat, lon. These WILL be set on the commandline
      -1000.f, -1000.f};





static void redraw_slippymap( void* mapctrl )
{
    reinterpret_cast<orb_mapctrl*>(mapctrl)->redraw();
}

static void newrender(horizonator_context_t* ctx,
                      float lat, float lon)
{
    g_view.lat = lat;
    g_view.lon = lon;

    // If the context hasn't been inited yet, I don't move the map. It will load
    // later, in the already-correct spot
    if(horizonator_context_isvalid(ctx))
        horizonator_move_viewer_keep_data(ctx, lat, lon);
}

class GLWidget : public Fl_Gl_Window
{
    horizonator_context_t m_ctx;
    bool render_texture;

    GLenum m_winding;
    int    m_polygon_mode_idx;
    int    m_last_drag_update_xy[2];


    void clip_az_radius_deg(void)
    {
        if     (g_view.az_radius_deg < 1.0f)  g_view.az_radius_deg = 1.0f;
        else if(g_view.az_radius_deg > 179.f) g_view.az_radius_deg = 179.f;
    }

public:
    GLWidget(int x, int y, int w, int h,
             bool _render_texture) :
        Fl_Gl_Window(x, y, w, h),
        render_texture(_render_texture)
    {
        mode(FL_RGB8 | FL_DOUBLE | FL_OPENGL3 | FL_DEPTH);
        m_ctx = (horizonator_context_t){};


        m_winding          = GL_CCW;
        m_polygon_mode_idx = 0;
    }

    horizonator_context_t* ctx(void)
    {
        return &m_ctx;
    }

    void draw(void)
    {
        if(!horizonator_context_isvalid(&m_ctx))
        {
            // Docs say to init this here. I don't know why.
            // https://www.fltk.org/doc-1.3/opengl.html
            if(!horizonator_init1( &m_ctx,

                                   render_texture,
                                   g_view.lat, g_view.lon,

                                   "~/.horizonator/DEMs_SRTM3",
                                   "~/.horizonator/tiles",
                                   true))
            {
                MSG("horizonator_init1() failed. Giving up");
                exit(1);
            }

            if(!horizonator_pan_zoom(&m_ctx,
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
                if(!horizonator_pan_zoom(&m_ctx,
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
            if(Fl::event_button() == FL_LEFT_MOUSE)
            {
                // I pan and zoom with left-click-and-drag
                m_last_drag_update_xy[0] = Fl::event_x();
                m_last_drag_update_xy[1] = Fl::event_y();
                return 1;
            }

            if(Fl::event_button() == FL_RIGHT_MOUSE)
            {
                // Right-click. I figure out where the user clicked, and show
                // that point on the map
                float lon, lat;
                if(!horizonator_pick(&m_ctx,
                                     &lat, &lon,
                                     Fl::event_x(), Fl::event_y()))
                    g_slippymap_annotations->unset_pick();
                else
                    g_slippymap_annotations->set_pick(lat, lon);
                g_slippymap->redraw();
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

                // For friendlier UI, I handle either vertical or horizontal
                // events at a time only. This prevents unintentional zooming
                // while panning, and vice versa
                if(dxy[0]*dxy[0] > dxy[1]*dxy[1])
                {
                    const float deg_per_pixel = 2.f*g_view.az_radius_deg/(float)pixel_w();
                    g_view.az_center_deg -= deg_per_pixel * (float)dxy[0];
                }
                else
                {
                    const float pixels_to_double = 200.0f;
                    float r = exp2((float)dxy[1] / pixels_to_double);
                    g_view.az_radius_deg *= r;
                    clip_az_radius_deg();
                }
                if(!horizonator_pan_zoom(&m_ctx,
                                         g_view.az_center_deg - g_view.az_radius_deg,
                                         g_view.az_center_deg + g_view.az_radius_deg))
                {
                    MSG("horizonator_zoom() failed. Giving up");
                    delete g_window;
                    return 1;
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

static void callback_slippymap(Fl_Widget* slippymap,
                               void*      cookie )
{
    // Something happened with the slippy-map. If it's a right-click then I do
    // stuff
    if(! (Fl::event()        == FL_PUSH &&
          Fl::event_button() == FL_RIGHT_MOUSE ))
        return;

    // right mouse button pressed
    orb_point<double> gps;
    if( ((orb_mapctrl*)slippymap)->mousegps(gps) != 0 )
    {
        MSG("couldn't get mouse click latlon position for some reason...");
        return;
    }

    float lat = (float)gps.get_y();
    float lon = (float)gps.get_x();

    GLWidget* w = (GLWidget*)cookie;
    horizonator_context_t* ctx = w->ctx();
    newrender(ctx, lat, lon);
    w->redraw();
    slippymap->redraw();
}

int main(int argc, char** argv)
{
    const char* usage =
        "%s [--texture]\n"
        "   LAT LON [AZ_CENTER_DEG AZ_RADIUS_DEG]\n"
        "\n"
        "This is an interactive tool, so the viewer position and azimuth bounds\n"
        "given on the commandline are just starting points. The viewer position\n"
        "is required, but the azimuth bounds may be omitted; some reasonable\n"
        "defaults will be used."
        "\n"
        "By default we colorcode the renders by range. If --texture, we\n"
        "use a set of image tiles to texture the render instead\n";

    struct option opts[] = {
        { "texture",           no_argument,       NULL, 'T' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };


    bool render_texture = false;

    int opt;
    do
    {
        // "h" means -h does something
        opt = getopt_long(argc, argv, "h1234567890.", opts, NULL);
        switch(opt)
        {
        case -1:
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '.':
            // these are numbers that aren't actually arguments. I stop parsing
            // here
            opt = -1;
            optind--;
            break;


        case 'h':
            printf(usage, argv[0]);
            return 0;

        case 'T':
            render_texture = true;
            break;

        case '?':
            fprintf(stderr, "Unknown option\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    } while( opt != -1 );

    int Nargs_remaining = argc-optind;
    if( !(Nargs_remaining == 2 || Nargs_remaining == 4) )
    {
        fprintf(stderr, "Need exactly 2 or 4 non-option arguments. Got %d\n\n",Nargs_remaining);
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    g_view.lat = (float)atof(argv[optind+0]);
    g_view.lon = (float)atof(argv[optind+1]);
    if(Nargs_remaining == 4)
    {
        g_view.az_center_deg = (float)atof(argv[optind+2]);
        g_view.az_radius_deg = (float)atof(argv[optind+3]);
    }

    ////////////// Done cmdline-option parsing. Let's do the thing.


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
    }
    {
        g_gl_widget = new GLWidget(0, map_h, g_window->w(), g_window->h()-map_h,
                                   render_texture);
    }


    orb_osmlayer* osmlayer  = new orb_osmlayer;
    g_slippymap_annotations = new SlippymapAnnotations(&g_view, g_gl_widget->ctx());
    osmlayer->callback( &redraw_slippymap, g_slippymap );

    layers.push_back(osmlayer);
    layers.push_back(g_slippymap_annotations);
    g_slippymap->layers(layers);

    g_slippymap->callback( &callback_slippymap, (void*)g_gl_widget );

    g_window->resizable(g_window);
    g_window->end();
    g_window->show();

    Fl::run();

    return 0;
}
