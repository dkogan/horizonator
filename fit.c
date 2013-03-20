#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#define IRON_ANGLE      72.2    /* angle of view for my iron mt photo */
#define PHOTO_PRESMOOTH 9


static void writeNormalized( const char* filename, const CvMat* img )
{
  double minval, maxval;
  CvPoint minpoint, maxpoint;
  cvMinMaxLoc( img, &minval, &maxval, &minpoint, &maxpoint, NULL );

  CvMat* scaled_img = cvCreateMat( img->rows, img->cols, CV_8UC1);
  cvConvertScale( img, scaled_img,
                  255.0/(maxval - minval),
                  -minval*255.0/(maxval - minval) );
  cvSaveImage( filename, scaled_img, 0 );
}

typedef enum{ PANO, PHOTO } image_type_t;
static CvMat* extractEdges( IplImage* img, image_type_t source )
{
  CvMat* temp_16sc;
  CvMat* temp_8uc;
  CvMat* edges;
  edges     = cvCreateMat( img->height, img->width, CV_8UC1 );
  temp_16sc = cvCreateMat( img->height, img->width, CV_16SC1 );
  temp_8uc  = cvCreateMat( img->height, img->width, CV_8UC1 );

  cvZero(edges);


  void accumulateEdgesFromChannel( int channel )
  {
    if( channel > 0 )
      cvSetImageCOI( img, channel );

    cvCopy(img, temp_8uc, NULL); // needed only becaues cvLaplace() doesn't support COI

    if( source == PHOTO )
      cvSmooth(temp_8uc, temp_8uc, CV_GAUSSIAN, PHOTO_PRESMOOTH, PHOTO_PRESMOOTH, 0.0, 0.0);

    cvLaplace(temp_8uc, temp_16sc, 3);
    cvAbs( temp_16sc, temp_16sc );
    cvAdd( edges, temp_16sc, edges, NULL);
  }


  if( img->nChannels == 1 )
    accumulateEdgesFromChannel( -1 );
  else if( source != PHOTO )
    accumulateEdgesFromChannel( 1 );
  else
    for(int i = 0; i < 3; i++)
      accumulateEdgesFromChannel( i+1 );


  cvReleaseMat(&temp_16sc);
  cvReleaseMat(&temp_8uc);
  return edges;
}

static CvPoint alignImages( const CvMat* img, const CvMat* pano )
{
  CvSize img_size  = cvGetSize(img);
  CvSize pano_size = cvGetSize(pano);

  // to handle wraparound correctly, I set the output size to be at least as
  // large as double the smallest dimensions
#define max(a,b) ( (a) > (b) ? (a) : (b) )
#define min(a,b) ( (a) < (b) ? (a) : (b) )
  CvSize dft_size =
    cvSize( max( 2*min(img_size.width,  pano_size.width),  max(img_size.width,  pano_size.width)  ),
            max( 2*min(img_size.height, pano_size.height), max(img_size.height, pano_size.height) ) );
#undef min
#undef max

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


  printf("right answer: (644,86)\n");


  cvSaveImage( "iedges.png", img_float ,0 );
  cvSaveImage( "pedges.png", pano_float,0 );

  CvMat* correlation_img = cvCreateMat( dft_size.height, dft_size.width, CV_8UC1);
  cvConvertScale( correlation, correlation_img,
                  255.0/(maxval - minval),
                  -minval*255.0/(maxval - minval) );
  cvSaveImage( "corr.png", correlation_img ,0 );
  cvReleaseMat( &correlation_img );



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


  CvMat* pano_edges;
  CvMat* img_edges;

  {
    pano_edges = extractEdges(pano, PANO);

    cvThreshold( pano_edges, pano_edges, 200.0, 0, CV_THRESH_TOZERO );
    // the non-edge areas of the panorama should be dont-care areas. I implement
    // this by
    // x -> dilate ? x : mean;
    // another way to state the same thing:
    //   !dilate -> mask
    //   cvSet(mean)

#define DILATE_R    9
#define EDGE_MINVAL 180

    IplConvKernel* kernel = cvCreateStructuringElementEx( 2*DILATE_R + 1, 2*DILATE_R + 1,
                                                         DILATE_R, DILATE_R,
                                                         CV_SHAPE_ELLIPSE, NULL);
    CvMat* dilated = cvCreateMat( pano->height, pano->width, CV_8UC1 );

    cvDilate(pano_edges, dilated, kernel, 1);

    CvScalar avg = cvAvg(pano_edges, dilated);
    cvCmpS(dilated, EDGE_MINVAL, dilated, CV_CMP_LT);
    cvSet( pano_edges, avg, dilated );

    cvReleaseMat(&dilated);
    cvReleaseStructuringElement(&kernel);
  }

  {
    img_edges = extractEdges(img, PHOTO);
    cvSmooth(img_edges, img_edges, CV_GAUSSIAN, 13, 13, 0.0, 0.0);
  }

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

