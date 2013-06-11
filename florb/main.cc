#include <FL/Fl.H>

#include "orb_osmlayer.hpp"
#include "orb_mapctrl.hpp"
#include <FL/Fl_Double_Window.H>

// This is a bare top-level main() file. It was adapter from florb's fluid sources

// Call this when the set of layers has changed
#if 0
static int updatelayers(std::vector<orb_layer*> &layers)
{
  // Update the map control with the updated set of layers
  mapctrl->layers(layers);

  // Check whether any existing layer has beed deleted
  for (std::vector<orb_layer*>::iterator itold=layers.begin();itold!=layers.end();++itold) {

      // Try to find the current layer in the updated list
      std::vector<orb_layer*>::iterator itnew=layers.begin();
      for (;itnew!=layers.end();++itnew)
      	if (*itold == *itnew)
      	    break;

      // Layer not found in the updated list, delete it.
      if (itnew == layers.end())
      	delete *itold;
  }

  // Update the callbacks (for any new layers which don't have one registered yet)
  for (std::vector<orb_layer*>::iterator itnew=layers.begin();itnew!=layers.end();++itnew)
      (*itnew)->callback(cb_layer, this);

  // Update the current layers store
  layers = layers;

  return 0;
}
#endif

static void cb_layer(void* mapctrl)
{
  reinterpret_cast<orb_mapctrl*>(mapctrl)->refresh();
}

int main(int argc, char **argv)
{
  Fl::lock();


  orb_mapctrl* mapctrl;

  Fl_Double_Window* window = new Fl_Double_Window(800, 600, "Slippy map");
  window->align(Fl_Align(FL_ALIGN_CLIP|FL_ALIGN_INSIDE));
  {
    mapctrl = new orb_mapctrl(0, 0, 800, 600, "Slippy Map");
    mapctrl->box(FL_NO_BOX);
    mapctrl->color(FL_BACKGROUND_COLOR);
    mapctrl->selection_color(FL_BACKGROUND_COLOR);
    mapctrl->labeltype(FL_NORMAL_LABEL);
    mapctrl->labelfont(0);
    mapctrl->labelsize(14);
    mapctrl->labelcolor(FL_FOREGROUND_COLOR);
    mapctrl->align(Fl_Align(FL_ALIGN_CENTER));
    mapctrl->when(FL_WHEN_RELEASE);
    window->resizable(window);
  }
  window->end();


  // Create the OSM base layer
  orb_layer *l = new orb_osmlayer();
  l->callback(cb_layer, mapctrl);

  std::vector<orb_layer*> layers;
  layers.push_back(l);

  // Set the OSM base layer
  mapctrl->layers(layers);

  window->show(argc, argv);

  return Fl::run();
}
