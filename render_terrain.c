#include <tgmath.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <opencv2/highgui/highgui_c.h>

#include "dem_downloader.h"

// can be used for testing/debugging to turn off the seam rendering
#define NOSEAM 0


static enum { PM_FILL, PM_LINE, PM_POINT, PM_NUM } PolygonMode = PM_FILL;
static int Ntriangles, Nvertices;

static GLint uniform_aspect;


// Each SRTM file is a grid of 1201x1201 samples; last row/col overlap in neighboring DEMs
#define WDEM          1201
#define CELLS_PER_DEG (WDEM - 1) /* -1 because of the overlapping DEM edges */

// We will render a square grid of data that is at most R_RENDER cells away from
// the viewer in the inf-norm sense
#define R_RENDER 1000

#define FOVY_DEG_DEFAULT    50.0f /* vertical field of view of the render */
#define OFFSCREEN_W_DEFAULT 4000

#define OFFSCREEN_H(width, fovy_deg) (int)( 0.5 + width / 360.0 * fovy_deg)
#define OFFSCREEN_H_DEFAULT OFFSCREEN_H(OFFSCREEN_W_DEFAULT, FOVY_DEG_DEFAULT)

static int offscreen_w = OFFSCREEN_W_DEFAULT;
static int offscreen_h = OFFSCREEN_H_DEFAULT;



static bool loadGeometry( float view_lat, float view_lon,
                          float* elevation_out )
{
  GLint uniform_view_z;
  GLint uniform_renderStartN, uniform_renderStartE;
  GLint uniform_DEG_PER_CELL;
  GLint uniform_view_lon, uniform_view_lat;
  GLint uniform_sin_view_lat, uniform_cos_view_lat;


  // These functions take in coordinates INSIDE THEIR SPECIFIC DEM
  int16_t sampleDEM(int i, int j, const unsigned char* dem)
  {
    // The DEMs are organized north to south, so I flip around the j accessor to
    // keep all accesses ordered by increasing lat, lon
    uint32_t p = i + (WDEM-1 -j )*WDEM;
    int16_t  z = (int16_t) ((dem[2*p] << 8) | dem[2*p + 1]);
    return (z < 0) ? 0 : z;
  }
  float getViewerHeight(int i, int j, const unsigned char* dem)
  {
#warning this function shouldnt be per-DEM. What if the viewer is on a DEM boundary?
    float z = -1e20f;

    for( int di=-1; di<=1; di++ )
      for( int dj=-1; dj<=1; dj++ )
      {
        if( i+di >= 0 && i+di < WDEM &&
            j+dj >= 0 && j+dj < WDEM )
          z = fmax(z, (float) sampleDEM(i+di, j+dj, dem) );
      }

    return z;
  }





  // Viewer is looking north, the seam is behind (to the south). If the viewer is
  // directly on a grid value, then the cell of the seam is poorly defined. In
  // that scenario, I nudge the viewer to one side to unambiguously pick the seam
  // cell. I do this in both lat and lon directions to resolve ambiguity.
  void nudgeCoord( float* view )
  {
    float cell_idx         = (*view - floor(*view)) * CELLS_PER_DEG;
    float cell_idx_rounded = round( cell_idx );

    // want at least 0.1 cells of separation
    if( fabs( cell_idx - cell_idx_rounded ) < 0.1 )
    {
      if( cell_idx > cell_idx_rounded ) *view += 0.1/CELLS_PER_DEG;
      else                              *view -= 0.1/CELLS_PER_DEG;
    }
  }
  nudgeCoord( &view_lat );
  nudgeCoord( &view_lon );



  // I render a square with radius R_RENDER centered at the view point. There
  // are (2*R_RENDER)**2 cells in the render. In all likelihood this will
  // encompass multiple DEMs. The base DEM is the one that contains the
  // viewpoint. I compute the latlon coords of the base DEM origin and of the
  // render origin. I also compute the grid coords of the base DEM origin (grid
  // coords of the render origin are 0,0 by definition)
  //
  // grid starts at the NW corner, and traverses along the latitude first.
  // DEM tile is named from the SW point

  int   baseDEMfileE,           baseDEMfileN;
  int   renderStartDEMfileE,    renderStartDEMfileN;
  int   renderStartDEMcoords_i, renderStartDEMcoords_j;
  float renderStartE,           renderStartN;
  int   renderEndDEMfileE,      renderEndDEMfileN;

  {
    baseDEMfileE = (int)floor( view_lon );
    baseDEMfileN = (int)floor( view_lat );

    // latlon of the render origin
    float renderStartE_unaligned = view_lon - (float)R_RENDER/CELLS_PER_DEG;
    float renderStartN_unaligned = view_lat - (float)R_RENDER/CELLS_PER_DEG;

    renderStartDEMfileE = floor(renderStartE_unaligned);
    renderStartDEMfileN = floor(renderStartN_unaligned);

    renderStartDEMcoords_i = round( (renderStartE_unaligned - renderStartDEMfileE) * CELLS_PER_DEG );
    renderStartDEMcoords_j = round( (renderStartN_unaligned - renderStartDEMfileN) * CELLS_PER_DEG );

    renderStartE = renderStartDEMfileE + (float)renderStartDEMcoords_i / (float)CELLS_PER_DEG;
    renderStartN = renderStartDEMfileN + (float)renderStartDEMcoords_j / (float)CELLS_PER_DEG;

    // 2*R_RENDER - 1 is the last cell.
    renderEndDEMfileE = renderStartDEMfileE + (renderStartDEMcoords_i + 2*R_RENDER-1 ) / CELLS_PER_DEG;
    renderEndDEMfileN = renderStartDEMfileN + (renderStartDEMcoords_j + 2*R_RENDER-1 ) / CELLS_PER_DEG;

    // If the last cell is the first on in a DEM, I can stay at the previous
    // DEM, since there's 1 row/col overlap between each adjacent pairs of DEMs
    if( (renderStartDEMcoords_i + 2*R_RENDER-1) % CELLS_PER_DEG == 0 )
      renderEndDEMfileE--;
    if( (renderStartDEMcoords_j + 2*R_RENDER-1) % CELLS_PER_DEG == 0 )
      renderEndDEMfileN--;
  }

  // I now load my DEMs. Each dems[] is a pointer to an mmap-ed source file.
  // The ordering of dems[] is increasing latlon, with lon varying faster
  int Ndems_i = renderEndDEMfileE - renderStartDEMfileE + 1;
  int Ndems_j = renderEndDEMfileN - renderStartDEMfileN + 1;

  unsigned char* dems      [Ndems_i][Ndems_j];
  size_t         mmap_sizes[Ndems_i][Ndems_j];
  int            mmap_fd   [Ndems_i][Ndems_j];

  memset( dems, 0, Ndems_i*Ndems_j*sizeof(dems[0][0]) );

  void unmmap_all_dems(void)
  {
    for( int i=0; i<Ndems_i; i++)
      for( int j=0; j<Ndems_j; j++)
        if( dems[i][j] != NULL && dems[i][j] != MAP_FAILED )
        {
          munmap( dems[i][j], mmap_sizes[i][j] );
          close( mmap_fd[i][j] );
        }
  }

  for( int j = 0; j < Ndems_j; j++ )
  {
    for( int i = 0; i < Ndems_i; i++ )
    {
      // This function will try to download the DEM if it's not found
      const char* filename = getDEM_filename( j + renderStartDEMfileN,
                                              i + renderStartDEMfileE);
      if( filename == NULL )
        return false;

      struct stat sb;
      mmap_fd[i][j] = open( filename, O_RDONLY );
      if( mmap_fd[i][j] <= 0 )
      {
        unmmap_all_dems();
        fprintf(stderr, "couldn't open DEM file '%s'\n", filename );
        return false;
      }

      assert( fstat(mmap_fd[i][j], &sb) == 0 );

      dems      [i][j] = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, mmap_fd[i][j], 0);
      mmap_sizes[i][j] = sb.st_size;

      if( dems[i][j] == MAP_FAILED )
      {
        unmmap_all_dems();
        return false;
      }

      if( WDEM*WDEM*2 != sb.st_size )
      {
        unmmap_all_dems();
        return false;
      }
    }
  }

  Nvertices = (2*R_RENDER) * (2*R_RENDER);
  Ntriangles = (2*R_RENDER - 1)*(2*R_RENDER - 1) * 2;

  // seam business
  int Lseam = 0;
  int view_i, view_j;
  int view_i_DEMcoords, view_j_DEMcoords;

  float viewer_z;
  {
    // if we're doing a mercator projection, we must take care of the seam. The
    // camera always looks north, so the seam is behind us. Behind me are two
    // rows of vertices, one on either side. With a mercator projection, these
    // rows actually appear on opposite ends of the resulting image, and thus I
    // do not want to simply add triangles into this gap. Instead, I double-up
    // each of these rows, place the duplicated vertices off screen (angle < -pi
    // for one row and angle > pi for the other), and render the seam twice,
    // once for each side.
    //
    // The square I'm sitting on demands special treatment. I construct a
    // 6-triangle tiling that fully covers my window
    view_i           = floor( ( view_lon - (float)renderStartE) * CELLS_PER_DEG );
    view_j           = floor( ( view_lat - (float)renderStartN) * CELLS_PER_DEG );
    view_i_DEMcoords = (view_i + renderStartDEMcoords_i) % CELLS_PER_DEG;
    view_j_DEMcoords = (view_j + renderStartDEMcoords_j) % CELLS_PER_DEG;

    // The viewer elevation
    viewer_z = getViewerHeight( view_i_DEMcoords, view_j_DEMcoords,
                                dems[baseDEMfileE - renderStartDEMfileE][baseDEMfileN - renderStartDEMfileN] );

    Lseam = view_j+1;

#if NOSEAM == 0
    Nvertices  += Lseam*2;      // double-up the seam vertices
    Ntriangles += (Lseam-1)*2;  // Seam rendered twice. This is the extra one
    Ntriangles -= 2;            // Don't render the normal thing at the viewer square
    Ntriangles += 6;            // tiling at the viewer square
    Nvertices  += 2;            // the vertices in the bottom-left and
                                // bottom-right of the image. used for the
                                // viewer square
#else
    Ntriangles -= (Lseam-1)*2;
    Ntriangles -= 2;            // Don't render anything at the viewer square
#endif
  }

  // vertices
  //
  // I fill in the VBO. Each point is a 16-bit integer tuple (ilon,ilat,height).
  // The first 2 args are indices into the DEM. The height is in meters
  {
    GLuint vertexBufID;
    glGenBuffers(1, &vertexBufID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);
    glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
    glVertexPointer(3, GL_SHORT, 0, NULL);

    GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    int vertex_buf_idx = 0;

    {
      int j_dem    = renderStartDEMcoords_j;
      int DEMfileN = renderStartDEMfileN;

      for( int j=0; j<2*R_RENDER; j++ )
      {
        int i_dem    = renderStartDEMcoords_i;
        int DEMfileE = renderStartDEMfileE;

        for( int i=0; i<2*R_RENDER; i++ )
        {
          // it would be more efficient to do this one DEM at a time (mmap one at
          // a time), but that would complicate the code, and not make any
          // observable difference
          vertices[vertex_buf_idx++] = i;
          vertices[vertex_buf_idx++] = j;
          vertices[vertex_buf_idx++] = sampleDEM(i_dem, j_dem,
                                                 dems[DEMfileE - renderStartDEMfileE][DEMfileN - renderStartDEMfileN] );

          if( ++i_dem >= CELLS_PER_DEG )
          {
            i_dem = 0;
            DEMfileE++;
          }
        }

        if( ++j_dem >= CELLS_PER_DEG )
        {
          j_dem = 0;
          DEMfileN++;
        }
      }
    }

#if NOSEAM == 0
    // add the extra seam vertices
    if( Lseam )
    {
      int j_dem    = renderStartDEMcoords_j;
      int DEMfileN = renderStartDEMfileN;

      for( int j=0; j<Lseam; j++ )
      {
        // These duplicates have the same geometry as the originals, but the
        // shader will project them differently, by moving the resulting angle
        // by 2*pi

        // left side; negative to indicate that this is a duplicate for the left seam
        vertices[vertex_buf_idx++] = view_i;
        vertices[vertex_buf_idx++] = -(j+1); // extra 1 because I can't assume that -0 < 0
        vertices[vertex_buf_idx++] = sampleDEM(view_i_DEMcoords, j_dem,
                                               dems[baseDEMfileE - renderStartDEMfileE][DEMfileN - renderStartDEMfileN]);


        // right side; negative to indicate that this is a duplicate for the right
        // seam
        vertices[vertex_buf_idx++] = -(view_i+1);
        vertices[vertex_buf_idx++] = j;
        vertices[vertex_buf_idx++] = sampleDEM(view_i_DEMcoords+1, j_dem,
                                               dems[baseDEMfileE - renderStartDEMfileE][DEMfileN - renderStartDEMfileN]);

        if( ++j_dem >= CELLS_PER_DEG )
        {
          j_dem = 0;
          DEMfileN++;
        }
      }
    }

    // Now two magic extra vertices used for the square I'm on: the bottom-left
    // of screen and the bottom-right of screen. The vertex coordinates here are
    // bogus. They are just meant to indicate to the shader to use hard-coded
    // transformed coords. (neg neg neg) means bottom-left. (neg neg pos) means
    // bottom-right
    vertices[vertex_buf_idx++] = -1.0;
    vertices[vertex_buf_idx++] = -1.0;
    vertices[vertex_buf_idx++] = -1.0;

    vertices[vertex_buf_idx++] = -1.0;
    vertices[vertex_buf_idx++] = -1.0;
    vertices[vertex_buf_idx++] =  1.0;
#endif

    assert( glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE );
    assert( vertex_buf_idx == Nvertices*3 );
  }

  // indices
  {
    GLuint indexBufID;
    glGenBuffers(1, &indexBufID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, Ntriangles*3*sizeof(GLuint), NULL, GL_STATIC_DRAW);

    GLuint* indices = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<(2*R_RENDER-1); j++ )
    {
      for( int i=0; i<(2*R_RENDER-1); i++ )
      {
        // seam?
        if( i == view_i)
        {
          if( j == view_j )
          {
#if NOSEAM == 0
            // square camera is sitting on gets special treatment

#define BEHIND_LEFT_NOT_MIRRORED              (j + 0)*(2*R_RENDER) + (i + 0)
#define BEHIND_RIGHT_NOT_MIRRORED             (j + 0)*(2*R_RENDER) + (i + 1)
#define FRONT_LEFT                            (j + 1)*(2*R_RENDER) + (i + 0)
#define FRONT_RIGHT                           (j + 1)*(2*R_RENDER) + (i + 1)
#define BEHIND_LEFT_MIRRORED_PAST_RIGHT_EDGE  (2*R_RENDER)*(2*R_RENDER) + j*2
#define BEHIND_RIGHT_MIRRORED_PAST_LEFT_EDGE  (2*R_RENDER)*(2*R_RENDER) + j*2 + 1
#define BOTTOM_LEFT_OF_IMAGE                  Nvertices - 2
#define BOTTOM_RIGHT_OF_IMAGE                 Nvertices - 1

            indices[idx++] = BEHIND_RIGHT_MIRRORED_PAST_LEFT_EDGE;
            indices[idx++] = BOTTOM_LEFT_OF_IMAGE;
            indices[idx++] = BEHIND_LEFT_NOT_MIRRORED;

            indices[idx++] = BEHIND_LEFT_NOT_MIRRORED;
            indices[idx++] = BOTTOM_LEFT_OF_IMAGE;
            indices[idx++] = FRONT_LEFT;

            indices[idx++] = FRONT_LEFT;
            indices[idx++] = BOTTOM_LEFT_OF_IMAGE;
            indices[idx++] = BOTTOM_RIGHT_OF_IMAGE;

            indices[idx++] = FRONT_LEFT;
            indices[idx++] = BOTTOM_RIGHT_OF_IMAGE;
            indices[idx++] = FRONT_RIGHT;

            indices[idx++] = FRONT_RIGHT;
            indices[idx++] = BOTTOM_RIGHT_OF_IMAGE;
            indices[idx++] = BEHIND_RIGHT_NOT_MIRRORED;

            indices[idx++] = BEHIND_RIGHT_NOT_MIRRORED;
            indices[idx++] = BOTTOM_RIGHT_OF_IMAGE;
            indices[idx++] = BEHIND_LEFT_MIRRORED_PAST_RIGHT_EDGE;

#undef FRONT_LEFT
#undef FRONT_RIGHT
#undef BEHIND_LEFT_NOT_MIRRORED
#undef BEHIND_RIGHT_NOT_MIRRORED
#undef BEHIND_LEFT_MIRRORED_PAST_RIGHT_EDGE
#undef BEHIND_RIGHT_MIRRORED_PAST_LEFT_EDGE
#undef BOTTOM_LEFT_OF_IMAGE
#undef BOTTOM_RIGHT_OF_IMAGE

#endif
            continue;
          }

          if( j < Lseam )
          {
#if NOSEAM == 0
            // seam. I add two sets of triangles here; one for the left edge of
            // the screen and one for the right

            // right edge:
            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) +  j     *2;
            indices[idx++] = (j + 1)     *(2*R_RENDER) + (i + 1);
            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) + (j + 1)*2;

            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) +  j*2;
            indices[idx++] = (j + 0)     *(2*R_RENDER) + (i + 1);
            indices[idx++] = (j + 1)     *(2*R_RENDER) + (i + 1);

            // left edge:
            indices[idx++] = (j + 0)     *(2*R_RENDER) + (i + 0);
            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) + (j + 1)*2 + 1;
            indices[idx++] = (j + 1)     *(2*R_RENDER) + (i + 0);

            indices[idx++] = (j + 0)     *(2*R_RENDER) + (i + 0);
            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) +  j     *2 + 1;
            indices[idx++] = (2*R_RENDER)*(2*R_RENDER) + (j + 1)*2 + 1;
#endif

            continue;
          }
        }

        // non-seam
        indices[idx++] = (j + 0)*(2*R_RENDER) + (i + 0);
        indices[idx++] = (j + 1)*(2*R_RENDER) + (i + 1);
        indices[idx++] = (j + 1)*(2*R_RENDER) + (i + 0);

        indices[idx++] = (j + 0)*(2*R_RENDER) + (i + 0);
        indices[idx++] = (j + 0)*(2*R_RENDER) + (i + 1);
        indices[idx++] = (j + 1)*(2*R_RENDER) + (i + 1);
      }
    }
    assert( glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) == GL_TRUE );
    assert(idx == Ntriangles*3);
  }

  // shaders
  {
    // The shader transforms the VBO vertices into the view coord system. Each VBO
    // point is a 16-bit integer tuple (ilon,ilat,height). The first 2 args are
    // indices into the DEM. The height is in meters
    const GLchar* vertexShaderSource =
#include "vertex.glsl.h"
      ;

    const GLchar* fragmentShaderSource =
#include "fragment.glsl.h"
      ;

    char msg[1024];
    int len;
    GLuint program =glCreateProgram();
    assert( glGetError() == GL_NO_ERROR );


#define installshader(type,TYPE)                                        \
    GLuint type ## Shader = glCreateShader(GL_ ## TYPE ## _SHADER);     \
    assert( glGetError() == GL_NO_ERROR );                              \
                                                                        \
    glShaderSource(type ## Shader, 1, (const GLchar**)&type ## ShaderSource, NULL); \
    assert( glGetError() == GL_NO_ERROR );                              \
                                                                        \
    glCompileShader(type ## Shader);                                    \
    assert( glGetError() == GL_NO_ERROR );                              \
    glGetShaderInfoLog( type ## Shader, sizeof(msg), &len, msg );       \
    if( strlen(msg) )                                                   \
      printf(#type " msg: %s\n", msg);                                  \
                                                                        \
    glAttachShader(program, type ##Shader);                             \
    assert( glGetError() == GL_NO_ERROR );



    installshader(vertex, VERTEX);
    installshader(fragment, FRAGMENT);

    glLinkProgram(program); assert( glGetError() == GL_NO_ERROR );
    glUseProgram(program);  assert( glGetError() == GL_NO_ERROR );


    uniform_view_z       = glGetUniformLocation(program, "view_z"      ); assert( glGetError() == GL_NO_ERROR );
    uniform_renderStartN = glGetUniformLocation(program, "renderStartN"); assert( glGetError() == GL_NO_ERROR );
    uniform_renderStartE = glGetUniformLocation(program, "renderStartE"); assert( glGetError() == GL_NO_ERROR );
    uniform_DEG_PER_CELL = glGetUniformLocation(program, "DEG_PER_CELL"); assert( glGetError() == GL_NO_ERROR );
    uniform_view_lat     = glGetUniformLocation(program, "view_lat"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_view_lon     = glGetUniformLocation(program, "view_lon"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_sin_view_lat = glGetUniformLocation(program, "sin_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_cos_view_lat = glGetUniformLocation(program, "cos_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_aspect       = glGetUniformLocation(program, "aspect"      ); assert( glGetError() == GL_NO_ERROR );

    glUniform1f( uniform_view_z,       viewer_z);
    glUniform1f( uniform_renderStartN, renderStartN);
    glUniform1f( uniform_renderStartE, renderStartE);
    glUniform1f( uniform_DEG_PER_CELL, 1.0f/ (float)CELLS_PER_DEG );

    glUniform1f( uniform_view_lon,     view_lon * M_PI / 180.0f );
    glUniform1f( uniform_view_lat,     view_lat * M_PI / 180.0f );
    glUniform1f( uniform_sin_view_lat, sin( M_PI / 180.0f * view_lat ));
    glUniform1f( uniform_cos_view_lat, cos( M_PI / 180.0f * view_lat ));
  }

  unmmap_all_dems();

  if( elevation_out )
    *elevation_out = viewer_z;

  return true;
}


static void window_reshape(int width, int height)
{
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUniform1f(uniform_aspect, (float)width / (float)height);
}

static void do_draw(void)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  static const GLenum pmMap[] = {GL_FILL, GL_LINE, GL_POINT};
  glPolygonMode(GL_FRONT_AND_BACK, pmMap[ PolygonMode ] );

  glEnable(GL_CULL_FACE);

  // draw
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_INDEX_ARRAY);
  glDrawElements(GL_TRIANGLES, Ntriangles*3, GL_UNSIGNED_INT, NULL);


  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_CULL_FACE);
}

static void window_display(void)
{
  do_draw();
  glutSwapBuffers();
}

static void window_keyPressed(unsigned char key,
                              int x __attribute__((unused)) ,
                              int y __attribute__((unused)) )
{
  static GLenum winding = GL_CCW;

  switch (key)
  {
  case 'w':
    if(++PolygonMode == PM_NUM)
      PolygonMode = 0;
    break;

  case 'r':
    if (winding == GL_CCW) winding = GL_CW;
    else                   winding = GL_CCW;
    glFrontFace(winding);
    break;

  case 'q':
  case 27:
    exit(0);
  }

  glutPostRedisplay();
}


static IplImage* readOffscreenPixels( bool do_bgr )
{
  CvSize size = { .width  = offscreen_w,
                  .height = offscreen_h };

  IplImage* img = cvCreateImage(size, 8, 3);
  assert( img );

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0,0, offscreen_w, offscreen_h,
               do_bgr ? GL_BGR : GL_RGB,
               GL_UNSIGNED_BYTE, img->imageData);
  cvFlip(img, NULL, 0);
  return img;
}

static bool setup_gl( bool doRenderToScreen,
                      float view_lat, float view_lon,
                      float* elevation_out )
{
  void DoFeatureChecks(void)
  {
    char *version = (char *) glGetString(GL_VERSION);
    if (version[0] == '1') {
      /* check for individual extensions */
      if (!glutExtensionSupported("GL_ARB_vertex_shader")) {
        printf("Sorry, GL_ARB_vertex_shader is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_ARB_fragment_shader")) {
        printf("Sorry, GL_ARB_fragment_shader is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_ARB_vertex_buffer_object")) {
        printf("Sorry, GL_ARB_vertex_buffer_object is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_EXT_framebuffer_object")) {
        printf("GL_EXT_framebuffer_object not found!\n");
        exit(1);
      }
    }
  }

  void createOffscreenTargets(void)
  {
    GLuint frameBufID;
    {
      glGenFramebuffers(1, &frameBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    {
      GLuint renderBufID;
      glGenRenderbuffers(1, &renderBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindRenderbuffer(GL_RENDERBUFFER, renderBufID);
      assert( glGetError() == GL_NO_ERROR );

      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, offscreen_w, offscreen_h);
      assert( glGetError() == GL_NO_ERROR );

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, renderBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    {
      GLuint depthBufID;
      glGenRenderbuffers(1, &depthBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindRenderbuffer(GL_RENDERBUFFER, depthBufID);
      assert( glGetError() == GL_NO_ERROR );

      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, offscreen_w, offscreen_h);
      assert( glGetError() == GL_NO_ERROR );

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depthBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    assert( glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE );
  }


  glutInit(&(int){1}, &(char*){"exec"});
  glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH | ( doRenderToScreen ? GLUT_DOUBLE : 0 ));

  // when offscreen, I really don't want to glutCreateWindow(), but for some
  // reason not doing this causes glewInit() to segfault...
  glutCreateWindow("Terrain renderer");
  glewInit();
  DoFeatureChecks();

  if( doRenderToScreen )
  {
    glutKeyboardFunc(window_keyPressed);
    glutReshapeFunc (window_reshape);
    glutDisplayFunc (window_display);
  }
  else
    createOffscreenTargets();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_NORMALIZE);
  glClearColor(0, 0, 1, 0);

  return loadGeometry( view_lat, view_lon, elevation_out );
}

// returns the rendered opencv image. NULL on error. It is the caller's
// responsibility to free this image's memory
IplImage* render_terrain( float view_lat, float view_lon, float* elevation,
                          int width, float fovy_deg, // render parameters. negative to take defaults
                          bool do_bgr )
{
  if( width    <= 0 ) width    = OFFSCREEN_W_DEFAULT;
  if( fovy_deg <= 0 ) fovy_deg = FOVY_DEG_DEFAULT;

  offscreen_w = width;
  offscreen_h = OFFSCREEN_H(width, fovy_deg);

  if( !setup_gl( false, view_lat, view_lon, elevation ) )
    return NULL;

  window_reshape(offscreen_w, offscreen_h);
  do_draw();

  IplImage* img = readOffscreenPixels( do_bgr );
  glutExit();
  return img;
}

bool render_terrain_to_window( float view_lat, float view_lon )
{
  if( setup_gl( true, view_lat, view_lon, NULL ) )
  {
    glutMainLoop();
    return true;
  }
  return false;
}
