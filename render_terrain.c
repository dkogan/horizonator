#define _GNU_SOURCE

#include <tgmath.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "bench.h"
#include "dem.h"


// can be used for testing/debugging to turn off the seam rendering
#define NOSEAM              1

// We will render a square grid of data that is at most RENDER_RADIUS cells away
// from the viewer in the N or E direction
#define RENDER_RADIUS       1000

#define IMAGE_HEIGHT(width, fovy_deg) (int)( 0.5 + width / 360.0 * fovy_deg)


#define FOVY_DEG_DEFAULT    50.0f /* vertical field of view of the render */
#define IMAGE_WIDTH_DEFAULT 4000
#define IMAGE_HEIGHT_DEFAULT IMAGE_HEIGHT(IMAGE_WIDTH_DEFAULT, FOVY_DEG_DEFAULT)


// for texture rendering
#define OSM_RENDER_ZOOM     13
#define OSM_TILE_WIDTH      256
#define OSM_TILE_HEIGHT     256


typedef struct
{
    int    Ntriangles;
    GLint  uniform_aspect;
    float  view_lon, view_lat;
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

                 // Will be nudged a bit. The latlon we will use are
                 // returned in the context
                 float _view_lat, float _view_lon,
                 bool do_render_texture )
{
    glutInit(&(int){1}, &(char*){"exec"});
    glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH | ( render_offscreen ? 0 : GLUT_DOUBLE ));

    // when offscreen, I really don't want to glutCreateWindow(), but for some
    // reason not doing this causes glewInit() to segfault...
    glutCreateWindow("Terrain renderer");
    glewInit();

    const char* version = (const char*)glGetString(GL_VERSION);
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
    glEnable(GL_NORMALIZE);
    glClearColor(0, 0, 1, 0);


    // Viewer is looking north, the seam is behind (to the south). If the viewer is
    // directly on a grid value, then the cell of the seam is poorly defined. In
    // that scenario, I nudge the viewer to one side to unambiguously pick the seam
    // cell. I do this in both lat and lon directions to resolve ambiguity.
    void nudgeCoord( float* view )
    {
        float cell_idx         = *view * CELLS_PER_DEG;
        float cell_idx_rounded = round( cell_idx );

        if( fabs( cell_idx - cell_idx_rounded ) < 0.1f )
        {
            if( cell_idx > cell_idx_rounded ) *view += 0.1f/CELLS_PER_DEG;
            else                              *view -= 0.1f/CELLS_PER_DEG;
        }
    }
    nudgeCoord( &_view_lat );
    nudgeCoord( &_view_lon );


    bool result = false;

    ctx->view_lon = _view_lon;
    ctx->view_lat = _view_lat;

    bool dem_context_inited = false;
    dem_context_t dem_context;
    if( !dem_init( &dem_context,
                   ctx->view_lat, ctx->view_lon, RENDER_RADIUS ) )
    {
        MSG("Couldn't init DEMs. Giving up");
        goto done;
    }
    dem_context_inited = true;

    int Nvertices   = (2*RENDER_RADIUS) * (2*RENDER_RADIUS);
    ctx->Ntriangles = (2*RENDER_RADIUS - 1)*(2*RENDER_RADIUS - 1) * 2;


    // The viewer elevation. I nudge it up a tiny bit to not see the stuff
    // immediately around me
    const float viewer_z = dem_elevation_at_center(&dem_context) + 1.0;

    // we're doing a mercator projection, so we must take care of the seam. The
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

    int Lseam = dem_context.center_ij[1]+1;

#if defined NOSEAM && NOSEAM
    ctx->Ntriangles -= (Lseam-1)*2;
    ctx->Ntriangles -= 2;            // Don't render anything at the viewer square
#else
    Nvertices       += Lseam*2;      // double-up the seam vertices
    ctx->Ntriangles += (Lseam-1)*2;  // Seam rendered twice. This is the extra one
    ctx->Ntriangles -= 2;            // Don't render the normal thing at the viewer square
    ctx->Ntriangles += 6;            // tiling at the viewer square
    Nvertices       += 2;            // the vertices in the bottom-left and
                                     // bottom-right of the image. used for
                                     // the viewer square
#endif

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
            assert( (unsigned)snprintf(filename, sizeof(filename),
                                       "/home/dima/.horizonator/tiles/%d/%d/%d.png",
                                       OSM_RENDER_ZOOM, osmTileX, osmTileY)
                    < sizeof(filename) );
            assert( (unsigned)snprintf(directory, sizeof(directory),
                                       "/home/dima/.horizonator/tiles/%d/%d",
                                       OSM_RENDER_ZOOM, osmTileX)
                    < sizeof(directory) );
            assert( (unsigned)snprintf(url, sizeof(url),
                                       "http://tile.openstreetmap.org/%d/%d/%d.png",
                                       OSM_RENDER_ZOOM, osmTileX, osmTileY)
                    < sizeof(url) );

            if( access( filename, R_OK ) != 0 )
            {
                // tile doesn't exist. Make a directory for it and try to download
                char cmd[1024];
                assert( snprintf( cmd, sizeof(cmd),
                                  "mkdir -p %s && wget -O %s %s", directory, filename, url  )
                        < sizeof(cmd) );
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
            // The spherical mercator lat-to-projection equation is (from
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



        // My render data is in a grid centered on ctx->view_lat/ctx->view_lon, branching
        // RENDER_RADIUS*DEG_PER_CELL degrees in all 4 directions
        float start_E = ctx->view_lon - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float start_N = ctx->view_lat - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float end_E   = ctx->view_lon + (float)RENDER_RADIUS/CELLS_PER_DEG;
        float end_N   = ctx->view_lat + (float)RENDER_RADIUS/CELLS_PER_DEG;

        computeTextureMapInterpolationCoeffs(ctx->view_lat);
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


        float x_texture = TEXTUREMAP_LON1 * ctx->view_lon*M_PI/180.0f + TEXTUREMAP_LON0;
        x_texture       = (x_texture - (float)start_osmTileX) / (float)NtilesX;
#endif
    }

    // vertices
    //
    // I fill in the VBO. Each point is a 16-bit integer tuple
    // (ilon,ilat,height). The first 2 args are indices into the virtual DEM
    // (accessed with dem_sample). The height is in meters
    {
        GLuint vertexBufID;
        glGenBuffers(1, &vertexBufID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);
        glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
        glVertexPointer(3, GL_SHORT, 0, NULL);

        GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        int vertex_buf_idx = 0;

        for( int j=0; j<2*RENDER_RADIUS; j++ )
            for( int i=0; i<2*RENDER_RADIUS; i++ )
            {
                vertices[vertex_buf_idx++] = i;
                vertices[vertex_buf_idx++] = j;
                vertices[vertex_buf_idx++] = dem_sample(&dem_context, i,j);
            }

#if !(defined NOSEAM && NOSEAM)
        for( int j=0; j<Lseam; j++ )
        {
            // These duplicates have the same geometry as the originals, but the
            // shader will project them differently, by moving the resulting angle
            // by 2*pi

            // left side; j<0 to indicate that this is a duplicate for the left
            // seam. Extra 1 because -0 is not < 0
            vertices[vertex_buf_idx++] = dem_context.center_ij[0];
            vertices[vertex_buf_idx++] = -(j+1);
            vertices[vertex_buf_idx++] = dem_sample(&dem_context,
                                                    dem_context.center_ij[0], j);


            // right side; i<0 to indicate that this is a duplicate for the
            // right seam. Extra 1 because -0 is not < 0
            vertices[vertex_buf_idx++] = -(dem_context.center_ij[0]+1);
            vertices[vertex_buf_idx++] = j;
            vertices[vertex_buf_idx++] = dem_sample(&dem_context,
                                                    dem_context.center_ij[0]+1, j);
        }

        // Two magic extra vertices used for the square I'm on: the bottom-left
        // of screen and the bottom-right of screen. The vertex coordinates here
        // are bogus. They are just meant to indicate to the shader to use
        // hard-coded transformed coords. (neg neg neg) means bottom-left. (neg
        // neg pos) means bottom-right
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
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ctx->Ntriangles*3*sizeof(GLuint), NULL, GL_STATIC_DRAW);

        GLuint* indices = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
        int idx = 0;
        for( int j=0; j<(2*RENDER_RADIUS-1); j++ )
        {
            for( int i=0; i<(2*RENDER_RADIUS-1); i++ )
            {
                // Seam
                if( i == dem_context.center_ij[0])
                {
                    if( j == dem_context.center_ij[1] )
                    {
#if !(defined NOSEAM && NOSEAM)
                        // This is the cell the viewer is sitting on. It needs
                        // special treatment

#define BEHIND_LEFT_NOT_MIRRORED              (j + 0)*(2*RENDER_RADIUS) + (i + 0)
#define BEHIND_RIGHT_NOT_MIRRORED             (j + 0)*(2*RENDER_RADIUS) + (i + 1)
#define FRONT_LEFT                            (j + 1)*(2*RENDER_RADIUS) + (i + 0)
#define FRONT_RIGHT                           (j + 1)*(2*RENDER_RADIUS) + (i + 1)
#define BEHIND_LEFT_MIRRORED_PAST_RIGHT_EDGE  (2*RENDER_RADIUS)*(2*RENDER_RADIUS) + j*2
#define BEHIND_RIGHT_MIRRORED_PAST_LEFT_EDGE  (2*RENDER_RADIUS)*(2*RENDER_RADIUS) + j*2 + 1
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

                    else if( j < Lseam )
                    {
#if !(defined NOSEAM && NOSEAM)
                        // seam. I add two sets of triangles here; one for the left edge of
                        // the screen and one for the right

                        // right edge:
                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) +  j     *2;
                        indices[idx++] = (j + 1)     *(2*RENDER_RADIUS) + (i + 1);
                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) + (j + 1)*2;

                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) +  j*2;
                        indices[idx++] = (j + 0)     *(2*RENDER_RADIUS) + (i + 1);
                        indices[idx++] = (j + 1)     *(2*RENDER_RADIUS) + (i + 1);

                        // left edge:
                        indices[idx++] = (j + 0)     *(2*RENDER_RADIUS) + (i + 0);
                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) + (j + 1)*2 + 1;
                        indices[idx++] = (j + 1)     *(2*RENDER_RADIUS) + (i + 0);

                        indices[idx++] = (j + 0)     *(2*RENDER_RADIUS) + (i + 0);
                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) +  j     *2 + 1;
                        indices[idx++] = (2*RENDER_RADIUS)*(2*RENDER_RADIUS) + (j + 1)*2 + 1;
#endif

                        continue;
                    }
                }

                // Non-seam. Two triangles to represent a rectangular cell
                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 0);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 1);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 0);

                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 0);
                indices[idx++] = (j + 0)*(2*RENDER_RADIUS) + (i + 1);
                indices[idx++] = (j + 1)*(2*RENDER_RADIUS) + (i + 1);
            }
        }
        assert( glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) == GL_TRUE );
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
            printf(#type " msg: %s\n", msg);                            \
                                                                        \
        glAttachShader(program, type ##Shader);                         \
        assert_opengl();



        install_shader(vertex,   VERTEX);
        install_shader(fragment, FRAGMENT);

        glLinkProgram(program); assert_opengl();
        glUseProgram(program);  assert_opengl();


#define make_uniform(gltype, name, expr) do {                           \
            GLint uniform_ ## name = glGetUniformLocation(program, #name); \
            assert_opengl();                                            \
            glUniform1 ## gltype ( uniform_ ## name, expr);             \
            assert_opengl();                                            \
        } while(0)

        make_uniform(f, view_z,       viewer_z);
        make_uniform(f, origin_N,     dem_context.origin_lon_lat[1]);
        make_uniform(f, origin_W,     dem_context.origin_lon_lat[0]);
        make_uniform(f, DEG_PER_CELL, 1.0f/ (float)CELLS_PER_DEG );
        make_uniform(f, view_lon,     ctx->view_lon * M_PI / 180.0f );
        make_uniform(f, view_lat,     ctx->view_lat * M_PI / 180.0f );
        make_uniform(i, center_ij0,   dem_context.center_ij[0] );
        make_uniform(i, center_ij1,   dem_context.center_ij[1] );
        make_uniform(f, sin_view_lat, sin( M_PI / 180.0f * ctx->view_lat ));
        make_uniform(f, cos_view_lat, cos( M_PI / 180.0f * ctx->view_lat ));

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
  {
      glEnable(GL_CULL_FACE);
      glEnableClientState(GL_VERTEX_ARRAY);
      glDrawElements(GL_TRIANGLES, ctx->Ntriangles*3, GL_UNSIGNED_INT, NULL);
  }
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_CULL_FACE);
}

// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* render_to_image(// output
                      int* image_width, int* image_height,

                      // input
                      float view_lat, float view_lon,
                      int width, float fovy_deg // render parameters. negative to take defaults
                      )
{
    char* result = NULL;
    char* img    = NULL;

    horizonator_context_t ctx;

    if( !init( &ctx,
               true, view_lat, view_lon, false ) )
        return NULL;

    *image_width  = width > 0 ? width : IMAGE_WIDTH_DEFAULT;

    if( fovy_deg <= 0 ) fovy_deg = FOVY_DEG_DEFAULT;
    *image_height = IMAGE_HEIGHT(*image_width, fovy_deg);

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

      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, *image_width, *image_height);
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

      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, *image_width, *image_height);
      assert_opengl();

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depthBufID);
      assert_opengl();
    }

    window_reshape(&ctx, *image_width, *image_height);
    draw(&ctx);

    img = malloc( (*image_width) * (*image_height) * 3 );
    if(img == NULL)
    {
        MSG("image buffer malloc() failed");
        goto done;
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0, *image_width, *image_height,
                 GL_RGB, GL_UNSIGNED_BYTE, img);
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

bool render_to_window( float view_lat, float view_lon )
{
    horizonator_context_t ctx;

    if( !init( &ctx,
               false, view_lat, view_lon, false ) )
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

    assert( glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE );

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
    // E = dlon cos(view_lat)
    // I have tan(az) = (-east)/(-north). I want to
    // break this into delta-n and delta-e => tan(az) = sin(az)/cos(az) =>
    // sin(az) = -east, cos(az) = -north
    float dn, de;
    sincosf((float)x * 2.0f * (float)M_PI / (float)ctx->image_width,
            &de, &dn);
    dn *= cos( (float)ctx->view_lat * (float)M_PI / 180.0f );

    float l = hypot(dn,de);
    *lon = ctx->view_lon - de*(float)d/l/255.0f;
    *lat = ctx->view_lat - dn*(float)d/l/255.0f;

    return true;
}

const CvMat* render_terrain_getdepth(void)
{
    return ctx->depth;
}
#endif

/*
get rid of CvFlip(). Should render what I need to begin with

The mapping of pixel <-> azel should be crystal clear

dem_sample now indexes ES not EN. And needs a context

should make sure the view origin is not quantized to the cells

Are there artifacts at the seam?
 */
