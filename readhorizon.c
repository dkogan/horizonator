#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <opencv2/highgui/highgui_c.h>

static enum { PM_FILL, PM_LINE, PM_POINT, PM_NUM } PolygonMode = PM_FILL;
static int Ntriangles;
static int Nvertices;

static const int     demfileN = 34.0;
static const int     demfileW = 118.0;
static const float   view_lat = 34.2883f; // peak of Iron Mt
static const float   view_lon = -117.7128f;
static unsigned char* dem;


// grid starts at the NW corner, and traverses along the latitude first.
// DEM tile is named from the SW point
#define lat_from_idx(j)   (  (float)demfileN + 1.0f - (float)(j)/(float)(WDEM-1) )
#define lon_from_idx(i)   ( -(float)demfileW        + (float)(i)/(float)(WDEM-1) )
#define idx_from_lat(lat) ( ((float)demfileN + 1.0f - (lat)) * (float)(WDEM-1) )
#define idx_from_lon(lon) ( ((float)demfileW        + (lon)) * (float)(WDEM-1) )


static int doOffscreen  = 0;
static int doNoMercator = 0;

static GLint uniform_view_z;
static GLint uniform_demfileN, uniform_demfileW;
static GLint uniform_WDEM;
static GLint uniform_sin_view_lon;
static GLint uniform_cos_view_lon;
static GLint uniform_sin_view_lat;
static GLint uniform_cos_view_lat;
static GLint uniform_aspect;

#define WDEM        1201
#define gridW       600
#define gridH       1200

#define IRON_ANGLE      72.2 /* angle of view for my iron mt photo */
#define IRON_WIDTH 3656

#define OFFSCREEN_W (int)( 1400.0/1050.0*OFFSCREEN_H /IRON_ANGLE * 360.0 + 0.5 )
#define OFFSCREEN_H 300



static short getDemAt(int i, int j)
{
  int p = i + j*WDEM;
  short z = (short) ((dem[2*p] << 8) | dem[2*p + 1]);
  if(z < 0)
    z = 0;
  return z;
}

static float getHeight(float lat, float lon)
{
  int i = floorf( idx_from_lon(lon) );
  int j = floorf( idx_from_lat(lat) );

  // return the largest height in the 4 neighboring cells
  float z = -1e20f;
#define inrange(i, j) ( (i) >= 0 && (i) < WDEM && (j) >= 0 && (j) < WDEM )

  if( inrange(i,  j  ) ) z = fmaxf(z, (float) getDemAt(i,  j  ) );
  if( inrange(i+1,j  ) ) z = fmaxf(z, (float) getDemAt(i+1,j  ) );
  if( inrange(i,  j+1) ) z = fmaxf(z, (float) getDemAt(i,  j+1) );
  if( inrange(i+1,j+1) ) z = fmaxf(z, (float) getDemAt(i+1,j+1) );

#undef inrange
  return z;
}

static void loadGeometry(void)
{
  char filename[1024];
  snprintf(filename, sizeof(filename), "../N%dW%d.srtm3.hgt", demfileN, demfileW);

  struct stat sb;
  int fd = open( filename, O_RDONLY );
  assert(fd > 0);
  assert( fstat(fd, &sb) == 0 );
  dem = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert( dem != MAP_FAILED );
  assert(  WDEM*WDEM*2 == sb.st_size );


  Nvertices = (gridW + 1) * (gridH + 1);
  Ntriangles = gridW*gridH*2;

  int Lseam = 0;
  int view_i, view_j;
  if( !doNoMercator )
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
    // Furthermore, I do not render the two triangles that span the cell that
    // the camera is in
    view_i = floorf( idx_from_lon(view_lon) );
    view_j = floorf( idx_from_lat(view_lat) );

    Lseam = gridH - view_j;

    Nvertices  += Lseam*2;      // double-up the seam vertices
    Ntriangles += (Lseam-1)*2;  // Seam rendered twice. This is the extra one
    Ntriangles -= 2;            // Don't render the triangles AT the viewer
  }

  // vertices
  {
    GLuint vertexBufID;
    glGenBuffers(1, &vertexBufID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);
    glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
    glVertexPointer(3, GL_SHORT, 0, NULL);

    GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<=gridH; j++ )
    {
      for( int i=0; i<=gridW; i++ )
      {
        vertices[idx++] = j;
        vertices[idx++] = i;
        vertices[idx++] = getDemAt(i,j);
      }
    }

    // add the extra seam vertices
    if( Lseam )
    {
      for( int j=view_j+1; j<=gridH; j++ )
      {
        // These duplicates have the same geometry as the originals, but the
        // shader will project them differently, by moving the resulting angle
        // by 2*pi

        // left side
        vertices[idx++] = j - WDEM; // negative to indicate that this is a duplicate for the seam
        vertices[idx++] =  view_i;
        vertices[idx++] = getDemAt(view_i,j);

        // right side
        vertices[idx++] = j - WDEM; // negative to indicate that this is a duplicate for the seam
        vertices[idx++] =  view_i+1;
        vertices[idx++] = getDemAt(view_i+1,j);
      }
    }

    assert( glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE );
    assert( idx == Nvertices*3 );
  }
  close(fd);

  // indices
  {
    GLuint indexBufID;
    glGenBuffers(1, &indexBufID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, Ntriangles*3*sizeof(GLuint), NULL, GL_STATIC_DRAW);

    GLuint* indices = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<gridH; j++ )
    {
      for( int i=0; i<gridW; i++ )
      {
        // seam? only do this if we're doing a mercator projection
        if( !doNoMercator && i == view_i)
        {
          // do not render the triangles the camera is sitting on
          if( j == view_j )
            continue;

          if( j >= view_j+1 )
          {
            // seam. I add two sets of triangles here; one for the left edge of
            // the screen and one for the right
            int jseam = j - (view_j + 1);

            // left edge:
            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2;
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2;
            indices[idx++] = (j + 1)*(gridW+1) + (i + 1);

            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2;
            indices[idx++] = (j + 1)*(gridW+1) + (i + 1);
            indices[idx++] = (j + 0)*(gridW+1) + (i + 1);

            // right edge:
            indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
            indices[idx++] = (j + 1)*(gridW+1) + (i + 0);
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2 + 1;

            indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2 + 1;
            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2 + 1;

            continue;
          }
        }

        // non-seam
        indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 1);

        indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 1);
        indices[idx++] = (j + 0)*(gridW+1) + (i + 1);
      }
    }
    assert( glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) == GL_TRUE );
    assert(idx == Ntriangles*3);
  }

  // shaders
  {
    const GLchar* vertexShaderSource_header =
"                                               \
#version 110\n                                  \
uniform float view_z;                           \
uniform int   demfileN, demfileW;               \
uniform int   WDEM;                             \
uniform float sin_view_lon;                     \
uniform float cos_view_lon;                     \
uniform float sin_view_lat;                     \
uniform float cos_view_lat;                     \
                                                \
uniform float aspect;                           \
varying float red;                              \
                                                \
void main(void)                                 \
{                                               \
  const float Rearth = 6371000.0;               \
  const float pi     = 3.14159265358979;        \
  const float znear  = 100.0, zfar = 200000.0;  \
";

#warning finish this
    const GLchar* vertexShaderSource_body_projection =
      "       gl_Position = gl_ProjectionMatrix * v;"
      "}";


    const GLchar* vertexShaderSource_body_mercator =
"                                                                       \
  /* gl_Vertex is (j,i,height) */                                       \
  bool at_seam;                                                         \
  vec3 vin = gl_Vertex.xyz;                                             \
  if( vin.x < 0.0 )                                                     \
  {                                                                     \
    vin.x += float(WDEM);                                               \
    at_seam = true;                                                     \
  }                                                                     \
  else                                                                  \
    at_seam = false;                                                    \
                                                                        \
  float lat = radians( float( demfileN + 1) - vin.x/float(WDEM-1) );    \
  float lon = radians( float(-demfileW)     + vin.y/float(WDEM-1) );    \
  float sin_lon  = sin( lon );                                          \
  float cos_lon  = cos( lon );                                          \
  float sin_lat  = sin( lat );                                          \
  float cos_lat  = cos( lat );                                          \
  float sin_dlat = sin_lat * cos_view_lat - cos_lat * sin_view_lat;     \
  float cos_dlat = cos_lat * cos_view_lat + sin_lat * sin_view_lat;     \
  float sin_dlon = sin_lon * cos_view_lon - cos_lon * sin_view_lon;     \
  float cos_dlon = cos_lon * cos_view_lon + sin_lon * sin_view_lon;     \
                                                                        \
  vec3 v = vec3( (Rearth + vin.z) * ( cos_lat * sin_dlon ),             \
                 (Rearth + vin.z) * ( cos_dlat*cos_dlon + sin_lat*sin_view_lat*(1.0 - cos_dlon) ), \
                 (Rearth + vin.z) * ( sin_dlat*cos_dlon + sin_lat*cos_view_lat*(1.0 - cos_dlon)) ); \
  /* this is bad for roundoff error */                                  \
  v.y -= Rearth + view_z;                                               \
                                                                        \
  float zeff  = length(vec2(v.x, v.z));                                 \
  float angle = atan(v.x, v.z) / pi;                                    \
  if( at_seam )                                                         \
  {                                                                     \
    if( angle > 0.0 )                                                   \
      angle -= 2.0;                                                     \
    else                                                                \
      angle += 2.0;                                                     \
  }                                                                     \
                                                                        \
  red = zeff / 10000.0;                                                 \
                                                                        \
  const float A = (zfar + znear) / (zfar - znear);                      \
  gl_Position = vec4( angle * zeff,                                     \
                      v.y / pi * aspect,                                \
                      mix(zfar, zeff, A),                               \
                      zeff );                                           \
}                                                                       \
";

    const GLchar* fragmentShaderSource =
      "                                                 \
      #version 110\n                                    \
      varying float red;                                \
      void main(void)                                   \
      {                                                 \
             gl_FragColor = vec4(red, 0.0 ,0.0, 0.0);   \
      }                                                 \
      ";

    GLuint vertexShader   = glCreateShader(GL_VERTEX_SHADER);   assert( glGetError() == GL_NO_ERROR );
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER); assert( glGetError() == GL_NO_ERROR );


    const GLchar* vertexShaderSource_body_pieces[] =
      {
        vertexShaderSource_header,
        NULL
      };
    vertexShaderSource_body_pieces[1] =
      doNoMercator ?
        vertexShaderSource_body_projection :
        vertexShaderSource_body_mercator;

    glShaderSource(vertexShader,
                   sizeof(vertexShaderSource_body_pieces)/sizeof(vertexShaderSource_body_pieces[0]),
                   vertexShaderSource_body_pieces,
                   NULL);
    assert( glGetError() == GL_NO_ERROR );

    glShaderSource(fragmentShader, 1, (const GLchar**)&fragmentShaderSource, NULL);
    assert( glGetError() == GL_NO_ERROR );

    glCompileShader(vertexShader);   assert( glGetError() == GL_NO_ERROR );


    char msg[1024];
    int len;
    glGetShaderInfoLog( vertexShader, sizeof(msg), &len, msg );
    if( strlen(msg) )
      printf("vertex msg: %s\n", msg);

    glCompileShader(fragmentShader); assert( glGetError() == GL_NO_ERROR );
    glGetShaderInfoLog( fragmentShader, sizeof(msg), &len, msg );
    if( strlen(msg) )
      printf("fragment msg: %s\n", msg);

    GLuint program = glCreateProgram();      assert( glGetError() == GL_NO_ERROR );
    glAttachShader(program, vertexShader);   assert( glGetError() == GL_NO_ERROR );
    glAttachShader(program, fragmentShader); assert( glGetError() == GL_NO_ERROR );
    glLinkProgram(program); assert( glGetError() == GL_NO_ERROR );
    glUseProgram(program);  assert( glGetError() == GL_NO_ERROR );

    uniform_view_z       = glGetUniformLocation(program, "view_z"      ); assert( glGetError() == GL_NO_ERROR );
    uniform_demfileN     = glGetUniformLocation(program, "demfileN"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_demfileW     = glGetUniformLocation(program, "demfileW"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_WDEM         = glGetUniformLocation(program, "WDEM"        ); assert( glGetError() == GL_NO_ERROR );
    uniform_sin_view_lon = glGetUniformLocation(program, "sin_view_lon"); assert( glGetError() == GL_NO_ERROR );
    uniform_cos_view_lon = glGetUniformLocation(program, "cos_view_lon"); assert( glGetError() == GL_NO_ERROR );
    uniform_sin_view_lat = glGetUniformLocation(program, "sin_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_cos_view_lat = glGetUniformLocation(program, "cos_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_aspect       = glGetUniformLocation(program, "aspect"      ); assert( glGetError() == GL_NO_ERROR );

    glUniform1f( uniform_view_z,       getHeight(view_lat, view_lon));
    glUniform1i( uniform_demfileN,     demfileN);
    glUniform1i( uniform_demfileW,     demfileW);
    glUniform1i( uniform_WDEM,         WDEM);
    glUniform1f( uniform_sin_view_lon, sinf( M_PI / 180.0f * view_lon ));
    glUniform1f( uniform_cos_view_lon, cosf( M_PI / 180.0f * view_lon ));
    glUniform1f( uniform_sin_view_lat, sinf( M_PI / 180.0f * view_lat ));
    glUniform1f( uniform_cos_view_lat, cosf( M_PI / 180.0f * view_lat ));
  }
}


static void reshape(int width, int height)
{
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  GLdouble aspect = (GLdouble)width / (GLdouble)height;

  // IRON_ANGLE
  double fovy = IRON_ANGLE/aspect;

  gluPerspective(fovy, aspect, 100.0, 200000.0);

  fprintf(stderr, "fovy: %f\n", fovy);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUniform1f(uniform_aspect, aspect);
}


static void display(void)
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

  if( !doOffscreen )
    glutSwapBuffers();
}


static void DoFeatureChecks(void)
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

static void createOffscreenTargets(void)
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

    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, OFFSCREEN_W, OFFSCREEN_H);
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

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, OFFSCREEN_W, OFFSCREEN_H);
    assert( glGetError() == GL_NO_ERROR );

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthBufID);
    assert( glGetError() == GL_NO_ERROR );
  }

  assert( glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE );
}

static void readOffscreenPixels(void)
{
  CvSize size = { .width  = OFFSCREEN_W,
                  .height = OFFSCREEN_H };

  IplImage* img = cvCreateImage(size, 8, 3);
  assert( img );

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0,0, OFFSCREEN_W, OFFSCREEN_H,
               GL_BGR, GL_UNSIGNED_BYTE, img->imageData);
  cvFlip(img, NULL, 0);
  cvSaveImage("out.png", img, (int[]){9,0}); // 9 == png quality, 0 == 'end of options'
  cvReleaseImage(&img);
}

static void keyPressed(unsigned char key, int x, int y)
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

int main(int argc, char** argv)
{
  static struct option long_options[] =
    {
      {"offscreen",  no_argument, &doOffscreen,  1 },
      {"nomercator", no_argument, &doNoMercator, 1 },
      {}
    };

  int getopt_res;
  do
  {
    getopt_res = getopt_long(argc, argv, "", long_options, NULL);
    if( getopt_res == '?' )
    {
      fprintf(stderr, "Unknown cmdline option encountered\n");
      exit(1);
    }
  } while(getopt_res != -1);


  glutInit(&(int){1}, argv);
  glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH | ( doOffscreen ? 0 : GLUT_DOUBLE ));
  glutCreateWindow("objview");
  glewInit();
  DoFeatureChecks();

  if( doOffscreen )
  {
    // when offscreen, I really don't want to glutCreateWindow(), but for some
    // reason not doing this causes glewInit() to segfault...
    createOffscreenTargets();
    loadGeometry();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);
    glClearColor(0.3, 0.3, 0.9, 0.0);

    reshape(OFFSCREEN_W, OFFSCREEN_H);
    display();

    readOffscreenPixels();
  }
  else
  {
    glutKeyboardFunc(keyPressed);

    glutReshapeFunc(reshape);
    glutDisplayFunc(display);

    loadGeometry();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);
    glClearColor(0.3, 0.3, 0.9, 0.0);

    glutMainLoop();
  }

  return 0;
}
