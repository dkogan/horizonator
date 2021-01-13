#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <FreeImage.h>
#include <math.h>

#include "render_terrain.h"

int main(int argc, char* argv[])
{
    const char* usage =
        "%s [--window] [--width WIDTH_PIXELS] [--output OUT.png]\n"
                "LAT LON AZ_DEG0 AZ_DEG1\n"
        "\n"
        "--window is exclusive with --width and --output. --width and\n"
        "--output must be given together. The output filename MUST be\n"
        "a .png file\n";

    struct option opts[] = {
        { "window",            no_argument,       NULL, 'W' },
        { "width",             required_argument, NULL, 'w' },
        { "output",            required_argument, NULL, 'o' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };


    bool        window = false;
    int         width  = 0;
    const char* output = NULL;

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

        case 'W':
            window = true;
            break;

        case 'w':
            width = atoi(optarg);
            break;

        case 'o':
            output = optarg;
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

    if( window )
    {
        if(width > 0 || output != NULL)
        {
            fprintf(stderr, "--window given, so --width and --output must NOT be given\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    }
    else
    {
        if(width <= 0 || output == NULL)
        {
            fprintf(stderr, "--window not given, so --width and --output MUST both be given\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }

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

    if(window)
    {
        render_to_window( lat, lon, az_deg0, az_deg1 );
        return 0;
    }

    const float fovy_deg = 20.0f;

    int height = (int)roundf( (float)width * fovy_deg / (az_deg1-az_deg0));

    char* image =
        render_to_image(lat, lon, az_deg0, az_deg1,
                        width, height);
    if(image == NULL)
    {
        fprintf(stderr, "Image render failed\n");
        return 1;
    }

    FreeImage_Initialise(true);
    FIBITMAP* fib = FreeImage_ConvertFromRawBits((BYTE*)image, width, height,
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
