#define _GNU_SOURCE

#include <tgmath.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include <epoxy/gl.h>
#include <epoxy/glx.h>

#include <GL/freeglut.h>
#include <unistd.h>
#include <string.h>

#include "bench.h"
#include "dem.h"
#include "util.h"


//////////////////// These are all used for 360-deg panorama renders. Leave them
//////////////////// at 0 otherwise

// We will render a square grid of data that is at most RENDER_RADIUS cells away
// from the viewer in the N or E direction
#define RENDER_RADIUS       1000

// for texture rendering
#define OSM_RENDER_ZOOM     13
#define OSM_TILE_WIDTH      256
#define OSM_TILE_HEIGHT     256


typedef struct
{
    int    Ntriangles;
    GLint  uniform_aspect;
    float  elevation_viewer;

    enum { PM_FILL, PM_LINE, PM_POINT, PM_NUM } PolygonMode;
} horizonator_context_t;


#define assert_opengl()                                 \
    do {                                                \
        int error = glGetError();                       \
        if( error != GL_NO_ERROR )                      \
        {                                               \
            MSG("Error: %#x! Giving up", error);        \
            assert(0);                                  \
        }                                               \
    } while(0)

static bool init( // output
                 horizonator_context_t* ctx,

                 // input
                 bool render_offscreen,
                 bool do_render_texture,

                 // Will be nudged a bit. The latlon we will use are
                 // returned in the context
                 float viewer_lat, float viewer_lon,

                 // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                 // edges lie at the edges of the image. So for an image that's
                 // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                 // elevation extents will be chosen to keep the aspect ratio
                 // square.
                 float az_deg0, float az_deg1)
{
    glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
    glutInitContextVersion(4,2);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInit(&(int){1}, &(char*){"exec"});
    glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH | ( render_offscreen ? 0 : GLUT_DOUBLE ));

    // when offscreen, I really don't want to glutCreateWindow(), but for some
    // reason not doing this causes glewInit() to segfault...
    glutCreateWindow("Terrain renderer");

    const char* version = (const char*)glGetString(GL_VERSION);
    MSG("glGetString(GL_VERSION) says we're using GL %s", version);
    MSG("Epoxy says we're using GL %d", epoxy_gl_version());

    if (version[0] == '1') {
        /* check for individual extensions */
        if (!glutExtensionSupported("GL_ARB_vertex_shader")) {
            MSG("Sorry, GL_ARB_vertex_shader is required.");
            return false;
        }
        if (!glutExtensionSupported("GL_ARB_fragment_shader")) {
            MSG("Sorry, GL_ARB_fragment_shader is required.");
            return false;
        }
        if (!glutExtensionSupported("GL_ARB_vertex_buffer_object")) {
            MSG("Sorry, GL_ARB_vertex_buffer_object is required.");
            return false;
        }
        if (!glutExtensionSupported("GL_EXT_framebuffer_object")) {
            MSG("GL_EXT_framebuffer_object not found!");
            return false;
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0, 0, 1, 0);


    // Viewer is looking north, the seam is behind (to the south).
    bool result = false;

    bool dem_context_inited = false;
    dem_context_t dem_context;
    if( !dem_init( &dem_context,
                   viewer_lat, viewer_lon, RENDER_RADIUS ) )
    {
        MSG("Couldn't init DEMs. Giving up");
        goto done;
    }
    dem_context_inited = true;

    // Dense triangulation. This may be adjusted below
    int Nvertices   = (2*RENDER_RADIUS) * (2*RENDER_RADIUS);
    ctx->Ntriangles = (2*RENDER_RADIUS - 1)*(2*RENDER_RADIUS - 1) * 2;


    // The viewer elevation. I nudge it up a tiny bit to not see the stuff
    // immediately around me
    const float viewer_z = fmaxf( fmaxf(dem_sample( &dem_context, RENDER_RADIUS-1, RENDER_RADIUS-1),
                                        dem_sample( &dem_context, RENDER_RADIUS,   RENDER_RADIUS-1)),
                                  fmaxf(dem_sample( &dem_context, RENDER_RADIUS-1, RENDER_RADIUS  ),
                                        dem_sample( &dem_context, RENDER_RADIUS,   RENDER_RADIUS  )) ) + 1.0;

    float viewer_cell[2];
    for(int i=0; i<2; i++)
        viewer_cell[i] =
            (dem_context.viewer_lon_lat[i] - dem_context.origin_dem_lon_lat[i]) * CELLS_PER_DEG - dem_context.origin_dem_cellij[i];

    // we're doing a equirectangular projection, so we must take care of the
    // seam. The camera always looks north, so the seam is behind us. Behind me
    // are two rows of vertices, one on either side. With a equirectangular
    // projection, these rows actually appear on opposite ends of the resulting
    // image, and thus I do not want to simply add triangles into this gap.
    // Instead, I double-up each of these rows, place the duplicated vertices
    // off screen (angle < -pi for one row and angle > pi for the other), and
    // render the seam twice, once for each side.
    //
    // The square I'm sitting on demands special treatment. I construct a
    // 6-triangle tiling that fully covers my window

    // Don't render the normal thing at the viewer cell. This never looks right.
    // We either render nothing, or we render a special thing, depending on the
    // settings
    ctx->Ntriangles -= 2;

    if(do_render_texture)
    {
        MSG("texturing not yet reviewed");
        assert(0);
#if 0
        // OSM tile texture business
        int NtilesX, NtilesY;
        int end_osmTileX, end_osmTileY;
        int start_osmTileX, start_osmTileY;
        float TEXTUREMAP_LON0, TEXTUREMAP_LON1;
        float TEXTUREMAP_LAT0, TEXTUREMAP_LAT1, TEXTUREMAP_LAT2;



        GLuint texID;
        glGenTextures(1, &texID);


        void initOSMtexture(void)
        {
            glActiveTextureARB( GL_TEXTURE0_ARB ); assert_opengl();
            glBindTexture( GL_TEXTURE_2D, texID ); assert_opengl();

            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

            // Init the whole texture with 0. Then later I'll fill it in tile by
            // tile
            glTexImage2D(GL_TEXTURE_2D, 0, 3,
                         NtilesX*OSM_TILE_WIDTH,
                         NtilesY*OSM_TILE_HEIGHT,
                         0, GL_BGR,
                         GL_UNSIGNED_BYTE, (const GLvoid *)NULL);
            assert_opengl();
        }

        void setOSMtextureTile( int osmTileX, int osmTileY )
        {
            // I read in an OSM tile. It is full of byte values RGB x width x
            // height
            char filename[256];
            char directory[256];
            char url[256];
            int len;

            len = snprintf(filename, sizeof(filename),
                           "/home/dima/.horizonator/tiles/%d/%d/%d.png",
                           OSM_RENDER_ZOOM, osmTileX, osmTileY);
            assert(len < (int)sizeof(filename));

            len = snprintf(directory, sizeof(directory),
                           "/home/dima/.horizonator/tiles/%d/%d",
                           OSM_RENDER_ZOOM, osmTileX);
            assert(len < (int)sizeof(directory));

            len = snprintf(url, sizeof(url),
                           "http://tile.openstreetmap.org/%d/%d/%d.png",
                           OSM_RENDER_ZOOM, osmTileX, osmTileY);
            assert(len < (int)sizeof(url));


            if( access( filename, R_OK ) != 0 )
            {
                // tile doesn't exist. Make a directory for it and try to download
                char cmd[1024];
                len = snprintf( cmd, sizeof(cmd),
                                "mkdir -p %s && wget -O %s %s", directory, filename, url  );
                assert(len < sizeof(cmd));
                int res = system(cmd);
                assert( res == 0 );
            }

            IplImage* img = cvLoadImage( filename, CV_LOAD_IMAGE_COLOR );
            assert(img);
            assert( img->width  == OSM_TILE_WIDTH );
            assert( img->height == OSM_TILE_HEIGHT );

            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            (osmTileX - start_osmTileX)*OSM_TILE_WIDTH,
                            (osmTileY - start_osmTileY)*OSM_TILE_HEIGHT,
                            OSM_TILE_WIDTH, OSM_TILE_HEIGHT,
                            GL_BGR, GL_UNSIGNED_BYTE, (const GLvoid *)img->imageData);
            assert_opengl();

            cvReleaseImage(&img);
        }

        void computeTextureMapInterpolationCoeffs(float lat0)
        {
            float n = (float)( 1 << OSM_RENDER_ZOOM);

            TEXTUREMAP_LON0 = n / 2.0f;
            TEXTUREMAP_LON1 = n / ((float)M_PI * 2.0f);
            // I use 2nd order interpolation for the lat computations in the
            // shader. The interpolation is centered around the viewing position.
            // The spherical equirectangular lat-to-projection equation is (from
            // https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames)
            //
            //    y(x) = n/2 * (1 - log( (sin(x) + 1)/cos(x) ) / pi)
            //
            // The derivatives are
            //
            //    y'(x)  = -n/(2*pi*cos(x))
            //    y''(x) = -n/(2*pi*tan(x)/cos(x))
            //
            // Here everything is in radians. For simplicity, let
            //   k = -n/(2*pi), c = cos(x0), t = tan(x0), X = x-x0
            //
            //    y(x0)  = n/2 + k*log( t + 1/c )
            //    y'(x0) = k / c
            //    y''(x0)= k * t / c
            //
            // Thus
            //
            //    y(x) ~ y(x0) + y'(x0)*(x0-x) + 1/2*y''(x0)*(x0-x)^2 =
            //         ~ y(x0) + y'(x0)*X      + 1/2*y''(x0)*X^2      =
            lat0 *= (float)M_PI / 180.0f;
            float k = -n / ((float)M_PI * 2.0f);
            float t = tan( lat0 );
            float c = cos( lat0 );
            TEXTUREMAP_LAT0 = n/2.0f + k*logf( t + 1.0f/c );
            TEXTUREMAP_LAT1 = k / c;
            TEXTUREMAP_LAT2 = k * t / c / 2.0f;
        }

        void getOSMTileID( float E, float N, // input latlon, in degrees
                           int*  x, int*  y  // output tile indices
                           )
        {
            // from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
            float n = (float)( 1 << OSM_RENDER_ZOOM);

            // convert E,N to radians. The interpolation coefficients assume this
            E *= (float)M_PI/180.0f;
            N *= (float)M_PI/180.0f;

            *x = (int)( fminf( n, fmaxf( 0.0f, E*TEXTUREMAP_LON1 + TEXTUREMAP_LON0 )));
            *y = (int)( n/2.0f * (1.0f -
                                  logf( (sinf(N) + 1.0f)/cosf(N) ) /
                                  (float)M_PI) );
        }



        // My render data is in a grid centered on ctx->viewer_lat/ctx->viewer_lon, branching
        // RENDER_RADIUS*DEG_PER_CELL degrees in all 4 directions
        float start_E = ctx->viewer_lon - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float start_N = ctx->viewer_lat - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float end_E   = ctx->viewer_lon + (float)RENDER_RADIUS/CELLS_PER_DEG;
        float end_N   = ctx->viewer_lat + (float)RENDER_RADIUS/CELLS_PER_DEG;

        computeTextureMapInterpolationCoeffs(ctx->viewer_lat);
        getOSMTileID( start_E, start_N,
                      &start_osmTileX, &end_osmTileY ); // y tiles are ordered backwards
        getOSMTileID( end_E, end_N,
                      &end_osmTileX, &start_osmTileY ); // y tiles are ordered backwards

        NtilesX = end_osmTileX - start_osmTileX + 1;
        NtilesY = end_osmTileY - start_osmTileY + 1;

        initOSMtexture();

        for( int osmTileY = start_osmTileY; osmTileY <= end_osmTileY; osmTileY++ )
            for( int osmTileX = start_osmTileX; osmTileX <= end_osmTileX; osmTileX++ )
                setOSMtextureTile( osmTileX, osmTileY );


        float x_texture = TEXTUREMAP_LON1 * ctx->viewer_lon*M_PI/180.0f + TEXTUREMAP_LON0;
        x_texture       = (x_texture - (float)start_osmTileX) / (float)NtilesX;
#endif
    }

    // vertices
    //
    // I fill in the VBO. Each point is a 16-bit integer tuple
    // (ilon,ilat,height). The first 2 args are indices into the virtual DEM
    // (accessed with dem_sample). The height is in meters
    {
        GLuint vertexArrayID;
        glGenVertexArrays(1, &vertexArrayID);
        glBindVertexArray(vertexArrayID);

        GLuint vertexBufID;
        glGenBuffers(1, &vertexBufID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);

        glEnableVertexAttribArray(0);

#define VBO_USES_INTEGERS 1

#if defined VBO_USES_INTEGERS && VBO_USES_INTEGERS
        // 16-bit integers. Only one of the paths below work with these
        glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, 0, NULL);
        GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
#else
        // 32-bit floats. These take more space, but work with all the paths below
        glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
        GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
#endif

        int vertex_buf_idx = 0;

        for( int j=0; j<2*RENDER_RADIUS; j++ )
        {
            for( int i=0; i<2*RENDER_RADIUS; i++ )
            {
                int32_t z = dem_sample(&dem_context, i,j);

                // Several paths are available. These require corresponding
                // updates in the GLSL, and exist for testing
#if 0
                // The CPU does all the math for the data procesing.
#if defined VBO_USES_INTEGERS && VBO_USES_INTEGERS
#error "This path requires floating-point vertices"
#endif
                const float Rearth = 6371000.0;
                const float cos_viewer_lat = cosf( M_PI / 180.0f * dem_context.viewer_lon_lat[1] );
                float e = ((float)i - viewer_cell[0]) / CELLS_PER_DEG * Rearth * M_PI/180.f * cos_viewer_lat;
                float n = ((float)j - viewer_cell[1]) / CELLS_PER_DEG * Rearth * M_PI/180.f;
                float h = (float)z - viewer_z;

                float d_ne = hypotf(e,n);
                vertices[vertex_buf_idx++] = atan2f(e,n   ) / M_PI;
                vertices[vertex_buf_idx++] = atan2f(h,d_ne) / M_PI;
                vertices[vertex_buf_idx++] = d_ne;
#elif 0
                // The CPU does some of the math for the data procesing.
                // Requires 32-bit floats for the vertices (selected above).
#if defined VBO_USES_INTEGERS && VBO_USES_INTEGERS
#error "This path requires floating-point vertices"
#endif
                const float Rearth = 6371000.0;
                const float cos_viewer_lat = cosf( M_PI / 180.0f * dem_context.viewer_lon_lat[1] );
                float e = ((float)i - viewer_cell[0]) / CELLS_PER_DEG * Rearth * M_PI/180.f * cos_viewer_lat;
                float n = ((float)j - viewer_cell[1]) / CELLS_PER_DEG * Rearth * M_PI/180.f;
                float h = (float)z - viewer_z;

                vertices[vertex_buf_idx++] = e;
                vertices[vertex_buf_idx++] = n;
                vertices[vertex_buf_idx++] = h;
#else
                // Integers into the VBO. All the work done in the GPU
                vertices[vertex_buf_idx++] = i;
                vertices[vertex_buf_idx++] = j;
                vertices[vertex_buf_idx++] = z;
#endif
            }
        }

        int res = glUnmapBuffer(GL_ARRAY_BUFFER);
        assert( res == GL_TRUE );
        assert( vertex_buf_idx == Nvertices*3 );
    }

    // indices
    {
        GLuint indexBufID;
        glGenBuffers(1, &indexBufID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ctx->Ntriangles*3*sizeof(GLuint), NULL, GL_STATIC_DRAW);

        GLuint* indices = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
        int idx = 0;
        for( int j=0; j<(2*RENDER_RADIUS-1); j++ )
        {
            for( int i=0; i<(2*RENDER_RADIUS-1); i++ )
            {
                // Two triangles to represent a rectangular cell. I don't add
                // triangles for the cell the viewer is sitting on. Those always
                // look wrong
                if( i == RENDER_RADIUS-1 )
                {
                    if( j == RENDER_RADIUS-1 )
                        continue;
                }

                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 0);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 1);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 0);

                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 0);
                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 1);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 1);
            }
        }
        int res = glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
        assert( res == GL_TRUE );
        assert(idx == ctx->Ntriangles*3);
    }

    // shaders
    {
        // The shader transforms the VBO vertices into the view coord system. Each VBO
        // point is a 16-bit integer tuple (ilon,ilat,height). The first 2 args are
        // indices into the DEM. The height is in meters
        const GLchar* vertexShaderSource =
            do_render_texture ?
#include "vertex.textured.glsl.h"
            :
#include "vertex.colored.glsl.h"
            ;

        const GLchar* fragmentShaderSource =
            do_render_texture ?
#include "fragment.textured.glsl.h"
            :
#include "fragment.colored.glsl.h"
            ;

        const GLchar* geometryShaderSource =
#include "geometry.glsl.h"
            ;

        char msg[1024];
        int len;
        GLuint program = glCreateProgram();
        assert_opengl();


#define install_shader(type,TYPE)                                       \
        GLuint type ## Shader = glCreateShader(GL_ ## TYPE ## _SHADER); \
        assert_opengl();                                                \
                                                                        \
        glShaderSource(type ## Shader, 1, (const GLchar**)&type ## ShaderSource, NULL); \
        assert_opengl();                                                \
                                                                        \
        glCompileShader(type ## Shader);                                \
        assert_opengl();                                                \
        glGetShaderInfoLog( type ## Shader, sizeof(msg), &len, msg );   \
        if( strlen(msg) )                                               \
            printf(#type " shader info: %s\n", msg);                    \
                                                                        \
        glAttachShader(program, type ##Shader);                         \
        assert_opengl();



        install_shader(vertex,   VERTEX);
        install_shader(fragment, FRAGMENT);
        install_shader(geometry, GEOMETRY);

        MSG("glLinkProgram"); glLinkProgram(program); assert_opengl();
        glGetProgramInfoLog( program, sizeof(msg), &len, msg );
        if( strlen(msg) )
            printf("program info after glLinkProgram(): %s\n", msg);

        MSG("glUseProgram"); glUseProgram(program);  assert_opengl();
        glGetProgramInfoLog( program, sizeof(msg), &len, msg );
        if( strlen(msg) )
            printf("program info after glUseProgram: %s\n", msg);


#define make_uniform(gltype, name, expr) do {                           \
            GLint uniform_ ## name = glGetUniformLocation(program, #name); \
            assert_opengl();                                            \
            glUniform1 ## gltype ( uniform_ ## name, expr);             \
            assert_opengl();                                            \
        } while(0)

        make_uniform(f, viewer_cell_i,  viewer_cell[0]);
        make_uniform(f, viewer_cell_j,  viewer_cell[1]);
        make_uniform(f, viewer_z,       viewer_z);
        make_uniform(f, DEG_PER_CELL,   1.0f/ (float)CELLS_PER_DEG );
        make_uniform(f, viewer_lon,     dem_context.viewer_lon_lat[0] * M_PI / 180.0f );
        make_uniform(f, viewer_lat,     dem_context.viewer_lon_lat[1] * M_PI / 180.0f );
        make_uniform(f, sin_viewer_lat, sin( M_PI / 180.0f * dem_context.viewer_lon_lat[1] ));
        make_uniform(f, cos_viewer_lat, cos( M_PI / 180.0f * dem_context.viewer_lon_lat[1] ));
        make_uniform(f, az_deg0,        az_deg0);
        make_uniform(f, az_deg1,        az_deg1);

        // This may be modified at runtime, so I do it manually, without make_uniform()
        ctx->uniform_aspect = glGetUniformLocation(program, "aspect");

        if( do_render_texture )
        {
            MSG("texturing not yet reviewed");
            assert(0);
#if 0
            make_uniform(f, TEXTUREMAP_LON0, TEXTUREMAP_LON0);
            make_uniform(f, TEXTUREMAP_LON1, TEXTUREMAP_LON1);
            make_uniform(f, TEXTUREMAP_LAT0, TEXTUREMAP_LAT0);
            make_uniform(f, TEXTUREMAP_LAT1, TEXTUREMAP_LAT1);
            make_uniform(f, TEXTUREMAP_LAT2, TEXTUREMAP_LAT2);
            make_uniform(i, NtilesX,         NtilesX);
            make_uniform(i, NtilesY,         NtilesY);
            make_uniform(i, start_osmTileX,  start_osmTileX);
            make_uniform(i, start_osmTileY,  start_osmTileY);
#endif
        }

#undef make_uniform
    }

    ctx->elevation_viewer = viewer_z;
    ctx->PolygonMode      = PM_FILL;

    result = true;

 done:
    if(dem_context_inited)
        dem_deinit(&dem_context);

    return result;
}


static void window_reshape(const horizonator_context_t* ctx, int width, int height)
{
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUniform1f(ctx->uniform_aspect, (float)width / (float)height);
}

static void draw(const horizonator_context_t* ctx)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const GLenum pmMap[] = {GL_FILL, GL_LINE, GL_POINT};
  glPolygonMode(GL_FRONT_AND_BACK, pmMap[ ctx->PolygonMode ] );
  glDrawElements(GL_TRIANGLES, ctx->Ntriangles*3, GL_UNSIGNED_INT, NULL);
}

// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* render_to_image(float viewer_lat, float viewer_lon,

                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1,

                      int width, int height )
{
    char* result = NULL;
    char* img    = NULL;

    horizonator_context_t ctx;

    if( !init( &ctx,
               true, false,
               viewer_lat, viewer_lon,
               az_deg0, az_deg1 ) )
        return NULL;

    GLuint frameBufID;
    {
      glGenFramebuffers(1, &frameBufID);
      assert_opengl();

      glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
      assert_opengl();
    }

    {
      GLuint renderBufID;
      glGenRenderbuffers(1, &renderBufID);
      assert_opengl();

      glBindRenderbuffer(GL_RENDERBUFFER, renderBufID);
      assert_opengl();

      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, width, height);
      assert_opengl();

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, renderBufID);
      assert_opengl();
    }

    {
      GLuint depthBufID;
      glGenRenderbuffers(1, &depthBufID);
      assert_opengl();

      glBindRenderbuffer(GL_RENDERBUFFER, depthBufID);
      assert_opengl();

      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
      assert_opengl();

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depthBufID);
      assert_opengl();
    }

    window_reshape(&ctx, width, height);
    draw(&ctx);

    img = malloc( (width) * (height) * 3 );
    if(img == NULL)
    {
        MSG("image buffer malloc() failed");
        goto done;
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0, width, height,
                 GL_BGR, GL_UNSIGNED_BYTE, img);
    glutExit();

    // depth-querying stuff
#if 0
    if( !ctx->dotexture )
    {
        if( ctx->depth == NULL )
        {
            ctx->depth = cvCreateMat( img->height, img->width, CV_8UC1 );
            assert(ctx->depth);
        }

        // I extract the third channel to a separate 'ctx->depth' image, and zero it out
        // in the image I draw. The ctx->depth image is used for picking only
        cvSetImageCOI( img, 3 );
        cvCopy( img, ctx->depth, NULL );
        cvSetImageCOI( img, 0 );
    }
#endif

    result = img;

 done:
    if(result == NULL)
    {
        free(img);
        // deinit(ctx);
    }
    return result;
}

bool render_to_window( float viewer_lat, float viewer_lon,

                       // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                       // edges lie at the edges of the image. So for an image that's
                       // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                       // elevation extents will be chosen to keep the aspect ratio
                       // square.
                       float az_deg0, float az_deg1 )
{
    horizonator_context_t ctx;

    if( !init( &ctx,
               false, false,
               viewer_lat, viewer_lon,
               az_deg0, az_deg1) )
        return false;

    void window_display(void)
    {
        draw(&ctx);
        glutSwapBuffers();
    }

    GLenum winding = GL_CCW;
    void window_keyPressed(unsigned char key,
                           int x __attribute__((unused)) ,
                           int y __attribute__((unused)) )
    {
        switch (key)
        {
        case 'w':
            if(++ctx.PolygonMode == PM_NUM)
                ctx.PolygonMode = 0;
            break;

        case 'r':
            if (winding == GL_CCW) winding = GL_CW;
            else                   winding = GL_CCW;
            glFrontFace(winding);
            break;

        case 'q':
        case 27:
            // Need both to avoid a segfault. This works differently with
            // different opengl drivers
            glutExit();
            exit(0);
        }

        glutPostRedisplay();
    }

    void _window_reshape(int width, int height)
    {
        window_reshape(&ctx, width, height);
    }

    glutDisplayFunc (window_display);
    glutKeyboardFunc(window_keyPressed);
    glutReshapeFunc (_window_reshape);

    int res = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert( res == GL_FRAMEBUFFER_COMPLETE );

    glutMainLoop();

    return true;
}

// pick and depth stuff
// not yet translated
#if 0
// returns true if an intersection is found
bool render_pick(// output
                 float* lon, float* lat,

                 // input
                 int x, int y )
{
    // I have rendered pixel coord (x,y); I want to map these to the source
    // triangles. The 'x' gives me the azimuth of the view. I also have a
    // 'ctx->depth' layer that gives me a distance along this azimuth.
    assert(ctx->depth);
    assert( x >= 0 && x < ctx->depth->cols &&
            y >= 0 && y < ctx->depth->rows );

    // If we have maximum ctx->depth, this click shoots above the mesh
    uint8_t d = ctx->depth->data.ptr[x + y*ctx->depth->cols];
    if( d == 255 )
        return false;

    // OK, we're pointing at the mesh. Let's get the vector direction. The ctx->depth
    // is the distance along that vector
    //
    // In my coordinate system, if I apply the small angle approximation, I get
    //
    // N = dlat
    // E = dlon cos(viewer_lat)
    // I have tan(az) = (-east)/(-north). I want to
    // break this into delta-n and delta-e => tan(az) = sin(az)/cos(az) =>
    // sin(az) = -east, cos(az) = -north
    float dn, de;
    sincosf((float)x * 2.0f * (float)M_PI / (float)ctx->image_width,
            &de, &dn);
    dn *= cos( (float)ctx->viewer_lat * (float)M_PI / 180.0f );

    float l = hypot(dn,de);
    *lon = ctx->viewer_lon - de*(float)d/l/255.0f;
    *lat = ctx->viewer_lat - dn*(float)d/l/255.0f;

    return true;
}

const CvMat* render_terrain_getdepth(void)
{
    return ctx->depth;
}
#endif
