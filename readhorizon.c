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

static GLboolean Cull = GL_TRUE;
static GLboolean WireFrame = GL_FALSE;
static int Ntriangles;
static int Nvertices;

static int            demfileN = 34;
static int            demfileW = 118;
static unsigned char* dem;

static const double Rearth = 6371000.0;

static int doOverhead  = 0;
static int doOffscreen = 0;

#define WDEM  1201
#define gridW 600
#define gridH 1200

#define OFFSCREEN_W 1024
#define OFFSCREEN_H 1024

static void getLatLonPos(GLdouble* vertices, double lat, double lon, double height)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] = cos(lon)*cos(lat)*(Rearth + height);
  vertices[1] = sin(lon)*cos(lat)*(Rearth + height);
  vertices[2] = sin(lat)         *(Rearth + height);
}

static void getUpVector(GLdouble* vertices, double lat, double lon)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] = cos(lon)*cos(lat);
  vertices[1] = sin(lon)*cos(lat);
  vertices[2] = sin(lat)         ;
}

static void getNorthVector(GLdouble* vertices, double lat, double lon)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] = -cos(lon)*sin(lat);
  vertices[1] = -sin(lon)*sin(lat);
  vertices[2] =  cos(lat)         ;
}

static void getSouthVector(GLdouble* vertices, double lat, double lon)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] =  cos(lon)*sin(lat);
  vertices[1] =  sin(lon)*sin(lat);
  vertices[2] = -cos(lat)         ;
}

static void getEastVector(GLdouble* vertices, double lat, double lon)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] = -sin(lon)*cos(lat);
  vertices[1] =  cos(lon)*cos(lat);
  vertices[2] = 0;
}

static void getWestVector(GLdouble* vertices, double lat, double lon)
{
  lat *= M_PI/180.0;
  lon *= M_PI/180.0;

  vertices[0] =  sin(lon)*cos(lat);
  vertices[1] = -cos(lon)*cos(lat);
  vertices[2] = 0;
}

static short getDemAt(int i, int j)
{
  int p = i + j*WDEM;
  short z = (short) ((dem[2*p] << 8) | dem[2*p + 1]);
  if(z < 0)
    z = 0;
  return z;
}

static double getHeight(double lat, double lon)
{
  // grid starts at the NW corner, and traverses along the latitude first.
  // coordinate system has +x aimed at lon=0, +y at lon=+90, +z north
  // DEM tile is named from the SW point
  int j = floor( ((double)demfileN + 1.0 - lat) * (double)(WDEM-1) );
  int i = floor( ((double)demfileW       + lon) * (double)(WDEM-1) );

  // return the largest height in the 4 neighboring cells
  double z = -1e20;
#define inrange(i, j) ( (i) >= 0 && (i) < WDEM && (j) >= 0 && (j) < WDEM )

  if( inrange(i,  j  ) ) z = fmax(z, (double) getDemAt(i,  j  ) );
  if( inrange(i+1,j  ) ) z = fmax(z, (double) getDemAt(i+1,j  ) );
  if( inrange(i,  j+1) ) z = fmax(z, (double) getDemAt(i,  j+1) );
  if( inrange(i+1,j+1) ) z = fmax(z, (double) getDemAt(i+1,j+1) );

#undef inrange
  return z;
}

static void getXYZ(GLfloat* vertices, int i, int j)
{
  // grid starts at the NW corner, and traverses along the latitude first.
  // coordinate system has +x aimed at lon=0, +y at lon=+90, +z north
  // DEM tile is named from the SW point
  float lat = ( (float) demfileN + 1.0f - (float)j/(float)(WDEM-1) ) * M_PI/180.0f;
  float lon = ( (float)-demfileW        + (float)i/(float)(WDEM-1) ) * M_PI/180.0f;

  float z = (float)getDemAt(i,j);

  vertices[0] = cosf(lon)*cosf(lat)*(Rearth + z);
  vertices[1] = sinf(lon)*cosf(lat)*(Rearth + z);
  vertices[2] = sinf(lat)          *(Rearth + z);
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
        getXYZ(&vertices[idx], i, j);
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

  // color shader
  {
    const GLchar* vertexShaderSource =
      "varying float z;\n"
      "void main(void)\n"
      "{\n"
      "       gl_Position=gl_ModelViewProjectionMatrix * gl_Vertex;\n"
      "       z = sqrt(gl_Vertex.x*gl_Vertex.x + gl_Vertex.y*gl_Vertex.y + gl_Vertex.z*gl_Vertex.z) - 6371000.0;\n"
      "}\n";

    const GLchar* fragmentShaderSource =
      "varying float z;\n"
      "void main(void)\n"
      "{\n"
      "       gl_FragColor.r = z/3000.0;\n"
      "}\n";

    GLuint vertexShader   = glCreateShader(GL_VERTEX_SHADER);   assert( glGetError() == GL_NO_ERROR );
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER); assert( glGetError() == GL_NO_ERROR );
    glShaderSource(vertexShader,   1, (const GLchar**)&vertexShaderSource,   0); assert( glGetError() == GL_NO_ERROR );
    glShaderSource(fragmentShader, 1, (const GLchar**)&fragmentShaderSource, 0); assert( glGetError() == GL_NO_ERROR );
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
}


static void display(void)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glPushMatrix();


  GLdouble eye[3];
  GLdouble up[3];
  GLdouble north[3];
  GLdouble view[3];

  if( doOverhead )
  {
    // overhead view
    double lat    = 34.5;
    double lon    = -117.5;
    double height = 100000.0;

    getUpVector(up, lat, lon);
    getLatLonPos(eye, lat, lon, height);

    for(int i=0; i<3; i++)
      view[i] = eye[i] - up[i];
    getNorthVector(up, lat, lon);
  }
  else
  {
    double lat    = 34.1;
    double lon    = -117.7;
    double height = getHeight(lat, lon) + 2.0;
    assert(height > -1e3);

    getNorthVector(north, lat, lon);
    getLatLonPos(eye, lat, lon, height);

    for(int i=0; i<3; i++)
      view[i] = eye[i] + north[i];

    getUpVector(up, lat, lon);
  }

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt( eye[0],  eye[1],  eye[2],
             view[0], view[1], view[2],
             up[0],   up[1],   up[2] );

  if (WireFrame)
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  else
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  if (Cull)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);


  // draw
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_INDEX_ARRAY);
  glDrawElements(GL_TRIANGLES, Ntriangles*3, GL_UNSIGNED_INT, NULL);


  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_CULL_FACE);

  glPopMatrix();

  glutSwapBuffers();
}


static void DoFeatureChecks(void)
{
  char *version = (char *) glGetString(GL_VERSION);
  if (version[0] == '1') {
    /* check for individual extensions */
    if (!glutExtensionSupported("GL_ARB_texture_cube_map")) {
      printf("Sorry, GL_ARB_texture_cube_map is required.\n");
      exit(1);
    }
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
  }
}

static void createOffscreenTargets(void)
{
  // create a renderbuffer object to store depth info
  GLuint renderBufID;
  glGenRenderbuffers(1, &renderBufID);
  glBindRenderbuffer(GL_RENDERBUFFER, renderBufID);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, OFFSCREEN_W, OFFSCREEN_W);

  GLuint frameBufID;
  glGenFramebuffers(1, &frameBufID);
  glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);

  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, renderBufID);

  assert( glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE );
}

static void readOffscreenPixels(void)
{
  FILE* fp = fopen("out.ppm", "w+");
  assert(fp);
  fprintf(fp, "P6\n%d %d\n255\n", OFFSCREEN_W, OFFSCREEN_H);

  // instead of allocating memory, I'd like to glMapBuffer(). This doesn't work
  // for unclear reasons, so I do this instead
  GLubyte* rgb = malloc(3*OFFSCREEN_W*OFFSCREEN_H);
  glReadPixels(0,0, OFFSCREEN_W, OFFSCREEN_H,
               GL_RGB, GL_UNSIGNED_BYTE, rgb);
  assert(rgb);
  fwrite(rgb, 3, OFFSCREEN_W*OFFSCREEN_H, fp);
  fclose(fp);
  free(rgb);
}

static void keyPressed(unsigned char key, int x, int y)
{
  static GLenum winding = GL_CCW;

  switch (key)
  {
  case 'w':
    WireFrame = !WireFrame;
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
  glutInit(&(int){1}, argv);
  glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
  glutCreateWindow("objview");
  glewInit();
  DoFeatureChecks();

  if(argc > 1)
  {
    doOverhead  = !strcmp(argv[1], "overhead");
    doOffscreen = !strcmp(argv[1], "offscreen");
  }

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
