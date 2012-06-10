#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#define IRON_ANGLE 72.2 /* angle of view for my iron mt photo */

static void extractEdges( const IplImage* img, CvMat* edges, int presmooth )
{
  CvSize size = cvGetSize(img);

  CvMat* img_channels[3];
  img_channels[0] = cvCreateMat( size.height, size.width, CV_8UC1 );
  img_channels[1] = cvCreateMat( size.height, size.width, CV_8UC1 );
  img_channels[2] = cvCreateMat( size.height, size.width, CV_8UC1 );

  CvMat* img_channel_edges[3];
  img_channel_edges[0] = cvCreateMat( size.height, size.width, CV_16SC1 );
  img_channel_edges[1] = cvCreateMat( size.height, size.width, CV_16SC1 );
  img_channel_edges[2] = cvCreateMat( size.height, size.width, CV_16SC1 );

  cvSplit(img,
          img_channels[0],
          img_channels[1],
          img_channels[2],
          NULL);

  if( presmooth )
  {
    cvSmooth(img_channels[0], img_channels[0], CV_GAUSSIAN, presmooth, presmooth, 0.0, 0.0);
    cvSmooth(img_channels[1], img_channels[2], CV_GAUSSIAN, presmooth, presmooth, 0.0, 0.0);
    cvSmooth(img_channels[2], img_channels[2], CV_GAUSSIAN, presmooth, presmooth, 0.0, 0.0);
  }

  cvLaplace(img_channels[0], img_channel_edges[0], 3);
  cvLaplace(img_channels[1], img_channel_edges[1], 3);
  cvLaplace(img_channels[2], img_channel_edges[2], 3);

  cvAbs( img_channel_edges[0], img_channel_edges[0] );
  cvAbs( img_channel_edges[1], img_channel_edges[1] );
  cvAbs( img_channel_edges[2], img_channel_edges[2] );

  cvZero(edges);
  cvAdd(edges, img_channel_edges[0], edges, NULL);
  cvAdd(edges, img_channel_edges[1], edges, NULL);
  cvAdd(edges, img_channel_edges[2], edges, NULL);

  cvReleaseMat( &img_channels[0] );
  cvReleaseMat( &img_channels[1] );
  cvReleaseMat( &img_channels[2] );

  cvReleaseMat( &img_channel_edges[0] );
  cvReleaseMat( &img_channel_edges[1] );
  cvReleaseMat( &img_channel_edges[2] );
}

static CvPoint alignImages( const CvMat* img, const CvMat* pano )
{
  CvSize img_size  = cvGetSize(img);
  CvSize pano_size = cvGetSize(pano);

  CvSize dft_size = cvSize( img_size.width  > pano_size.width  ? img_size.width  : pano_size.width,
                            img_size.height > pano_size.height ? img_size.height : pano_size.height );

#define DoDft(mat)                                                      \
  CvMat* mat ## _float = cvCreateMat( dft_size.height, dft_size.width, CV_32FC1); \
  assert( mat ## _float );                                              \
                                                                        \
  CvMat* mat ## _dft = cvCreateMat( dft_size.height, dft_size.width, CV_32FC1); \
  assert( mat ## _dft );                                                \
                                                                        \
  cvSet( mat ## _float,                                                 \
         cvAvg(mat, NULL),                                              \
         NULL );                                                        \
                                                                        \
  CvMat mat ## _float_origsize;                                         \
  cvGetSubRect( mat ## _float, &mat ## _float_origsize,                 \
                cvRect(0,0, mat ## _size.width, mat ## _size.height ) ); \
  cvConvert( mat,  &mat ## _float_origsize );                           \
  cvDFT(mat ## _float,  mat ## _dft,  CV_DXT_FORWARD, 0)



  DoDft(img);
  DoDft(pano);

  CvMat* correlation            = cvCreateMat( dft_size.height, dft_size.width, CV_32FC1);
  CvMat* correlation_freqdomain = cvCreateMat( dft_size.height, dft_size.width, CV_32FC1);
  assert(correlation_freqdomain);

  cvMulSpectrums(pano_dft, img_dft, correlation_freqdomain, CV_DXT_MUL_CONJ);
  cvDFT(correlation_freqdomain, correlation, CV_DXT_INVERSE, 0);


  double minval, maxval;
  CvPoint minpoint, maxpoint;
  cvMinMaxLoc( correlation, &minval, &maxval, &minpoint, &maxpoint, NULL );


  cvReleaseMat( &correlation );
  cvReleaseMat( &correlation_freqdomain );
  cvReleaseMat( &img_float );
  cvReleaseMat( &pano_float );
  cvReleaseMat( &img_dft );
  cvReleaseMat( &pano_dft );

  return maxpoint;
}

int main(int argc, char* argv[])
{
  if( argc != 3 )
  {
    fprintf(stderr, "Usage: %s panorender.png photo.jpg\n", argv[0]);
    return 1;
  }

  IplImage* pano = cvLoadImage( argv[1], CV_LOAD_IMAGE_COLOR);  assert(pano);
  IplImage* img  = cvLoadImage( argv[2], CV_LOAD_IMAGE_COLOR);  assert(img);

  CvMat* pano_edges = cvCreateMat( pano->height, pano->width, CV_16SC1 );
  extractEdges(pano, pano_edges, 0);

  CvMat* img_edges = cvCreateMat( img->height, img->width, CV_16SC1 );
  extractEdges(img, img_edges, 9);
  cvSmooth(img_edges, img_edges, CV_GAUSSIAN, 13, 13, 0.0, 0.0);

  // cvSmooth(img_channels[0], img_channels[0], CV_GAUSSIAN, presmooth, presmooth, 0.0, 0.0);

  CvPoint offset = alignImages( img_edges, pano_edges );
  printf("offset: x,y: %d %d\n", offset.x, offset.y );



  cvReleaseMat  ( &pano_edges );
  cvReleaseMat  ( &img_edges );
  cvReleaseImage( &pano );
  cvReleaseImage( &img );

  return 0;
}

#if 0
static void undistort(void)
{
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
  cvReleaseImage( &remapped );
  cvReleaseMat( &mapx );
  cvReleaseMat( &mapy );

}

#endif

