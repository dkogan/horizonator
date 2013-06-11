#ifndef _FL_MEMORY_PNG_IMAGE
#define _FL_MEMORY_PNG_IMAGE

#include <FL/Fl.H>
#include <FL/Fl_Image.H>

//#include <stdio.h>
#include <png.h>
#include <zlib.h>

struct png_data {
  size_t offset;
  void *data;
};

class Fl_PNG_Memory_Image : public Fl_RGB_Image
{
  public:
    Fl_PNG_Memory_Image(void *buf);

  private:
    static void png_read_mem(png_structp png_ptr, png_bytep data, png_size_t length);

    png_data m_data;
};

#endif // _FL_MEMORY_PNG_IMAGE

