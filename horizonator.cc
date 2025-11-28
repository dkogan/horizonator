#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Button.H>
#include <GL/gl.h>

#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"

#include "slippymap-annotations.hh"

extern "C"
{
#include "horizonator.h"
#include "util.h"
}


#define WINDOW_W      800
#define WINDOW_H      600
#define STATUS_H      20
#define COPY_BUTTON_W 150

class GLWidget;



/////////// Globals
//// Widgets
static Fl_Double_Window*     g_window;
static orb_mapctrl*          g_slippymap;
static SlippymapAnnotations* g_slippymap_annotations;
static GLWidget*             g_gl_widget;
static Fl_Output*            g_status_text;
static int                   g_status_text_cmd_len = 0;

//// The observer state
// Look North initially, with some arbitrary field of view
static view_t g_view =
    { // default az_center_deg, az_radius_deg. Can be set on the commandline
      0.0f, 30.0f,

      // default lat, lon. These WILL be set on the commandline
      -1000.f, -1000.f};

// unset by default
static float g_picked_lat = 1e6f;
static float g_picked_lon = 1e6f;


static void update_status_text()
{
    char str[256];

    int Nwritten =
        snprintf(str, sizeof(str),
                 "horizonator %.5f %.5f %.1f %.1f",
                 g_view.lat,
                 g_view.lon,
                 g_view.az_center_deg,
                 g_view.az_radius_deg);
    if(Nwritten >= (int)sizeof(str))
    {
        // truncated
        str[sizeof(str)-1] = '\0';
        g_status_text_cmd_len = sizeof(str)-1;
    }
    else
    {
        g_status_text_cmd_len = Nwritten;

        if( g_picked_lon < 1e3f && g_picked_lat < 1e3f)
        {
            int Nwritten2 = snprintf(&str[Nwritten], sizeof(str)-Nwritten,
                                     "; highlighting observed point (%.5f,%.5f)",
                                     g_picked_lat, g_picked_lon);
            if(Nwritten2 >= (int)(sizeof(str)-Nwritten))
            {
                // truncated
                str[sizeof(str)-1] = '\0';
            }
        }
    }
    g_status_text->value(str);
}

static void cb_copy_command(Fl_Widget* widget __attribute__((unused)),
                            void*      cookie __attribute__((unused)))
{
    if(g_status_text_cmd_len > 0)
        Fl::copy(g_status_text->value(), g_status_text_cmd_len,
                 // both clipboards
                 2);
}


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
        horizonator_move(ctx, NULL, lat, lon);

    update_status_text();
}

class GLWidget : public Fl_Gl_Window
{
    horizonator_context_t m_ctx;
    int radius;
    bool render_texture, SRTM1;
    float znear;
    float zfar;
    float znear_color;
    float zfar_color;

    GLenum m_winding;
    int    m_polygon_mode_idx;
    int    m_last_drag_update_xy[2];


    void clip_az_radius_deg(void)
    {
        if     (g_view.az_radius_deg < 1.0f)  g_view.az_radius_deg = 1.0f;
        else if(g_view.az_radius_deg > 179.f) g_view.az_radius_deg = 179.f;
    }

    bool pan_and_zoom(int dx, int dy,
                      float kx, float ky)
    {
        // For friendlier UI, I handle either vertical or horizontal
        // events at a time only. This prevents unintentional zooming
        // while panning, and vice versa
        if(dx*dx > dy*dy)
            g_view.az_center_deg += kx * (float)dx;
        else
        {
            float r = exp2(ky * (float)dy);
            g_view.az_radius_deg *= r;
            clip_az_radius_deg();
        }
        if(!horizonator_pan_zoom(&m_ctx,
                                 g_view.az_center_deg - g_view.az_radius_deg,
                                 g_view.az_center_deg + g_view.az_radius_deg))
        {
            MSG("horizonator_zoom() failed. Giving up");
            return false;
        }

        update_status_text();

        redraw();
        g_slippymap->redraw();
        return true;
    }

public:
    GLWidget(int x, int y, int w, int h,
             int _radius,
             bool _render_texture,
             bool _SRTM1,
             float _znear,
             float _zfar,
             float _znear_color,
             float _zfar_color) :
        Fl_Gl_Window(x, y, w, h),
        radius         (_radius),
        render_texture (_render_texture),
        SRTM1          (_SRTM1),
        znear          (_znear),
        zfar           (_zfar),
        znear_color    (_znear_color),
        zfar_color     (_zfar_color)
    {
        /*
          Here I don't ask for FL_OPENGL3. This is due a a bug in my graphics
          driver or fltk or something like that.

          If I have Intel integrated graphics (i915 or uhd620), then the
          (FL_OPENGL3 | FL_DOUBLE) combination doesn't work right: lots of
          redraws are missed for whatever reason, and the user gets either
          nothing or an out-of-date frame. Turning FL_DOUBLE off fixes THAT, but
          then the horizonator point picking doesn't work: glReadPixels(...,
          GL_DEPTH_COMPONENT, ...) returns an error. For some reason, omitting
          FL_OPENGL3 fixes the issues. That is despite the horizonator using a
          geometry shader, which requires at LEAST opengl 3.2. There's a
          related-looking bug report:

            https://github.com/fltk/fltk/issues/1005

          but the conclusion isn't clear to me. For the time being I simply
          disable FL_OPENGL3, and move on. More investigation and maybe a good
          bug report would be a good thing to do later
        */
        mode(
             // FL_OPENGL3 |
             FL_RGB    |
             FL_DEPTH  |
             FL_DOUBLE
             );
        memset(&m_ctx, 0, sizeof(m_ctx));


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
            if(!horizonator_init( &m_ctx,
                                  g_view.lat, g_view.lon,
                                  NULL,
                                  -1, -1,
                                  radius,
                                  false,
                                  render_texture, SRTM1,
                                  NULL,NULL,
                                  NULL,NULL,
                                  true))
            {
                MSG("horizonator_init() failed. Giving up");
                exit(1);
            }

            if(!horizonator_set_zextents(&m_ctx,
                                         znear, zfar, znear_color, zfar_color))
            {
                MSG("horizonator_set_zextents() failed. Giving up");
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
                const float pixels_to_move_rad = 100.0f;
                const float pixels_to_double = 20.0f;
                if(!pan_and_zoom( Fl::event_dx(), Fl::event_dy(),
                                  g_view.az_radius_deg  / pixels_to_move_rad,
                                  1.f / pixels_to_double))
                {
                    // Error!
                    delete g_window;
                    return 1;
                }
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
                if(!horizonator_pick(&m_ctx,
                                     &g_picked_lat, &g_picked_lon,
                                     Fl::event_x(), Fl::event_y()))
                {
                    g_slippymap_annotations->unset_pick();
                    g_picked_lat = 1e6f;
                    g_picked_lon = 1e6f;
                }
                else
                    g_slippymap_annotations->set_pick(g_picked_lat, g_picked_lon);

                update_status_text();
                g_slippymap->redraw();
            }

            break;

        case FL_DRAG:
            // I pan and zoom with left-click-and-drag
            if(Fl::event_state() & FL_BUTTON1)
            {
                const float deg_per_pixel = 2.f*g_view.az_radius_deg/(float)pixel_w();
                const float pixels_to_double = 200.0f;
                if(!pan_and_zoom( Fl::event_x() - m_last_drag_update_xy[0],
                                  Fl::event_y() - m_last_drag_update_xy[1],
                                  -deg_per_pixel,
                                  1.f / pixels_to_double  ))
                {
                    // Error!
                    delete g_window;
                    return 1;
                }

                m_last_drag_update_xy[0] = Fl::event_x();
                m_last_drag_update_xy[1] = Fl::event_y();
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
        "%s [--texture] [--SRTM1]\n"
        "   [--radius RENDER_RADIUS_CELLS]\n"
        "   [--zfar        ZFAR]\n"
        "   [--znear-color ZNEARCOLOR]\n"
        "   [--zfar-color  ZFARCOLOR]\n"
        "   LAT LON [AZ_CENTER_DEG AZ_RADIUS_DEG]\n"
        "\n"
        "This is an interactive tool, so the viewer position and azimuth bounds\n"
        "given on the commandline are just starting points. The viewer position\n"
        "is required, but the azimuth bounds may be omitted; some reasonable\n"
        "defaults will be used."
        "\n"
        "By default I load 1000 of the DEM in each direction from the center\n"
        "point. If --radius is given, I use that value instead\n"
        "\n"
        "The extents of ranges that we render are given by --znear and --zfar,\n"
        "in meters. Anything closer than --znear and further out than --zfar will\n"
        "not be rendered. The color-coding extents are given by --znear-color and\n"
        "--zfar-color. Anything at or closer than --znear-color will be rendered\n"
        "as black, and anything at or further than --zfar-color will be rendered\n"
        "as red. Of --zfar-color/--znear-color are omitted, they take the same value\n"
        "as --zfar/--znear. --znear and --zfar have  reasonable defaults, and may also\n"
        "be omitted\n"
        "\n"
        "By default we colorcode the renders by range. If --texture, we\n"
        "use a set of image tiles to texture the render instead\n"
        "\n"
        "By default we use 3\" SRTM data. Currently every triangle in the grid is\n"
        "rendered. This is inefficient, but the higher-resolution 1\" SRTM tiles\n"
        "would make it use 9 times more memory and computational resources, so\n"
        "sticking with the lower-resolution 3\" SRTM data is recommended for now\n";

    struct option opts[] = {
        { "texture",           no_argument,       NULL, 'T' },
        { "SRTM1",             no_argument,       NULL, 'S' },
        { "radius",            required_argument, NULL, 'R' },
        { "znear",             required_argument, NULL, '1' },
        { "zfar",              required_argument, NULL, '2' },
        { "znear-color",       required_argument, NULL, '3' },
        { "zfar-color",        required_argument, NULL, '4' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };


    bool render_texture = false;
    bool SRTM1          = false;

    int   render_radius_cells = 1000;
    float znear       = HORIZONATOR_ZNEAR_DEFAULT;
    float zfar        = HORIZONATOR_ZFAR_DEFAULT;
    float znear_color = -1.f;
    float zfar_color  = -1.f;

    int opt;
    do
    {
        // "h" means -h does something
        opt = getopt_long(argc, argv, "+h", opts, NULL);
        switch(opt)
        {
        case -1:
            break;

        case 'h':
            printf(usage, argv[0]);
            return 0;

        case 'T':
            render_texture = true;
            break;

        case 'S':
            SRTM1 = true;
            break;

        case 'R':
            render_radius_cells = atoi(optarg);
            if(render_radius_cells <= 0)
            {
                fprintf(stderr, "--radius must have an integer argument > 0\n");
                return 1;
            }
            break;

        case '1':
            znear = (float)atof(optarg);
            if(znear <= 0.0f)
            {
                fprintf(stderr, "--znear must have an float argument > 0\n");
                return 1;
            }
            break;
        case '2':
            zfar = (float)atof(optarg);
            if(zfar <= 0.0f)
            {
                fprintf(stderr, "--zfar must have an float argument > 0\n");
                return 1;
            }
            break;
        case '3':
            znear_color = (float)atof(optarg);
            if(znear_color <= 0.0f)
            {
                fprintf(stderr, "--znear-color must have an float argument > 0\n");
                return 1;
            }
            break;
        case '4':
            zfar_color = (float)atof(optarg);
            if(zfar_color <= 0.0f)
            {
                fprintf(stderr, "--zfar-color must have an float argument > 0\n");
                return 1;
            }
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

    if(znear_color < 0.f) znear_color = znear;
    if(zfar_color  < 0.f) zfar_color  = zfar;

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

    // The slippy-map and the render are in one group, and the status bar is in
    // the other group. I set the first group to be resizeable, so the status
    // bar always remains the same size
    Fl_Group* map_and_render =
        new Fl_Group(0,0,
                     WINDOW_W, WINDOW_H-STATUS_H);
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

        g_slippymap->center_at(g_view.lat, g_view.lon);
    }
    {
        g_gl_widget = new GLWidget(0, map_h,
                                   g_window->w(), g_window->h()-map_h-STATUS_H,
                                   render_radius_cells,
                                   render_texture, SRTM1,
                                   znear,zfar,znear_color,zfar_color);
    }
    map_and_render->end();

    {
        Fl_Group* group_status = new Fl_Group(0, g_window->h()-STATUS_H,
                                              g_window->w(), STATUS_H);

        g_status_text = new Fl_Output(0, g_window->h()-STATUS_H,
                                      g_window->w() - COPY_BUTTON_W, STATUS_H);

        Fl_Button* button_copy_cmd = new Fl_Button(g_window->w() - COPY_BUTTON_W, g_window->h()-STATUS_H,
                                                   COPY_BUTTON_W, STATUS_H,
                                                   "Copy command");
        button_copy_cmd->callback(cb_copy_command);

        group_status->resizable(g_status_text);
        group_status->end();

        update_status_text();
    }


    orb_osmlayer* osmlayer  = new orb_osmlayer;
    g_slippymap_annotations = new SlippymapAnnotations(&g_view, g_gl_widget->ctx());
    osmlayer->callback( &redraw_slippymap, g_slippymap );

    layers.push_back(osmlayer);
    layers.push_back(g_slippymap_annotations);
    g_slippymap->layers(layers);

    g_slippymap->callback( &callback_slippymap, (void*)g_gl_widget );

    g_window->resizable(map_and_render);
    g_window->end();
    g_window->show();

    Fl::run();

    return 0;
}
