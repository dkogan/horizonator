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

static const float   demfileN = 34.0f;
static const float   demfileW = 118.0f;
static const float   view_lat = 34.2883f; // peak of Iron Mt
static const float   view_lon = -117.7128f;
static unsigned char* dem;

// grid starts at the NW corner, and traverses along the latitude first.
// DEM tile is named from the SW point
#define lat_from_idx(j)   (  demfileN + 1.0f - (float)(j)/(float)(WDEM-1) )
#define lon_from_idx(i)   ( -demfileW        + (float)(i)/(float)(WDEM-1) )
#define idx_from_lat(lat) ( (demfileN + 1.0f - (lat)) * (float)(WDEM-1) )
#define idx_from_lon(lon) ( (demfileW        + (lon)) * (float)(WDEM-1) )


static const float Rearth = 6371000.0f;

static int doOverhead   = 0;
static int doOffscreen  = 0;
static int doNoMercator = 0;


static double extraHeight = 0.0;

static GLint aspectUniformLocation;

#define WDEM  1201
#define gridW 600
#define gridH 1200

#define OFFSCREEN_W 1024
#define OFFSCREEN_H 1024

// coordinate system has +x aimed at latlon=(0,0), +y at latlon=(0,90), +z north
static void getXYZ(GLfloat* v, float lat, float lon, float z)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  v[0] = cosf(lon)*cosf(lat)*(Rearth + z);
  v[1] = sinf(lon)*cosf(lat)*(Rearth + z);
  v[2] = sinf(lat)          *(Rearth + z);
}

static void getUpVector(GLfloat* vertices, float lat, float lon)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  vertices[0] = cosf(lon)*cosf(lat);
  vertices[1] = sinf(lon)*cosf(lat);
  vertices[2] = sinf(lat)          ;
}

static void getNorthVector(GLfloat* vertices, float lat, float lon)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  vertices[0] = -cosf(lon)*sinf(lat);
  vertices[1] = -sinf(lon)*sinf(lat);
  vertices[2] =  cosf(lat)          ;
}

static void getSouthVector(GLfloat* vertices, float lat, float lon)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  vertices[0] =  cosf(lon)*sinf(lat);
  vertices[1] =  sinf(lon)*sinf(lat);
  vertices[2] = -cosf(lat)          ;
}

static void getEastVector(GLfloat* vertices, float lat, float lon)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  vertices[0] = -sinf(lon)*cosf(lat);
  vertices[1] =  cosf(lon)*cosf(lat);
  vertices[2] = 0.0f;
}

static void getWestVector(GLfloat* vertices, float lat, float lon)
{
  lat *= M_PI/180.0f;
  lon *= M_PI/180.0f;

  vertices[0] =  sinf(lon)*cosf(lat);
  vertices[1] = -cosf(lon)*cosf(lat);
  vertices[2] = 0.0f;
}

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
  snprintf(filename, sizeof(filename), "../N%dW%d.srtm3.hgt", (int)demfileN, (int)demfileW);

  struct stat sb;
  int fd = open( filename, O_RDONLY );
  assert(fd > 0);
  assert( fstat(fd, &sb) == 0 );
  dem = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert( dem != MAP_FAILED );
  assert(  WDEM*WDEM*2 == sb.st_size );


  Nvertices = (gridW + 1) * (gridH + 1);
  Ntriangles = gridW*gridH*2;



  // vertices
  {
    GLuint vertexBufID;
    glGenBuffers(1, &vertexBufID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);
    glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLfloat), NULL, GL_STATIC_DRAW);
    glVertexPointer(3, GL_FLOAT, 0, NULL);

    GLfloat* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<=gridH; j++ )
    {
      for( int i=0; i<=gridW; i++ )
      {
        getXYZ(&vertices[idx],
               lat_from_idx(j),
               lon_from_idx(i),
               (float)getDemAt(i,j) );
        idx += 3;
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
      "#version 110\n"
      "uniform float aspect;"
      "varying float z;"
      "void main(void)"
      "{"
      "       vec4 v = gl_ModelViewMatrix * gl_Vertex;"
      "       z = length( gl_Vertex ) - 6371000.0;"
      "       z /= 3000.0;";

    const GLchar* vertexShaderSource_body_projection =
      "       gl_Position = gl_ProjectionMatrix * v;"
      "}";
    const GLchar* vertexShaderSource_body_mercator =
      "       const float znear = 10.0, zfar = 200000.0;"
      "       const float pi = 3.14159265358979;"
      "       float zeff  = length(vec2(v.x, v.z));"
      "       float angle = atan(v.x, -v.z) / pi;"

      // throw out the seam to ignore the wraparound triangles
      "       if( abs(angle) > 0.999 ) zeff = -100.0;"
      "       float A = (zfar + znear) / (zfar - znear);"
      "       float B = zfar * (1.0 - A);"
      "       gl_Position = vec4( angle * zeff,"
      "                           v.y / pi * aspect,"
      "                           zeff * A + B,"
      "                           zeff );"
      "}";

    const GLchar* fragmentShaderSource =
      "#version 110\n"
      "varying float z;"
      "void main(void)"
      "{"
      "       gl_FragColor = vec4(z, 0.0 ,0.0, 0.0);"
      "}";

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

    aspectUniformLocation = glGetUniformLocation(program, "aspect"); assert( glGetError() == GL_NO_ERROR );
  }
}


static void reshape(int width, int height)
{
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  GLdouble aspect = (GLdouble)width / (GLdouble)height;
  gluPerspective(72.2/aspect, aspect, 10.0, 200000.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUniform1f(aspectUniformLocation, aspect);
}


static void display(void)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glPushMatrix();


  GLfloat eye[3];
  GLfloat up[3];
  GLfloat view[3];

  if( doOverhead )
  {
    // overhead view
    float lat    = 34.5;
    float lon    = -117.5;
    float height = 100000.0;

    getUpVector(up, lat, lon);
    getXYZ(eye, lat, lon, height);

    for(int i=0; i<3; i++)
      view[i] = eye[i] - up[i];
    getNorthVector(up, lat, lon);
  }
  else
  {
    GLfloat viewdir[3];

    float height = getHeight(view_lat, view_lon) + extraHeight + 10.0f;
    assert(height > -1e3);

    getUpVector(up, view_lat, view_lon);

    getNorthVector(viewdir, view_lat, view_lon);
    getXYZ(eye, view_lat, view_lon, height);

    for(int i=0; i<3; i++)
      view[i] = eye[i] + viewdir[i] - extraHeight/10000.0f*up[i];

  }

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt( eye[0],  eye[1],  eye[2],
             view[0], view[1], view[2],
             up[0],   up[1],   up[2] );

  static const GLenum pmMap[] = {GL_FILL, GL_LINE, GL_POINT};
  glPolygonMode(GL_FRONT, pmMap[ PolygonMode ] );
  glPolygonMode(GL_BACK,  GL_POINT );

  glEnable(GL_CULL_FACE);

  // draw
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_INDEX_ARRAY);
  glDrawElements(GL_TRIANGLES, Ntriangles*3, GL_UNSIGNED_INT, NULL);


  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_CULL_FACE);

  glPopMatrix();

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

  case 'i':
    extraHeight += 1000.0;
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
      {"overhead",   no_argument, &doOverhead,   1 },
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
