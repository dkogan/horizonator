#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#define IRON_ANGLE      10.8373931367 /* angle of view for my iron mt photo */

int main(int argc, char* argv[])
{
  if( argc != 2 )
  {
    fprintf(stderr, "Must have input filename as the only argument\n");
    return 1;
  }

  IplImage* src = cvLoadImage( argv[1], CV_LOAD_IMAGE_COLOR);
  assert(src);

  CvSize size = cvGetSize(src);

  IplImage* remapped = cvCreateImage(size, 8, 3);
  assert(remapped);

  CvMat* mapx = cvCreateMat(size.height, size.width, CV_32FC1);
  assert(mapx); assert(mapx->step == size.width*sizeof(float));
  CvMat* mapy = cvCreateMat(size.height, size.width, CV_32FC1);
  assert(mapy); assert(mapy->step == size.width*sizeof(float));

  for(int j=0; j<size.height; j++ )
  {
    for( int i=0; i<size.width; i++ )
    {
      mapy->data.fl[i + mapy->cols*j] = j;

      // I'm mapping the image from a perspective projection to a mercator one.
      // Here 'i' represents an angle from a perspective projection.
      float w         = (float)(mapx->cols - 1);
      float pixel_mid = w / 2.0f;
      float i_mid     = (float)i - pixel_mid;
      float f         = w / 2.0f / tanf(IRON_ANGLE * M_PI / 180.0f / 2.0f);
      float angle     = 180.0f / M_PI * atanf( i_mid / f );

      mapx->data.fl[i + mapx->cols*j] = (angle / IRON_ANGLE + 0.5f) * w;
    }
  }

  cvRemap(src, remapped,
          mapx, mapy,
          CV_INTER_LINEAR+CV_WARP_FILL_OUTLIERS,
          cvScalarAll(0) );

  assert( cvSaveImage("remapped.png", remapped, 0) );

  cvReleaseImage( &src );
  cvReleaseImage( &remapped );
  cvReleaseMat( &mapx );
  cvReleaseMat( &mapy );


  return 0;
}
