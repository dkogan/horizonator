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
                       bool shading_enabled, float sun_az_deg, float sun_el_deg,
                       bool materials_enabled,
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
                       const char* dir_landcover,
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
                           dir_landcover,
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

    if(!horizonator_set_sun(&ctx, shading_enabled, sun_az_deg, sun_el_deg))
        return false;

    if(!horizonator_set_materials(&ctx, materials_enabled))
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
        "   [--shading] [--sun-azimuth DEG] [--sun-elevation DEG]\n"
        "   [--materials]\n"
        "   [--no-restrict-mesh-azimuth]\n"
        "   [--no-ridge-lines] [--ridge-line-threshold METERS] [--ridge-line-gray FRACTION]\n"
        "   [--label-font-size-pt POINTS]\n"
        "   [--list-visible-peaks OUT.txt]\n"
        "   [--znear ZNEAR] [--zfar ZFAR] [--znear-color ZNEARCOLOR] [--zfar-color ZFARCOLOR]\n"
        "   [--dirdems DIRECTORY] [--dirlandcover DIRECTORY]\n"
        "   [--dirtiles DIRECTORY] [--tiles NAME=FMT]\n"
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
        "=== Slope shading ===\n"
        "\n"
        "By default terrain is colored purely by distance (see COLOR below),\n"
        "with no directional lighting. Pass --shading to darken/lighten the\n"
        "relief by a directional light, based on a smoothly-interpolated\n"
        "per-vertex surface normal (estimated from the DEM), giving the\n"
        "relief a 3D appearance. --sun-azimuth (default 135, i.e. SE) and\n"
        "--sun-elevation (default 45deg above the horizon) set the direction\n"
        "the light comes from; both are ignored unless --shading is given.\n"
        "A slope facing away from the sun is dimmed, never made fully black.\n"
        "\n"
        "=== Materials ===\n"
        "\n"
        "By default terrain isn't tinted by land cover, only by distance and\n"
        "(if --shading) slope lighting. Pass --materials to tint it by real\n"
        "land cover instead: forest/grass/bare rock/snow-or-ice/water, from\n"
        "ESA WorldCover tiles (overlaid with Copernicus CORINE Land Cover\n"
        "just for its dedicated glacier class -- WorldCover alone doesn't\n"
        "reliably tell a glacier apart from bare rock). These tiles are\n"
        "prepared offline by build-landcover-tiles.py, not downloaded by\n"
        "this program. Wherever no such data has been prepared for a given\n"
        "point (e.g. build-landcover-tiles.py hasn't been run for that\n"
        "area), --materials falls back to a purely procedural\n"
        "approximation there instead: elevation (snow above a fixed snow\n"
        "line) and slope steepness (bare rock on steep terrain), forest\n"
        "below the tree line and alpine grass above it otherwise -- using\n"
        "no real data, just the DEM already being rendered.\n"
        "\n"
        "=== Color and ridge outlines ===\n"
        "\n"
        "By default the render is color-coded by range (atmospheric\n"
        "perspective): near terrain is drawn in a dark neutral gray, far\n"
        "terrain fades towards a pale blue-gray, closer to the white\n"
        "background, the way haze tints distant relief in a real photo.\n"
        "--znear-color/--zfar-color set the distance extents used for this\n"
        "(in meters); if omitted, they take the same values as --znear/--zfar.\n"
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
        "=== Peak labels ===\n"
        "\n"
        "See query-peaks-from-osm.py to generate the compiled-in list of\n"
        "named peaks (a poi_t[] #included by this file -- edit the #include\n"
        "line to point at your own generated file). A peak counts as visible\n"
        "if it's (a) actually found in the rendered range image (not\n"
        "occluded by closer terrain), (b) farther than 500m, and (c) no\n"
        "farther than --zfar. --viewer-height and --curvature/--refraction-k\n"
        "affect where a peak is predicted to land on screen; this tool\n"
        "always uses the same values for the render and for the labelling,\n"
        "so there's nothing extra to keep in sync here.\n"
        "\n"
        "--list-visible-peaks OUT.txt writes the visible peaks (independent\n"
        "of --image; works even without one) to a tab-separated text file:\n"
        "name, lat, lon, ele_m, range_m, one per line, in no particular\n"
        "order. Peaks with no OSM name fall back to their altitude as a\n"
        "name (e.g. \"1583.0\") -- filter those out downstream by checking\n"
        "for at least one letter, if wanted.\n"
        "\n"
        "With a .pdf/.svg --image, visible peaks are labelled on the\n"
        "render: a black name in vertical text (read by tilting your head\n"
        "to the left: first letter at the bottom, last letter at the top),\n"
        "connected to its peak by a thin almond-green leader line. Each\n"
        "label's last (top) character sits 30px below the top of the\n"
        "canvas, regardless of how far its peak actually is -- so the\n"
        "leader line length alone shows how far up the label had to reach\n"
        "in the sky. Before drawing, peaks whose screen-x positions are\n"
        "closer than --label-font-size-pt*1.5 are considered to conflict;\n"
        "within each conflicting group, only the highest-elevation peak\n"
        "gets a label, the others are dropped entirely (no line, no name).\n"
        "--label-font-size-pt sets the text height in real typographic\n"
        "points, i.e. as it will appear in the output PDF/SVG page (default\n"
        "12); raise it for a legible render at a large --width, or when the\n"
        "page will be viewed/printed at less than 100% zoom -- this also\n"
        "widens the conflict-detection gap, so fewer, larger labels survive.\n"
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
        "The land-cover tiles used by --materials (see MATERIALS above) are\n"
        "in the directory given by --dirlandcover, or in\n"
        "~/.horizonator/landcover/ if omitted; see build-landcover-tiles.py\n"
        "to prepare them. A missing directory, or missing individual tiles,\n"
        "is not an error: --materials just falls back to its procedural\n"
        "approximation wherever real data isn't available.\n"
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
        { "dirlandcover",      required_argument, NULL, 'l' },
        { "dirtiles",          required_argument, NULL, 't' },
        { "tiles",             required_argument, NULL, 'I' },
        { "texture",           no_argument,       NULL, 'T' },
        { "SRTM1",             no_argument,       NULL, 'S' },
        { "allow-tile-downloads",no_argument,     NULL, 'a' },
        { "viewer-height",     required_argument, NULL, 'V' },
        { "curvature",         no_argument,       NULL, 'C' },
        { "refraction-k",      required_argument, NULL, 'k' },
        { "shading",            no_argument,       NULL, 's' },
        { "sun-azimuth",        required_argument, NULL, 'A' },
        { "sun-elevation",      required_argument, NULL, 'E' },
        { "materials",          no_argument,       NULL, 'm' },
        { "no-restrict-mesh-azimuth", no_argument, NULL, 'M' },
        { "no-ridge-lines",     no_argument,       NULL, 'R' },
        { "ridge-line-threshold", required_argument, NULL, 'r' },
        { "ridge-line-gray",    required_argument, NULL, 'g' },
        { "label-font-size-pt", required_argument, NULL, 'F' },
        { "list-visible-peaks", required_argument, NULL, 'L' },
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
    const char* filename_visible_peaks = NULL;
    const char* dir_dems            = NULL;
    const char* dir_landcover       = NULL;
    const char* dir_tiles           = NULL;
    const char* tiles_name          = NULL;
    const char* tiles_url_fmt       = NULL;
    bool        render_texture      = false;
    bool        SRTM1               = false;
    bool        allow_downloads     = false;
    float       viewer_height_m     = 0.0f;
    bool        curvature_enabled   = false;
    float       refraction_k        = 0.13f;
    bool        shading_enabled     = false;
    float       sun_az_deg          = 135.0f;
    float       sun_el_deg          = 45.0f;
    bool        materials_enabled   = false;
    bool        restrict_mesh_azimuth = true;
    bool        ridge_lines          = true;
    float       ridge_line_threshold_m = 500.0f;
    float       ridge_line_gray      = 0.15f;
    float       label_font_size_pt   = 12.0f;

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

        case 'l':
            dir_landcover = optarg;
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

        case 's':
            shading_enabled = true;
            break;

        case 'A':
            sun_az_deg = (float)atof(optarg);
            break;

        case 'E':
            sun_el_deg = (float)atof(optarg);
            break;

        case 'm':
            materials_enabled = true;
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

        case 'F':
            label_font_size_pt = (float)atof(optarg);
            break;

        case 'L':
            filename_visible_peaks = optarg;
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

    if(width >  0 && filename_image == NULL && filename_visible_peaks == NULL)
    {
        fprintf(stderr, "--width makes sense only with --image or --list-visible-peaks\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if(width <= 0 && (filename_image != NULL || filename_visible_peaks != NULL))
    {
        fprintf(stderr, "--width required if --image or --list-visible-peaks\n\n");
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

    if(filename_image == NULL && filename_visible_peaks == NULL)
    {
        glut_loop(render_texture, SRTM1,
                  lat, lon,
                  viewer_height_m,
                  curvature_enabled, refraction_k,
                  shading_enabled, sun_az_deg, sun_el_deg,
                  materials_enabled,
                  restrict_mesh_azimuth,
                  az_center_deg-az_radius_deg,
                  az_center_deg+az_radius_deg,
                  znear,zfar,znear_color,zfar_color,
                  dir_dems, dir_landcover, dir_tiles,
                  tiles_name, tiles_url_fmt,
                  allow_downloads);
        return 0;
    }

    // filename_image can legitimately be NULL here now (--list-visible-peaks
    // without --image); strlen(NULL) is undefined behavior, so this has to
    // stay inside the NULL check, unlike before
    int strlen_filename_image = 0;
    if(filename_image != NULL)
    {
        strlen_filename_image = strlen(filename_image);
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
    if(filename_image != NULL || filename_visible_peaks != NULL)
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
                           dir_dems, dir_landcover, dir_tiles,
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

    if(!horizonator_set_sun(&ctx, shading_enabled, sun_az_deg, sun_el_deg))
        return false;

    if(!horizonator_set_materials(&ctx, materials_enabled))
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

    // Shared by --image (.pdf/.svg) and --list-visible-peaks
    poi_t pois[] = {
// ./query-peaks-from-osm.py 45.77294 4.82993 200000 > lyon-peaks.h
#include "socal-peaks.h"
    };
    const int N_pois = (int)(sizeof(pois) / sizeof(pois[0]));

    if(filename_visible_peaks != NULL)
    {
        visible_poi_t visible[N_pois];
        int Nvisible = find_visible_pois(visible,
                                         ranges, width, height, cut_off_bottom_px,
                                         pois, N_pois,
                                         lat, lon,
                                         az_center_deg-az_radius_deg,
                                         az_center_deg+az_radius_deg,
                                         viewer_z,
                                         curvature_enabled, refraction_k,
                                         zfar);

        FILE* fp = fopen(filename_visible_peaks, "w");
        if(fp == NULL)
        {
            fprintf(stderr, "Couldn't open '%s' for writing\n", filename_visible_peaks);
            return 1;
        }
        // name<TAB>lat<TAB>lon<TAB>ele_m<TAB>range_m, one visible peak per
        // line, unsorted. See cluster-visible-peaks.py for a companion
        // script that reads this format
        for(int i=0; i<Nvisible; i++)
        {
            const poi_t* poi = &pois[ visible[i].poi_index ];
            fprintf(fp, "%s\t%.6f\t%.6f\t%.1f\t%.1f\n",
                    poi->name, poi->lat, poi->lon, poi->ele_m, visible[i].range);
        }
        fclose(fp);
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
            annotate(filename_image,
                     (uint8_t*)image, ranges, width, height, cut_off_bottom_px,
                     pois, N_pois,
                     lat, lon,
                     az_center_deg-az_radius_deg,
                     az_center_deg+az_radius_deg,
                     viewer_z,
                     curvature_enabled, refraction_k,
                     zfar,
                     label_font_size_pt);
        }
    }

    free(pool);

    return 0;
}
