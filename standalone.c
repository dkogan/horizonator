#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <FreeImage.h>
#include <math.h>
#include <epoxy/gl.h>
#include <GL/freeglut.h>

#include "horizonator.h"
#include "annotator.h"
#include "util.h"

static bool glut_loop( bool render_texture, bool SRTM1,
                       float viewer_lat, float viewer_lon,

                       // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                       // edges lie at the edges of the image. So for an image that's
                       // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                       // elevation extents will be chosen to keep the aspect ratio
                       // square.
                       float az_deg0, float az_deg1,
                       int render_radius_cells,

                       // rendering and color-coding boundaries. Set to <=0 for
                       // defaults
                       float znear,       float zfar,
                       float znear_color, float zfar_color,

                       const char* dir_dems,
                       const char* dir_tiles,
                       const char* tiles_name,
                       const char* tiles_url_fmt,
                       bool allow_downloads)
{
    horizonator_context_t ctx;

    if( !horizonator_init( &ctx,
                           viewer_lat, viewer_lon,
                           NULL,
                           -1, -1,
                           render_radius_cells,
                           true,
                           render_texture, SRTM1,
                           dir_dems,
                           dir_tiles,
                           tiles_name,
                           tiles_url_fmt,
                           allow_downloads) )
        return false;

    if(!horizonator_set_zextents(&ctx,
                                 znear, zfar, znear_color, zfar_color))
       return false;

    if(!horizonator_pan_zoom( &ctx, az_deg0, az_deg1))
        return false;

    void window_display(void)
    {
        horizonator_redraw(&ctx);
        glutSwapBuffers();
    }

    GLenum winding = GL_CCW;
    const GLenum polygon_modes[] = {GL_FILL, GL_LINE, GL_POINT};
    int polygon_mode_idx = 0;
    void window_keyPressed(unsigned char key,
                           int x __attribute__((unused)) ,
                           int y __attribute__((unused)) )
    {
        switch (key)
        {
        case 'w':
            ;
            if(++polygon_mode_idx == sizeof(polygon_modes)/sizeof(polygon_modes[0]))
                polygon_mode_idx = 0;

            glPolygonMode(GL_FRONT_AND_BACK, polygon_modes[ polygon_mode_idx ] );
            break;

        case 'r':
            if (winding == GL_CCW) winding = GL_CW;
            else                   winding = GL_CCW;
            glFrontFace(winding);
            break;

        case 'q':
        case 27:
            // Need both to avoid a segfault. This works differently with
            // different opengl drivers
            glutExit();
            exit(0);
        }

        glutPostRedisplay();
    }

    void _horizonator_resized(int width, int height)
    {
        horizonator_resized(&ctx, width, height);
    }

    glutDisplayFunc (window_display);
    glutKeyboardFunc(window_keyPressed);
    glutReshapeFunc (_horizonator_resized);

    glutMainLoop();

    return true;
}

int main(int argc, char* argv[])
{
    const char* usage =
        "%s [--width WIDTH_PIXELS] [--height HEIGHT_PIXELS]\n"
        "   [--image OUT.png|OUT.pdf|OUT.svg]\n"
        "   [--radius RENDER_RADIUS_CELLS]\n"
        "   [--texture] [--SRTM1]\n"
        "   [--allow-tile-downloads]\n"
        "   [--znear       ZNEAR]\n"
        "   [--zfar        ZFAR]\n"
        "   [--znear-color ZNEARCOLOR]\n"
        "   [--zfar-color  ZFARCOLOR]\n"
        "   [--dirdems DIRECTORY]\n"
        "   [--dirtiles DIRECTORY]\n"
        "   [--tiles NAME=FMT]\n"
        "   LAT LON AZ_DEG0 AZ_DEG1\n"
        "\n"
        "By default, we render to a window. If --width is given, we render\n"
        "to an image (--image) instead.\n"
        "--height applies only if --width is given, and is optional; a reasonable\n"
        "field-of-view will be assumed if --height is omitted."
        "If we're annotating a --image, you can pass --cut-off-bottom-px to cut off\n"
        "the given number of pixels from the bottom. This is a workaround for the\n"
        "uneven edges of the render at the bottom\n"
        "\n"
        "The image filename MUST be a .png file (the render will be written)\n"
        "OR a .pdf or .svg file (the annotated render will be written)\n"
        "\n"
        "By default I load 1000 of the DEM in each direction from the center\n"
        "point. If --radius is given, I use that value instead\n"
        "\n"
        "When plotting to a window, AZ_DEG are the azimuth bounds of the\n"
        "VIEWPORT. When rendering to an image, AZ_DEG are the\n"
        "centers of the first and last pixels. This is slightly smaller\n"
        "than the whole viewport: there's one extra pixel on each side\n"
        "\n"
        "The extents of ranges that we render are given by --zmin and --zmax,\n"
        "in meters. Anything closer than --zmin and further out than --zmax will\n"
        "not be rendered. The color-coding extents are given by --zmin-color and\n"
        "--zmax-color. Anything at or closer than --zmin-color will be rendered\n"
        "as black, and anything at or further than --zmax-color will be rendered\n"
        "as red. All 4 of these have reasonable defaults, and may be omitted\n"
        "\n"
        "By default we colorcode the renders by range. If --texture, we\n"
        "use a set of image tiles to texture the render instead\n"
        "\n"
        "By default we use 3\" SRTM data. Currently every triangle in the grid is\n"
        "rendered. This is inefficient, but the higher-resolution 1\" SRTM tiles\n"
        "would make it use 9 times more memory and computational resources, so\n"
        "sticking with the lower-resolution 3\" SRTM data is recommended for now.\n"
        "\n"
        "The DEMs are in the directory given by --dirdems, or in\n"
        "~/.horizonator/DEMs_SRTM3/ (or DEMs_SRTM1) if omitted.\n"
        "\n"
        "The tiles are in the directory given by --dirtiles, or in\n"
        "~/.horizonator/tiles if omitted. This is the BASE directory for ALL the\n"
        "available tile sets. By default we use the OSM mapnik tiles. To specify\n"
        "different tiles, pass '--tiles NAME=FMT'. Where NAME is the identifier of\n"
        "this set and FMT is the URL format string to use for this set\n";

    struct option opts[] = {
        { "width",             required_argument, NULL, 'w' },
        { "height",            required_argument, NULL, 'H' },
        { "cut-off-bottom-px", required_argument, NULL, 'c' },
        { "image",             required_argument, NULL, 'i' },
        { "radius",            required_argument, NULL, 'R' },
        { "dirdems",           required_argument, NULL, 'd' },
        { "dirtiles",          required_argument, NULL, 't' },
        { "tiles",             required_argument, NULL, 'I' },
        { "texture",           no_argument,       NULL, 'T' },
        { "SRTM1",             no_argument,       NULL, 'S' },
        { "allow-tile-downloads",no_argument,     NULL, 'a' },
        { "znear",             required_argument, NULL, '1' },
        { "zfar",              required_argument, NULL, '2' },
        { "znear-color",       required_argument, NULL, '3' },
        { "zfar-color",        required_argument, NULL, '4' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };

    int         width               = 0;
    int         height              = 0;
    int         cut_off_bottom_px   = 0;
    const char* filename_image      = NULL;
    const char* dir_dems            = NULL;
    const char* dir_tiles           = NULL;
    const char* tiles_name          = NULL;
    const char* tiles_url_fmt       = NULL;
    bool        render_texture      = false;
    bool        SRTM1               = false;
    bool        allow_downloads     = false;
    int         render_radius_cells = 1000;

    float znear       = -1.0f;
    float zfar        = -1.0f;
    float znear_color = -1.0f;
    float zfar_color  = -1.0f;

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

        case 'w':
            width = atoi(optarg);
            if(width <= 0)
            {
                fprintf(stderr, "--width must have an integer argument > 0\n");
                return 1;
            }
            break;

        case 'H':
            height = atoi(optarg);
            if(height <= 0)
            {
                fprintf(stderr, "--height must have an integer argument > 0\n");
                return 1;
            }
            break;

        case 'c':
            cut_off_bottom_px = atoi(optarg);
            if(cut_off_bottom_px < 0)
            {
                fprintf(stderr, "--cut-off-bottom-px must have an integer argument >= 0\n");
                return 1;
            }
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

        case 'i':
            filename_image = optarg;
            break;

        case 'd':
            dir_dems = optarg;
            break;

        case 't':
            dir_tiles = optarg;
            break;

        case 'I':
            // --tiles NAME=FMT into tiles_name, tiles_url_fmt
            char* eq = strchr(optarg,'=');
            if(eq == NULL)
            {
                MSG("Couldn't find '=' in --tiles");
                return 1;
            }
            *eq = '\0';
            tiles_name = optarg;
            tiles_url_fmt = &eq[1];
            break;

        case 'T':
            render_texture = true;
            break;

        case 'S':
            SRTM1 = true;
            break;

        case 'a':
            allow_downloads = true;
            break;

        case '?':
            fprintf(stderr, "Unknown option\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    } while( opt != -1 );

    int Nargs_remaining = argc-optind;
    if( Nargs_remaining != 4 )
    {
        fprintf(stderr, "Need exactly 4 non-option arguments. Got %d\n\n",Nargs_remaining);
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    if(width >  0 && filename_image == NULL)
    {
        fprintf(stderr, "--width makes sense only with --image\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if(width <= 0 &&  filename_image != NULL)
    {
        fprintf(stderr, "--width required if --image\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if( height > 0 && width <= 0 )
    {
        fprintf(stderr, "--height makes sense only with --width\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    const int strlen_filename_image = strlen(filename_image);
    if(filename_image != NULL)
    {
        if(!(strlen_filename_image >= 5 &&
             (0 == strcasecmp(".png", &filename_image[strlen_filename_image-4]) ||
              0 == strcasecmp(".pdf", &filename_image[strlen_filename_image-4]) ||
              0 == strcasecmp(".svg", &filename_image[strlen_filename_image-4]))))
        {
            fprintf(stderr, "--image MUST be given a '.png' or '.pdf' or '.svg' filename\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    }

    float lat     = (float)atof(argv[optind+0]);
    float lon     = (float)atof(argv[optind+1]);
    float az_deg0 = (float)atof(argv[optind+2]);
    float az_deg1 = (float)atof(argv[optind+3]);

    if( lat < -80.f  || lat > 80.f )
    {
        fprintf(stderr, "Got invalid latitude");
        return false;

    }
    if( lon < -180.f || lon > 180.f )
    {
        fprintf(stderr, "Got invalid longitude");
        return false;

    }

    if(az_deg0 > az_deg1)
    {
        fprintf(stderr, "MUST have az_deg0 < az_deg1\n");
        return 1;
    }

    if(filename_image == NULL)
    {
        glut_loop(render_texture, SRTM1,
                  lat, lon, az_deg0, az_deg1,
                  render_radius_cells,
                  znear,zfar,znear_color,zfar_color,
                  dir_dems, dir_tiles,
                  tiles_name, tiles_url_fmt,
                  allow_downloads);
        return 0;
    }


    // The user gave me az_deg referring to the center of the pixels at the
    // edge. I need to convert them to represent the edges of the viewport.
    // That's 0.5 pixels extra on either side
    float az_per_pixel = (az_deg1 - az_deg0) / (float)(width-1);
    az_deg0 -= az_per_pixel/2.f;
    az_deg1 += az_per_pixel/2.f;

    if(height <= 0)
    {
        // Assume a 20deg fov if no height requested
        const float fovy_deg = 20.0f;
        height = (int)roundf( (float)width * fovy_deg / (az_deg1-az_deg0));
    }

    uint8_t* pool = NULL;
    char*    image;
    float* ranges;
    if(filename_image != NULL)
    {
        // rgb for the image and float for the depth
        pool = malloc( width*height * (3 + sizeof(float)) );
        if(pool == NULL)
        {
            MSG("image,ranges buffer malloc() failed");
            return 1;
        }

        ranges = (float*)pool;
        image  = (char*)&pool[width*height*sizeof(float)];
    }


    horizonator_context_t ctx;
    float viewer_z = -1.0f; // this will be filed-in by horizonator_init()
    if( !horizonator_init( &ctx,
                           lat, lon,
                           &viewer_z,
                           width, height,
                           render_radius_cells,
                           true,
                           render_texture, SRTM1,
                           dir_dems, dir_tiles,
                           tiles_name, tiles_url_fmt,
                           allow_downloads) )
    {
        fprintf(stderr, "horizonator_init() failed\n");
        return false;
    }

    if(!horizonator_set_zextents(&ctx,
                                 znear, zfar, znear_color, zfar_color))
        return false;

    if(!horizonator_pan_zoom( &ctx, az_deg0, az_deg1))
    {
        fprintf(stderr, "horizonator_pan_zoom() failed");
        return false;
    }

    if(!horizonator_render_offscreen(&ctx, image, ranges))
    {
        fprintf(stderr, "render failed\n");
        return 1;
    }

    if(filename_image != NULL)
    {
        if(0 == strcasecmp(".png", &filename_image[strlen_filename_image-4]))
        {
            // png file requested. I write out the render only
            FreeImage_Initialise(true);
            FIBITMAP* fib = FreeImage_ConvertFromRawBitsEx(false,
                                                           (BYTE*)image,
                                                           FIT_BITMAP,
                                                           width,
                                                           height-cut_off_bottom_px,
                                                           3*width, 24,
                                                           0,0,0,
                                                           // Top row is stored first
                                                           true);

            if(!FreeImage_Save(FIF_PNG, fib, filename_image, 0))
            {
                fprintf(stderr, "Couldn't save to '%s'\n", filename_image);
                return 1;
            }
            FreeImage_Unload(fib);
            FreeImage_DeInitialise();
        }
        else
        {
            // pdf file is requested. I write an annotated pdf
            poi_t pois[] = {
// ./query-peaks-from-osm.py 34. -118 100000 > socal-peaks.h
#include "socal-peaks.h"
            };
            const int N_pois = (int)(sizeof(pois) / sizeof(pois[0]));

            annotate(filename_image,
                     (uint8_t*)image, ranges, width, height, cut_off_bottom_px,
                     pois, N_pois,
                     lat, lon,
                     az_deg0, az_deg1,
                     viewer_z);
        }

        free(pool);
    }

    return 0;
}
