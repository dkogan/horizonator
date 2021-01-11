#include <stdio.h>
#include <stdlib.h>
#include <FreeImage.h>

#include "render_terrain.h"

int main(int argc, char* argv[])
{
    int image_width, image_height;

    char* image =
        render_to_image(&image_width, &image_height,

                        // Big Iron
                        34.2884, -117.7134,
                        -1, -1 );
    if(image == NULL)
        return 1;

    FreeImage_Initialise(true);

    FIBITMAP* fib = FreeImage_ConvertFromRawBits((BYTE*)image, image_width, image_height,
                                                 3*image_width, 24,
                                                 0,0,0,
                                                 false);
    FreeImage_Save(FIF_PNG, fib, "/tmp/tst.png", 0);
    FreeImage_Unload(fib);
    FreeImage_DeInitialise();

    free(image);

    return 0;
}
