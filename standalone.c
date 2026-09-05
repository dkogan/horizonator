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

// Post-processing pass, working directly off the range (depth) buffer:
// darkens to a dark gray any pixel that sits at a depth discontinuity --
// either terrain against the sky, or one terrain surface abruptly giving
// way to another, much nearer or farther one behind a gap (a col, a
// notch...). This traces a crisp line along the crest of each visible
// ridge/mountain range "layer", the way classic drawn panoramas (e.g.
// udeuschle.de) do it. Only checks vertically (row to row): ridge crests
// are near-horizontal features in a panorama, so this is where the sharp
// depth jumps are.
//
// The jump is judged as an absolute distance, in meters: at a fixed
// threshold, grazing, near-horizontal sightlines (typically towards the
// bottom of the frame, at low elevation angles) produce many more of
// these lines than steeper sightlines towards distant peaks, since a
// one-pixel vertical step there corresponds to a much bigger jump in true
// distance. This is no different than in a real drawn panorama: nearby
// terrain, seen at a grazing angle, really does present many more
// distinct ridgelets per degree of elevation than distant terrain does
static void draw_ridge_outlines(uint8_t* image, const float* ranges,
                                int width, int height,
                                float threshold_m,
                                float ridge_line_gray)
{
    const uint8_t gray_u8 = (uint8_t)(ridge_line_gray*255.0f + 0.5f);

    for(int x=0; x<width; x++)
        for(int y=1; y<height; y++)
        {
            float r0 = ranges[(size_t)(y-1)*width + x]; // pixel above (towards the sky)
            float r1 = ranges[(size_t)y    *width + x]; // this pixel

            if(r1 <= 0.0f)
                // this pixel has no render data (sky/background): nothing
                // to outline here
                continue;

            bool edge =
                (r0 <= 0.0f) ||               // terrain appearing out of the sky
                (fabsf(r1-r0) > threshold_m); // one surface replaced by another

            if(edge)
            {
                uint8_t* px = &image[((size_t)y*width + x)*3];
                px[0] = px[1] = px[2] = gray_u8;
            }
        }
}

static bool glut_loop( bool render_texture, bool SRTM1,
                       float viewer_lat, float viewer_lon,
                       float viewer_height_m,
                       bool curvature_enabled, float refraction_k,
                       bool restrict_mesh_azimuth,

                       // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                       // edges lie at the edges of the image. So for an image that's
                       // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                       // elevation extents will be chosen to keep the aspect ratio
                       // square.
                       float az_deg0, float az_deg1,

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

    float viewer_z = -1.0f;
    if( !horizonator_init( &ctx,
                           viewer_lat, viewer_lon,
                           &viewer_z,
                           -1, -1,
                           -1, zfar,
                           restrict_mesh_azimuth, az_deg0, az_deg1,
                           true,
                           render_texture, SRTM1,
                           dir_dems,
                           dir_tiles,
                           tiles_name,
                           tiles_url_fmt,
                           allow_downloads) )
        return false;

    if(viewer_height_m != 0.0f)
    {
        // horizonator_init() auto-selected a viewer_z sitting on the DEM
        // ground surface. I add the height of the observer above that
        // ground (e.g. the floor of an apartment building) and re-apply it.
        viewer_z += viewer_height_m;
        if(!horizonator_move(&ctx, &viewer_z, viewer_lat, viewer_lon))
            return false;
    }

    if(!horizonator_set_curvature(&ctx, curvature_enabled, refraction_k))
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
        "   [--image OUT.png|OUT.pdf|OUT.svg] [--cut-off-bottom-px N]\n"
        "   [--texture] [--SRTM1]\n"
        "   [--allow-tile-downloads]\n"
        "   [--viewer-height METERS]\n"
        "   [--curvature] [--refraction-k K]\n"
        "   [--no-restrict-mesh-azimuth]\n"
        "   [--no-ridge-lines] [--ridge-line-threshold METERS] [--ridge-line-gray FRACTION]\n"
        "   [--znear ZNEAR] [--zfar ZFAR] [--znear-color ZNEARCOLOR] [--zfar-color ZFARCOLOR]\n"
        "   [--dirdems DIRECTORY] [--dirtiles DIRECTORY] [--tiles NAME=FMT]\n"
        "   LAT LON AZ_CENTER_DEG AZ_RADIUS_DEG\n"
        "\n"
        "=== Basic operation ===\n"
        "\n"
        "By default, we render to a window. If --width is given, we render\n"
        "to an image (--image) instead. --height applies only if --width is\n"
        "given, and is optional; a reasonable field-of-view is assumed if\n"
        "--height is omitted (see FIELD OF VIEW below).\n"
        "\n"
        "LAT, LON are the coordinates of the viewer, in degrees. AZ_CENTER_DEG\n"
        "and AZ_RADIUS_DEG give the azimuth window we look at: from\n"
        "AZ_CENTER_DEG-AZ_RADIUS_DEG to AZ_CENTER_DEG+AZ_RADIUS_DEG (0 = North,\n"
        "90 = East). When plotting to a window, these describe the azimuth\n"
        "bounds of the VIEWPORT; when rendering to an image, the centers of\n"
        "the first and last pixels (slightly narrower than the viewport: one\n"
        "extra half-pixel on each side).\n"
        "\n"
        "The image filename MUST be a .png file (the render alone is written)\n"
        "OR a .pdf or .svg file (the render, annotated with named peaks and\n"
        "bearing markers, is written -- see PEAK LABELS below). If annotating,\n"
        "--cut-off-bottom-px N discards the bottom N pixels of the render; a\n"
        "workaround for the uneven edges some renders have at the bottom.\n"
        "\n"
        "=== Field of view: what to render, and how much of it fills the frame ===\n"
        "\n"
        "--znear/--zfar (meters) bound how much of the terrain is rendered:\n"
        "closer than --znear or farther than --zfar is not drawn at all. Both\n"
        "have reasonable defaults and may be omitted. Raising --znear is a\n"
        "good way to skip an uninteresting nearby plain, dedicating the whole\n"
        "frame to the distant relief that's actually worth looking at.\n"
        "\n"
        "The image's vertical field of view is NOT set directly: it falls out\n"
        "of --width, --height and AZ_RADIUS_DEG, because the same angular\n"
        "resolution (degrees/pixel) is used both horizontally and vertically:\n"
        "  vertical_FOV_deg = 2*AZ_RADIUS_DEG * height/width\n"
        "A real mountain panorama usually only occupies a few degrees of\n"
        "elevation above/below the horizontal, so with the default --height\n"
        "(a 20deg half-angle assumed when omitted) most of the frame ends up\n"
        "as empty sky. Passing an explicit, small --height (to get a vertical\n"
        "FOV of, say, 5-10deg) both crops that away AND increases the\n"
        "effective resolution devoted to the relief that matters -- this is\n"
        "the single biggest lever for a sharp-looking render. The same logic\n"
        "applies to AZ_RADIUS_DEG: narrowing it, at a fixed --width, increases\n"
        "the angular resolution (degrees/pixel) of that slice.\n"
        "\n"
        "=== Earth curvature and atmospheric refraction ===\n"
        "\n"
        "By default we render assuming a flat tangent plane, ignoring the\n"
        "curvature of the Earth -- a reasonable approximation at short range,\n"
        "but at 100+km (distant high peaks) the apparent elevation angle can\n"
        "be off by upwards of a kilometer of apparent height. Pass --curvature\n"
        "to correct for it:\n"
        "  drop = (1-k) * distance^2 / (2*Rearth)\n"
        "subtracted from the apparent height of each rendered point.\n"
        "--refraction-k sets the refraction coefficient k (default 0.13, a\n"
        "commonly-used value; see udeuschle.de). It is ignored unless\n"
        "--curvature is also given. k=0 is pure geometric curvature, with no\n"
        "refraction compensation (peaks look their lowest); increasing k\n"
        "raises them back up somewhat, closer to the flat-plane render.\n"
        "\n"
        "=== Color and ridge outlines ===\n"
        "\n"
        "By default the render is color-coded by range: near terrain is drawn\n"
        "in a dark gray, far terrain in a light gray, converging towards the\n"
        "white background at --zfar-color. --znear-color/--zfar-color set the\n"
        "distance extents used for this (in meters); if omitted, they take\n"
        "the same values as --znear/--zfar.\n"
        "\n"
        "On top of that, the crest of each visible ridge/mountain-range\n"
        "\"layer\" is outlined, the way classic drawn panoramas (e.g.\n"
        "udeuschle.de) do it: a post-process on the range image darkens any\n"
        "pixel that sits at a sharp depth discontinuity -- terrain against\n"
        "the sky, or one surface abruptly giving way to a much nearer/farther\n"
        "one behind a gap. Pass --no-ridge-lines to disable this.\n"
        "--ridge-line-threshold (meters, default 500) sets how big a range\n"
        "jump between vertically-adjacent pixels counts as a new layer: lower\n"
        "values pick out more (and finer) ridgelets, especially at low\n"
        "elevation angles / grazing sightlines near the bottom of the frame,\n"
        "where a one-pixel step naturally corresponds to a much bigger jump\n"
        "in true distance than the same step towards a distant peak; this is\n"
        "expected, not a bug, and mirrors what a real drawn panorama looks\n"
        "like up close. --ridge-line-gray (0-1, default 0.15) sets how dark\n"
        "the outline is: 0 is black, 1 is white. Keep it darker than the\n"
        "nearest terrain gray so it stays visible against it.\n"
        "\n"
        "If --texture, we use a set of image tiles to texture the render\n"
        "instead of any of the above color-coding.\n"
        "\n"
        "=== Peak labels (PDF/SVG output only) ===\n"
        "\n"
        "See query-peaks-from-osm.py to generate the compiled-in list of\n"
        "named peaks (a poi_t[] #included by this file -- edit the #include\n"
        "line to point at your own generated file). Only peaks that are (a)\n"
        "actually visible in the rendered range image, (b) farther than 500m,\n"
        "and (c) no farther than --zfar are labelled. --viewer-height and\n"
        "--curvature/--refraction-k affect where a peak is predicted to land\n"
        "on screen; this tool always uses the same values for the render and\n"
        "for the labelling, so there's nothing extra to keep in sync here.\n"
        "\n"
        "=== Viewer position ===\n"
        "\n"
        "--viewer-height adds the given number of meters to the viewer\n"
        "elevation sampled from the DEM at LAT,LON (e.g. the height of an\n"
        "apartment floor above street level). Defaults to 0.\n"
        "\n"
        "=== Performance ===\n"
        "\n"
        "By default we use 3\" SRTM data, and only mesh the AZ_CENTER_DEG +-\n"
        "AZ_RADIUS_DEG wedge that's actually being rendered (plus a small\n"
        "margin), rather than the full circle of loaded DEM data (radius\n"
        "--zfar) -- this cuts the triangle count, and so the render time,\n"
        "without changing the output, since this tool always renders the\n"
        "same wedge it was given on the commandline. Pass\n"
        "--no-restrict-mesh-azimuth to mesh the full circle instead (slower;\n"
        "only useful for debugging).\n"
        "\n"
        "The higher-resolution 1\" SRTM tiles can be selected with --SRTM1;\n"
        "this gives a 9x increase in triangle count (currently every\n"
        "triangle in the mesh is rendered, however far away and however tiny\n"
        "on screen), so it can easily overload the machine. Stick with the\n"
        "default 3\" data unless you need the extra resolution for very\n"
        "close terrain.\n"
        "\n"
        "=== Data sources ===\n"
        "\n"
        "The DEMs are in the directory given by --dirdems, or in\n"
        "~/.horizonator/DEMs_SRTM3/ (or DEMs_SRTM1) if omitted.\n"
        "\n"
        "The tiles are in the directory given by --dirtiles, or in\n"
        "~/.horizonator/tiles if omitted. This is the BASE directory for ALL\n"
        "the available tile sets. By default we use the OSM mapnik tiles. To\n"
        "specify different tiles, pass '--tiles NAME=FMT', where NAME is the\n"
        "identifier of this set and FMT is the URL format string to use for\n"
        "this set. --allow-tile-downloads lets missing tiles be fetched over\n"
        "the network; without it, a missing tile is just left blank.\n";

    struct option opts[] = {
        { "width",             required_argument, NULL, 'w' },
        { "height",            required_argument, NULL, 'H' },
        { "cut-off-bottom-px", required_argument, NULL, 'c' },
        { "image",             required_argument, NULL, 'i' },
        { "dirdems",           required_argument, NULL, 'd' },
        { "dirtiles",          required_argument, NULL, 't' },
        { "tiles",             required_argument, NULL, 'I' },
        { "texture",           no_argument,       NULL, 'T' },
        { "SRTM1",             no_argument,       NULL, 'S' },
        { "allow-tile-downloads",no_argument,     NULL, 'a' },
        { "viewer-height",     required_argument, NULL, 'V' },
        { "curvature",         no_argument,       NULL, 'C' },
        { "refraction-k",      required_argument, NULL, 'k' },
        { "no-restrict-mesh-azimuth", no_argument, NULL, 'M' },
        { "no-ridge-lines",     no_argument,       NULL, 'R' },
        { "ridge-line-threshold", required_argument, NULL, 'r' },
        { "ridge-line-gray",    required_argument, NULL, 'g' },
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
    float       viewer_height_m     = 0.0f;
    bool        curvature_enabled   = false;
    float       refraction_k        = 0.13f;
    bool        restrict_mesh_azimuth = true;
    bool        ridge_lines          = true;
    float       ridge_line_threshold_m = 500.0f;
    float       ridge_line_gray      = 0.15f;

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

        case 'V':
            viewer_height_m = (float)atof(optarg);
            break;

        case 'C':
            curvature_enabled = true;
            break;

        case 'k':
            refraction_k = (float)atof(optarg);
            break;

        case 'M':
            restrict_mesh_azimuth = false;
            break;

        case 'R':
            ridge_lines = false;
            break;

        case 'r':
            ridge_line_threshold_m = (float)atof(optarg);
            break;

        case 'g':
            ridge_line_gray = (float)atof(optarg);
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

    if(znear_color < 0.f) znear_color = znear;
    if(zfar_color  < 0.f) zfar_color  = zfar;

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

    float lat           = (float)atof(argv[optind+0]);
    float lon           = (float)atof(argv[optind+1]);
    float az_center_deg = (float)atof(argv[optind+2]);
    float az_radius_deg = (float)atof(argv[optind+3]);

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

    if(filename_image == NULL)
    {
        glut_loop(render_texture, SRTM1,
                  lat, lon,
                  viewer_height_m,
                  curvature_enabled, refraction_k,
                  restrict_mesh_azimuth,
                  az_center_deg-az_radius_deg,
                  az_center_deg+az_radius_deg,
                  znear,zfar,znear_color,zfar_color,
                  dir_dems, dir_tiles,
                  tiles_name, tiles_url_fmt,
                  allow_downloads);
        return 0;
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

    // The user gave me az referring to the center of the pixels at the
    // edge. I need to convert them to represent the edges of the viewport.
    // That's 0.5 pixels extra on either side
    float az_per_pixel = 2.*az_radius_deg / (float)(width-1);
    az_radius_deg += az_per_pixel/2.f;

    if(height <= 0)
    {
        // Assume a 20deg fov if no height requested
        const float fovy_deg = 20.0f;
        height = (int)roundf( (float)width * fovy_deg / az_radius_deg);
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
                           -1, zfar,
                           restrict_mesh_azimuth,
                           az_center_deg-az_radius_deg,
                           az_center_deg+az_radius_deg,
                           true,
                           render_texture, SRTM1,
                           dir_dems, dir_tiles,
                           tiles_name, tiles_url_fmt,
                           allow_downloads) )
    {
        fprintf(stderr, "horizonator_init() failed\n");
        return false;
    }

    if(viewer_height_m != 0.0f)
    {
        // viewer_z was auto-selected by horizonator_init() to sit on the DEM
        // ground surface. I add the height of the observer above that
        // ground (e.g. the floor of an apartment building) and re-apply it.
        viewer_z += viewer_height_m;
        if(!horizonator_move(&ctx, &viewer_z, lat, lon))
        {
            fprintf(stderr, "horizonator_move() failed\n");
            return false;
        }
    }

    if(!horizonator_set_curvature(&ctx, curvature_enabled, refraction_k))
        return false;

    if(!horizonator_set_zextents(&ctx,
                                 znear, zfar, znear_color, zfar_color))
        return false;

    if(!horizonator_pan_zoom( &ctx,
                              az_center_deg-az_radius_deg,
                              az_center_deg+az_radius_deg))
    {
        fprintf(stderr, "horizonator_pan_zoom() failed");
        return false;
    }

    if(!horizonator_render_offscreen(&ctx, image, ranges))
    {
        fprintf(stderr, "render failed\n");
        return 1;
    }

    if(ridge_lines)
        draw_ridge_outlines((uint8_t*)image, ranges, width, height,
                            ridge_line_threshold_m, ridge_line_gray);

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
                     az_center_deg-az_radius_deg,
                     az_center_deg+az_radius_deg,
                     viewer_z,
                     curvature_enabled, refraction_k,
                     zfar);
        }

        free(pool);
    }

    return 0;
}
