#include "Fl_PNG_Memory_Image.hpp"

//
// 'Fl_PNG_Image::Fl_PNG_Memory_Image()' - Load a PNG image buffer.
//

Fl_PNG_Memory_Image::Fl_PNG_Memory_Image (void *buf) : 
    Fl_RGB_Image(0,0,0)
{
    int           i;                      // Looping var
    int           channels;               // Number of color channels
    png_structp   pp;                     // PNG read pointer
    png_infop     info;                   // PNG info pointers
    png_bytep     *rows;                  // PNG row pointers

    //prepare the struct
    m_data.offset = 0;
    m_data.data = buf;

    // Setup the PNG data structures...
    pp   = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(pp);
    png_const_structp cpp = (png_const_structp)pp;

    // Initialize the PNG read "engine"...
    png_set_read_fn(pp, (png_voidp)&m_data, png_read_mem);

    // Get the image dimensions and convert to grayscale or RGB...
    png_read_info(pp, info);

    if (png_get_color_type(cpp, info) == PNG_COLOR_TYPE_PALETTE)
        png_set_expand(pp);

    if (png_get_color_type(cpp, info) & PNG_COLOR_MASK_COLOR)
        channels = 3;
    else
        channels = 1;

    if ((png_get_color_type(cpp, info) & PNG_COLOR_MASK_ALPHA))
        channels ++;

    w((int)(png_get_image_width(cpp, info)));
    h((int)(png_get_image_height(cpp, info)));
    d(channels);

    if (png_get_bit_depth(cpp, info) < 8) {
        png_set_packing(pp);
        png_set_expand(pp);
    }
    else if (png_get_bit_depth(cpp, info) == 16)
        png_set_strip_16(pp);

#  if defined(HAVE_PNG_GET_VALID) && defined(HAVE_PNG_SET_TRNS_TO_ALPHA)
    // Handle transparency...
    if (png_get_valid(pp, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(pp);
#  endif // HAVE_PNG_GET_VALID && HAVE_PNG_SET_TRNS_TO_ALPHA

    array = new uchar[w() * h() * d()];
    alloc_array = 1;

    // Allocate pointers...
    rows = new png_bytep[h()];

    for (i = 0; i < h(); i ++)
        rows[i] = (png_bytep)(array + i * w() * d());

    // Read the image, handling interlacing as needed...
    for (i = png_set_interlace_handling(pp); i > 0; i --)
        png_read_rows(pp, rows, NULL, h());

#ifdef WIN32
    // Some Windows graphics drivers don't honor transparency when RGB == 
    white
        if (channels == 4) {
            // Convert RGB to 0 when alpha == 0...
            uchar *ptr = (uchar *)array;
            for (i = w() * h(); i > 0; i --, ptr += 4)
                if (!ptr[3]) ptr[0] = ptr[1] = ptr[2] = 0;
        }
#endif // WIN32

    // Free memory and return...
    delete[] rows;

    png_read_end(pp, info);
    png_destroy_read_struct(&pp, &info, NULL);
}

void Fl_PNG_Memory_Image::png_read_mem(png_structp png_ptr, png_bytep data, png_size_t length)
{
  png_data *pngdata = (png_data*)png_get_io_ptr(png_ptr);
  void *src = (void*)&(((unsigned char*)pngdata->data)[pngdata->offset]);

  /* copy data from image buffer */
  memcpy(data, src, length);

  /* advance in the file */
  pngdata->offset += length;
}

