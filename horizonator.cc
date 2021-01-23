#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Double_Window.H>

#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"

#define WINDOW_W 800
#define WINDOW_H 600

static void redraw_slippymap( void* mapctrl );

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
    orb_mapctrl*            mapctrl;

    Fl_Double_Window* window = new Fl_Double_Window( WINDOW_W, WINDOW_H, "Horizonator" );
    const int         map_h  = window->h()/2;

    {
        mapctrl = new orb_mapctrl( 0, 0, window->w(), map_h, "horizonator!" );
        mapctrl->box(FL_NO_BOX);
        mapctrl->color(FL_BACKGROUND_COLOR);
        mapctrl->selection_color(FL_BACKGROUND_COLOR);
        mapctrl->labeltype(FL_NORMAL_LABEL);
        mapctrl->labelfont(0);
        mapctrl->labelsize(14);
        mapctrl->labelcolor(FL_FOREGROUND_COLOR);
        mapctrl->align(Fl_Align(FL_ALIGN_CENTER));

        orb_osmlayer* osmlayer = new orb_osmlayer;
        osmlayer->callback( &redraw_slippymap, mapctrl );

        layers.push_back(osmlayer);

        mapctrl->layers(layers);
    }

    window->resizable(window);
    window->end();
    window->show();


    Fl::run();

    return 0;
}

static void redraw_slippymap( void* mapctrl )
{
    reinterpret_cast<orb_mapctrl*>(mapctrl)->redraw();
}
