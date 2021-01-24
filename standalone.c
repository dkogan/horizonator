#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <FreeImage.h>
#include <math.h>

#include "horizonator.h"

int main(int argc, char* argv[])
{
    const char* usage =
        "%s [--width WIDTH_PIXELS] [--output OUT.png]\n"
        "   [--texture]\n"
        "   [--allow-tile-downloads]\n"
        "   [--dirdems DIRECTORY]\n"
        "   [--dirtiles DIRECTORY]\n"
        "   LAT LON AZ_DEG0 AZ_DEG1\n"
        "\n"
        "By default, we render to a window. If --width and --output\n"
        "are given, we render to an image on disk instead. --width and\n"
        "--output must appear together. The output filename MUST be\n"
        "a .png file\n"
        "\n"
        "When plotting to a window, AZ_DEG are the azimuth bounds of the\n"
        "VIEWPORT. When rendering to an image, AZ_DEG are the\n"
        "centers of the first and last pixels. This is slightly smaller\n"
        "than the whole viewport: there's one extra pixel on each side\n"
        "\n"
        "By default we colorcode the renders by range. If --texture, we\n"
        "use a set of image tiles to texture the render instead\n"
        "\n"
        "The DEMs are in the directory given by --dirdems, or in\n"
        "~/.horizonator/DEMs_SRTM3/ if omitted.\n"
        "\n"
        "The tiles are in the directory given by --dirtiles, or in\n"
        "~/.horizonator/tiles if omitted.\n";

    struct option opts[] = {
        { "width",             required_argument, NULL, 'w' },
        { "output",            required_argument, NULL, 'o' },
        { "dirdems",           required_argument, NULL, 'd' },
        { "dirtiles",          required_argument, NULL, 't' },
        { "texture",           no_argument,       NULL, 'T' },
        { "allow-tile-downloads",no_argument,     NULL, 'a' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };


    int         width           = 0;
    const char* output          = NULL;
    const char* dir_dems        = "~/.horizonator/DEMs_SRTM3";
    const char* dir_tiles       = "~/.horizonator/tiles";
    bool        render_texture  = false;
    bool        allow_downloads = false;

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

        case 'w':
            width = atoi(optarg);
            break;

        case 'o':
            output = optarg;
            break;

        case 'd':
            dir_dems = optarg;
            break;

        case 't':
            dir_tiles = optarg;
            break;

        case 'T':
            render_texture = true;
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

    if( (width >  0 && output == NULL) ||
        (width <= 0 && output != NULL) )
    {
        fprintf(stderr, "Either both or neither of --width and --output must be given\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    if(output != NULL)
    {
        int l = strlen(output);
        if(l < 5 || 0 != strcasecmp(".png", &output[l-4]))
        {
            fprintf(stderr, "--output MUST be given a '.png' filename\n\n");
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

    if(output == NULL)
    {
        horizonator_allinone_glut_loop(render_texture,
                                       lat, lon, az_deg0, az_deg1,
                                       dir_dems, dir_tiles,
                                       allow_downloads);
        return 0;
    }


    // The user gave me az_deg referring to the center of the pixels at the
    // edge. I need to convert them to represent the edges of the viewport.
    // That's 0.5 pixels extra on either side
    float az_per_pixel = (az_deg1 - az_deg0) / (float)(width-1);
    az_deg0 -= az_per_pixel/2.f;
    az_deg1 += az_per_pixel/2.f;

    const float fovy_deg = 20.0f;
    int height = (int)roundf( (float)width * fovy_deg / (az_deg1-az_deg0));

    char* image =
        horizonator_allinone_render_to_image(render_texture,
                                             lat, lon, az_deg0, az_deg1,
                                             width, height,
                                             dir_dems, dir_tiles,
                                             allow_downloads);
    if(image == NULL)
    {
        fprintf(stderr, "Image render failed\n");
        return 1;
    }

    FreeImage_Initialise(true);

    FIBITMAP* fib = FreeImage_ConvertFromRawBitsEx(false,
                                                   (BYTE*)image,
                                                   FIT_BITMAP,
                                                   width, height,
                                                   3*width, 24,
                                                   0,0,0,
                                                   false);

    int result = 0;
    if(!FreeImage_Save(FIF_PNG, fib, output, 0))
    {
        fprintf(stderr, "Couldn't save to '%s'\n", output);
        result = 1;
    }
    FreeImage_Unload(fib);
    FreeImage_DeInitialise();

    free(image);

    return result;
}
