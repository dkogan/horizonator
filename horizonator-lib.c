#define _GNU_SOURCE

#include <tgmath.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include <epoxy/gl.h>
#include <epoxy/glx.h>
#include <GL/freeglut.h>

#include <FreeImage.h>

#include "horizonator.h"
#include "bench.h"
#include "dem.h"
#include "util.h"


//////////////////// These are all used for 360-deg panorama renders. Leave them
//////////////////// at 0 otherwise

// We will render a square grid of data that is at most RENDER_RADIUS cells away
// from the viewer in the N or E direction
#define RENDER_RADIUS       500

// for texture rendering
#define OSM_RENDER_ZOOM     12
#define OSM_TILE_WIDTH      256
#define OSM_TILE_HEIGHT     256

// these define the front and back clipping planes, in meters
#define ZNEAR 100.0f
#define ZFAR  40000.0f


#define assert_opengl()                                 \
    do {                                                \
        int error = glGetError();                       \
        if( error != GL_NO_ERROR )                      \
        {                                               \
            MSG("Error: %#x! Giving up", error);        \
            assert(0);                                  \
        }                                               \
    } while(0)


// The main init routine. We support 3 modes:
//
// - GLUT: static window    (use_glut = true, offscreen_width <= 0)
// - GLUT: offscreen render (use_glut = true, offscreen_width > 0)
// - no GLUT: higher-level application (use_glut = false)
//
// This routine loads the DEMs around the viewer (viewer is at the center of the
// DEMs). The render can then be updated by calling any of
// - horizonator_move()
// - horizonator_pan_zoom()
// - horizonator_resized()
// and then
// - horizonator_redraw()
//
// If rendering off-screen, horizonator_resized() is not allowed.
// horizonator_pan_zoom() must be called to update the azimuth extents.
// Completely arbitrarily, these are set to -45deg - 45deg initially
bool horizonator_init( // output
                       horizonator_context_t* ctx,

                       // input
                       bool use_glut,
                       int offscreen_width, int offscreen_height,
                       bool render_texture,
                       float viewer_lat, float viewer_lon,
                       const char* dir_dems,
                       const char* dir_tiles,
                       bool allow_downloads)
{
    bool result             = false;
    bool dem_context_inited = false;


    if(use_glut)
    {
        bool double_buffered = offscreen_width <= 0;

        glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
        glutInitContextVersion(4,2);
        glutInitContextProfile(GLUT_CORE_PROFILE);
        glutInit(&(int){1}, &(char*){"exec"});
        glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH |
                             (double_buffered ? GLUT_DOUBLE : 0) );

        // when offscreen, I really don't want to glutCreateWindow(), but for some
        // reason not doing this causes glewInit() to segfault...
        glutCreateWindow("horizonator");

        const char* version = (const char*)glGetString(GL_VERSION);

        // MSG("glGetString(GL_VERSION) says we're using GL %s", version);
        // MSG("Epoxy says we're using GL %d", epoxy_gl_version());

        if (version[0] == '1')
        {
            if (!glutExtensionSupported("GL_ARB_vertex_shader")) {
                MSG("Sorry, GL_ARB_vertex_shader is required.");
                goto done;
            }
            if (!glutExtensionSupported("GL_ARB_fragment_shader")) {
                MSG("Sorry, GL_ARB_fragment_shader is required.");
                goto done;
            }
            if (!glutExtensionSupported("GL_ARB_vertex_buffer_object")) {
                MSG("Sorry, GL_ARB_vertex_buffer_object is required.");
                goto done;
            }
            if (!glutExtensionSupported("GL_EXT_framebuffer_object")) {
                MSG("GL_EXT_framebuffer_object not found!");
                goto done;
            }
        }
    }

    static_assert(sizeof(GLint) == sizeof(ctx->uniform_aspect),
                  "horizonator_context_t.uniform_... must be a GLint");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0, 0, 1, 0);

    if( !horizonator_dem_init( &ctx->dems,
                   viewer_lat, viewer_lon,
                   RENDER_RADIUS,
                   dir_dems) )
    {
        MSG("Couldn't init DEMs. Giving up");
        goto done;
    }
    dem_context_inited = true;

    // Dense triangulation. This may be adjusted below
    int Nvertices   = (2*RENDER_RADIUS) * (2*RENDER_RADIUS);
    ctx->Ntriangles = (2*RENDER_RADIUS - 1)*(2*RENDER_RADIUS - 1) * 2;

    typedef struct
    {
        // How many tiles we have in each direction
        int NtilesXY[2];

        // Lowest and highest OSM tile indices. These increase towards E and
        // towards S (i.e. in the opposite direction as latitude)
        int osmtile_lowestXY [2];
        int osmtile_highestXY[2];

    } texture_ctx_t;
    texture_ctx_t texture_ctx = {};

    ctx->render_texture = render_texture;

    if(render_texture)
    {
        GLuint texID;
        glGenTextures(1, &texID);

        void getOSMTileID( // output tile indices
                          int* x, int* y,

                          // input
                          // latlon, in degrees
                          float E, float N)
        {
            // from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
            float n = (float)( 1 << OSM_RENDER_ZOOM);

            // convert E,N to radians. The interpolation coefficients assume this
            E *= (float)M_PI/180.0f;
            N *= (float)M_PI/180.0f;

            float lon0 = n / 2.0f;
            float lon1 = n / ((float)M_PI * 2.0f);
            *x = (int)( fminf( n, fmaxf( 0.0f, E*lon1 + lon0 )));
            *y = (int)( n/2.0f * (1.0f -
                                  logf( (sinf(N) + 1.0f)/cosf(N) ) /
                                  (float)M_PI) );
        }

        void initOSMtexture(const texture_ctx_t* texture_ctx)
        {
            glActiveTextureARB( GL_TEXTURE0_ARB ); assert_opengl();
            glBindTexture( GL_TEXTURE_2D, texID ); assert_opengl();

            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

            // Init the whole texture with 0. Then later I'll fill it in tile by
            // tile
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                         texture_ctx->NtilesXY[0]*OSM_TILE_WIDTH,
                         texture_ctx->NtilesXY[1]*OSM_TILE_HEIGHT,
                         0, GL_RGB,
                         GL_UNSIGNED_BYTE, (const GLvoid *)NULL);

            assert_opengl();
        }

        void setOSMtextureTile( int osmTileX, int osmTileY,
                                const texture_ctx_t* texture_ctx)
        {
            char filename[256];
            char directory[256];
            int len;

            if(dir_tiles[0] == '~' && dir_tiles[1] == '/' )
            {
                const char* home = getenv("HOME");
                if(home == NULL)
                {
                    MSG("User asked for ~, but the 'HOME' env var isn't defined");
                    assert(0);
                }

                len = snprintf(filename, sizeof(filename),
                               "%s/%s/%d/%d/%d.png",
                               home,
                               &dir_tiles[2], OSM_RENDER_ZOOM, osmTileX, osmTileY);
            }
            else
                len = snprintf(filename, sizeof(filename),
                               "%s/%d/%d/%d.png",
                               dir_tiles, OSM_RENDER_ZOOM, osmTileX, osmTileY);

            assert(len < (int)sizeof(filename));


            if( access( filename, R_OK ) != 0 )
            {
                if(!allow_downloads)
                {
                    MSG("Tile '%s' doesn't exist on disk, and downloads aren't allowed. Giving up", filename);
                    assert(0);
                }

                // tile doesn't exist. Make a directory for it and try to download
                len = snprintf(directory, sizeof(directory),
                               "/home/dima/.horizonator/tiles/%d/%d",
                               OSM_RENDER_ZOOM, osmTileX);
                assert(len < (int)sizeof(directory));

                char url[256];
                len = snprintf(url, sizeof(url),
                               "https://a.tile.openstreetmap.org/%d/%d/%d.png",
                               OSM_RENDER_ZOOM, osmTileX, osmTileY);
                assert(len < (int)sizeof(url));

                char cmd[1024];
                len = snprintf( cmd, sizeof(cmd),
                                "mkdir -p %s && wget --user-agent=horizonator -O %s %s", directory, filename, url  );
                assert(len < (int)sizeof(cmd));
                int res = system(cmd);
                assert( res == 0 );
            }

            FREE_IMAGE_FORMAT format = FreeImage_GetFileType(filename,0);
            if(format == FIF_UNKNOWN)
            {
                MSG("Couldn't load '%s'", filename);
                assert(0);
            }

            FIBITMAP* fib = FreeImage_Load(format,
                                           filename,
                                           0);
            if(fib == NULL)
            {
                MSG("Couldn't load '%s'", filename);
                assert(0);
            }

            if(FreeImage_GetColorType(fib) == FIC_PALETTE)
            {
                // OSM tiles are palettized, and I must explicitly handle that in
                // FreeImage
                FIBITMAP* fib24 = FreeImage_ConvertTo24Bits(fib);
                FreeImage_Unload(fib);
                fib = fib24;

                if(fib == NULL)
                {
                    MSG("Couldn't unpalettize '%s'", filename);
                    assert(0);
                }
            }

            assert( FreeImage_GetWidth(fib)  == OSM_TILE_WIDTH );
            assert( FreeImage_GetHeight(fib) == OSM_TILE_HEIGHT );
            assert( FreeImage_GetBPP(fib)    == 8*3 );
            assert( FreeImage_GetPitch(fib)  == OSM_TILE_WIDTH*3 );

            // GL stores its textures upside down, so I flipt the y index of the
            // tile
            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            (osmTileX - texture_ctx->osmtile_lowestXY[0] )*OSM_TILE_WIDTH,
                            (texture_ctx->osmtile_highestXY[1] - osmTileY)*OSM_TILE_HEIGHT,
                            OSM_TILE_WIDTH, OSM_TILE_HEIGHT,
                            GL_BGR, GL_UNSIGNED_BYTE,
                            (const GLvoid *)FreeImage_GetBits(fib));
            assert_opengl();

            FreeImage_Unload(fib);
        }

        // My render data is in a grid centered on viewer_lat/viewer_lon, branching
        // RENDER_RADIUS*DEG_PER_CELL degrees in all 4 directions
        float lowest_E  = viewer_lon - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float lowest_N  = viewer_lat - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float highest_E = viewer_lon + (float)RENDER_RADIUS/CELLS_PER_DEG;
        float highest_N = viewer_lat + (float)RENDER_RADIUS/CELLS_PER_DEG;

        // ytile decreases with lat, so I treat it backwards
        getOSMTileID( &texture_ctx.osmtile_lowestXY[0],
                      &texture_ctx.osmtile_lowestXY[1],
                      lowest_E, highest_N);
        getOSMTileID( &texture_ctx.osmtile_highestXY[0],
                      &texture_ctx.osmtile_highestXY[1],
                      highest_E, lowest_N);

        texture_ctx.NtilesXY[0] = texture_ctx.osmtile_highestXY[0] - texture_ctx.osmtile_lowestXY[0] + 1;
        texture_ctx.NtilesXY[1] = texture_ctx.osmtile_highestXY[1] - texture_ctx.osmtile_lowestXY[1] + 1;

        initOSMtexture(&texture_ctx);

        for( int osmTileY = texture_ctx.osmtile_lowestXY[1];
             osmTileY <= texture_ctx.osmtile_highestXY[1];
             osmTileY++)
            for( int osmTileX = texture_ctx.osmtile_lowestXY[0];
                 osmTileX <= texture_ctx.osmtile_highestXY[0];
                 osmTileX++ )
                setOSMtextureTile( osmTileX, osmTileY, &texture_ctx );
    }

    // vertices
    //
    // I fill in the VBO. Each point is a 16-bit integer tuple
    // (ilon,ilat,height). The first 2 args are indices into the virtual DEM
    // (accessed with horizonator_dem_sample). The height is in meters
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
                int32_t z = horizonator_dem_sample(&ctx->dems, i,j);

                // Several paths are available. These require corresponding
                // updates in the GLSL, and exist for testing
#if 0
                // The CPU does all the math for the data procesing.
#if defined VBO_USES_INTEGERS && VBO_USES_INTEGERS
#error "This path requires floating-point vertices"
#endif
                const float Rearth = 6371000.0;
                const float cos_viewer_lat = cosf( M_PI / 180.0f * viewer_lat );
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
                const float cos_viewer_lat = cosf( M_PI / 180.0f * viewer_lat );
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
#include "vertex.glsl.h"
            ;

        const GLchar* geometryShaderSource =
#include "geometry.glsl.h"
            ;

        const GLchar* fragmentShaderSource =
#include "fragment.glsl.h"
            ;

        char msg[1024];
        int len;
        ctx->program = glCreateProgram();
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
        glAttachShader(ctx->program, type ##Shader);                         \
        assert_opengl();



        install_shader(vertex,   VERTEX);
        install_shader(fragment, FRAGMENT);
        install_shader(geometry, GEOMETRY);

        glLinkProgram(ctx->program); assert_opengl();
        glGetProgramInfoLog( ctx->program, sizeof(msg), &len, msg );
        if( strlen(msg) )
            printf("program info after glLinkProgram(): %s\n", msg);

        glUseProgram(ctx->program);  assert_opengl();
        glGetProgramInfoLog( ctx->program, sizeof(msg), &len, msg );
        if( strlen(msg) )
            printf("program info after glUseProgram: %s\n", msg);


#define make_and_set_uniform(gltype, name, expr) do {                   \
            GLint uniform_ ## name = glGetUniformLocation(ctx->program, #name); \
            assert_opengl();                                            \
            glUniform1 ## gltype ( uniform_ ## name, expr);             \
            assert_opengl();                                            \
        } while(0)

        make_and_set_uniform(f, DEG_PER_CELL,   1.0f/ (float)CELLS_PER_DEG );

        make_and_set_uniform(f, origin_cell_lon_deg,
                     (float)ctx->dems.origin_dem_lon_lat[0] +
                     (float)ctx->dems.origin_dem_cellij[0] / (float)CELLS_PER_DEG);
        make_and_set_uniform(f, origin_cell_lat_deg,
                     (float)ctx->dems.origin_dem_lon_lat[1] +
                     (float)ctx->dems.origin_dem_cellij[1] / (float)CELLS_PER_DEG);
        make_and_set_uniform(i, NtilesX,         texture_ctx.NtilesXY[0]);
        make_and_set_uniform(i, NtilesY,         texture_ctx.NtilesXY[1]);
        make_and_set_uniform(i, osmtile_lowestX, texture_ctx.osmtile_lowestXY[0]);
        make_and_set_uniform(i, osmtile_lowestY, texture_ctx.osmtile_lowestXY[1]);
        make_and_set_uniform(f, znear,           ZNEAR);
        make_and_set_uniform(f, zfar,            ZFAR);

        // These may be modified at runtime, so I make, but don't set
        ctx->uniform_aspect           = glGetUniformLocation(ctx->program, "aspect");           assert_opengl();
        ctx->uniform_az_deg0          = glGetUniformLocation(ctx->program, "az_deg0");          assert_opengl();
        ctx->uniform_az_deg1          = glGetUniformLocation(ctx->program, "az_deg1");          assert_opengl();
        ctx->uniform_viewer_cell_i    = glGetUniformLocation(ctx->program, "viewer_cell_i");    assert_opengl();
        ctx->uniform_viewer_cell_j    = glGetUniformLocation(ctx->program, "viewer_cell_j");    assert_opengl();
        ctx->uniform_viewer_z         = glGetUniformLocation(ctx->program, "viewer_z");         assert_opengl();
        ctx->uniform_viewer_lat       = glGetUniformLocation(ctx->program, "viewer_lat");       assert_opengl();
        ctx->uniform_cos_viewer_lat   = glGetUniformLocation(ctx->program, "cos_viewer_lat");   assert_opengl();
        ctx->uniform_texturemap_lon0  = glGetUniformLocation(ctx->program, "texturemap_lon0");  assert_opengl();
        ctx->uniform_texturemap_lon1  = glGetUniformLocation(ctx->program, "texturemap_lon1");  assert_opengl();
        ctx->uniform_texturemap_dlat0 = glGetUniformLocation(ctx->program, "texturemap_dlat0"); assert_opengl();
        ctx->uniform_texturemap_dlat1 = glGetUniformLocation(ctx->program, "texturemap_dlat1"); assert_opengl();
        ctx->uniform_texturemap_dlat2 = glGetUniformLocation(ctx->program, "texturemap_dlat2"); assert_opengl();
#undef make_and_set_uniform

        // And I set the other uniforms
        horizonator_move(ctx, viewer_lat, viewer_lon);
    }

    if(offscreen_width > 0)
    {
        static_assert(sizeof(GLuint) == sizeof(ctx->offscreen.frameBufID),
                      "horizonator_context_t.offscreen.... must be a GLuint");

        glGenFramebuffers(1, &ctx->offscreen.frameBufID);
        assert_opengl();
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->offscreen.frameBufID);
        assert_opengl();

        glGenRenderbuffers(1, &ctx->offscreen.renderBufID);
        assert_opengl();
        glBindRenderbuffer(GL_RENDERBUFFER, ctx->offscreen.renderBufID);
        assert_opengl();
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB,
                              offscreen_width, offscreen_height);
        assert_opengl();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, ctx->offscreen.renderBufID);
        assert_opengl();

        glGenRenderbuffers(1, &ctx->offscreen.depthBufID);
        assert_opengl();
        glBindRenderbuffer(GL_RENDERBUFFER, ctx->offscreen.depthBufID);
        assert_opengl();
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                              offscreen_width, offscreen_height);
        assert_opengl();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, ctx->offscreen.depthBufID);
        assert_opengl();

        glViewport(0, 0, offscreen_width, offscreen_height);
        glUniform1f(ctx->uniform_aspect,
                    (float)offscreen_width / (float)offscreen_height);

        ctx->offscreen.inited = true;
        ctx->offscreen.width  = offscreen_width;
        ctx->offscreen.height = offscreen_height;

        atexit(glutExit);
    }


    // arbitrary az bounds initially
    if(!horizonator_pan_zoom(ctx, -45.f, 45.f))
        goto done;

    result = true;

 done:
    if(dem_context_inited && !result)
        horizonator_dem_deinit(&ctx->dems);

    return result;
}

void horizonator_move(horizonator_context_t* ctx,
                      float viewer_lat, float viewer_lon)
{
    void texture_coeffs(// output
                        float* lon0,
                        float* lon1,
                        float* dlat0,
                        float* dlat1,
                        float* dlat2,

                        // input
                        float lat_center)
    {
        // The spherical equirectangular latlon-to-projection equations
        // (from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames):
        //
        //    xtile(lon) = n * (lon + pi)/(2pi)
        //    ytile(lat) = n/2 * (1 - log( (sin(lat) + 1)/cos(lat) ) / pi)
        //
        // The lon expression is linear, so I compute the two exact
        // coefficients. The lat expression is not linear. I compute the
        // 2nd-order taylor-series approximation (around the viewer
        // position), and store those coefficients. Let lat,lon be in
        // radians.
        //
        // xtile increases with lon
        // ytile decreases with lat

        float n = (float)(1 << OSM_RENDER_ZOOM);

        *lon0 = n / 2.0f;
        *lon1 = n / ((float)M_PI * 2.0f);

        // The derivatives are
        //
        //    ytile'(lat)  = -n/(2*pi*cos(lat))
        //    ytile''(lat) = -n/(2*pi)*tan(lat)/cos(lat)
        //
        // Let
        //    k = -n/(2*pi), c = cos(lat_center), t = tan(lat_center), dlat = lat-lat_center
        //
        //    ytile(lat_center)  = n/2 + k*log( t + 1/c )
        //    ytile'(lat_center) = k / c
        //    ytile''(lat_center)= k * t / c
        //
        // Thus
        //
        //    ytile(lat) ~ ytile(lat_center) + ytile'(lat_center)*dlat + 1/2*ytile''(lat_center)*dlat^2
        lat_center *= (float)M_PI / 180.0f;
        float k = -n / ((float)M_PI * 2.0f);
        float t = tanf( lat_center );
        float c = cosf( lat_center );
        *dlat0 = n/2.0f + k*logf( t + 1.0f/c );
        *dlat1 = k / c;
        *dlat2 = k * t / c / 2.0f;
    }

    float lon0,lon1,dlat0,dlat1,dlat2;
    texture_coeffs(&lon0,&lon1,&dlat0,&dlat1,&dlat2,
                   viewer_lat);

    float viewer_cell_i =
        (viewer_lon - ctx->dems.origin_dem_lon_lat[0]) * CELLS_PER_DEG -
        ctx->dems.origin_dem_cellij[0];
    float viewer_cell_j =
        (viewer_lat - ctx->dems.origin_dem_lon_lat[1]) * CELLS_PER_DEG -
        ctx->dems.origin_dem_cellij[1];


    // The viewer elevation. I nudge it up a tiny bit to not see fewer bumps
    // immediately around me
    int i0 = (int)floorf(viewer_cell_i);
    int j0 = (int)floorf(viewer_cell_j);
    float viewer_z =
        fmaxf( fmaxf(horizonator_dem_sample( &ctx->dems, i0,   j0),
                     horizonator_dem_sample( &ctx->dems, i0+1, j0)),
               fmaxf(horizonator_dem_sample( &ctx->dems, i0,   j0+1 ),
                     horizonator_dem_sample( &ctx->dems, i0+1, j0+1 )) ) + 1.0;

    glUniform1f(ctx->uniform_viewer_cell_i,    viewer_cell_i);
    assert_opengl();
    glUniform1f(ctx->uniform_viewer_cell_j,    viewer_cell_j);
    assert_opengl();
    glUniform1f(ctx->uniform_viewer_z,         viewer_z);
    assert_opengl();
    glUniform1f(ctx->uniform_viewer_lat,             viewer_lat * M_PI / 180.0f );
    assert_opengl();
    glUniform1f(ctx->uniform_cos_viewer_lat,   cosf( viewer_lat * M_PI / 180.0f ));
    assert_opengl();
    glUniform1f(ctx->uniform_texturemap_lon0,  lon0);
    assert_opengl();
    glUniform1f(ctx->uniform_texturemap_lon1,  lon1);
    assert_opengl();
    glUniform1f(ctx->uniform_texturemap_dlat0, dlat0);
    assert_opengl();
    glUniform1f(ctx->uniform_texturemap_dlat1, dlat1);
    assert_opengl();
    glUniform1f(ctx->uniform_texturemap_dlat2, dlat2);
    assert_opengl();

    ctx->viewer_lat = viewer_lat;
    ctx->viewer_lon = viewer_lon;
}

bool horizonator_pan_zoom(const horizonator_context_t* ctx,
                      // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                      // edges lie at the edges of the image. So for an image that's
                      // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                      // elevation extents will be chosen to keep the aspect ratio
                      // square.
                      float az_deg0, float az_deg1)
{
    glUniform1f( ctx->uniform_az_deg0, az_deg0); assert_opengl();
    glUniform1f( ctx->uniform_az_deg1, az_deg1); assert_opengl();
    return true;
}

void horizonator_resized(const horizonator_context_t* ctx, int width, int height)
{
    if( ctx->offscreen.inited )
    {
        MSG("Resising an offscreen window is not yet supported");
        assert(0);
    }

    glViewport(0, 0, width, height);
    glUniform1f(ctx->uniform_aspect, (float)width / (float)height);
}

void horizonator_redraw(const horizonator_context_t* ctx)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, ctx->Ntriangles*3, GL_UNSIGNED_INT, NULL);
}

// Renders a given scene to an RGB image and/or a range image.
// horizonator_init() must have been called first with use_glut=true and
// offscreen_width,height > 0. Then the viewer and camera must have been
// configured with horizonator_move() and horizonator_pan_zoom()
//
// Renders a given scene to an RGB image and/or a range image. Returns true on
// success. The image and ranges buffers must be large-enough to contain packed
// 24-bits-per-pixel BGR data and 32-bit floats respectively. The images are
// returned using the OpenGL convention: bottom row is stored first. This is
// opposite of the usual image convention: top row is first. Invisible points
// have ranges <0
bool horizonator_render_offscreen(// output
                                  // either may be NULL
                                  char* image, float* ranges,

                                  const horizonator_context_t* ctx)
{
    if(!ctx->offscreen.inited)
    {
        MSG("Prior to calling horizonator_render_offscreen(), the context must have been inited for offscreen rendering with horizonator_init(use_glut=true, offscreen_width,height > 0)");
        return false;
    }

    int width  = ctx->offscreen.width;
    int height = ctx->offscreen.height;

    horizonator_redraw(ctx);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if(image != NULL)
        glReadPixels(0,0, width, height,
                     GL_BGR, GL_UNSIGNED_BYTE, image);
    if(ranges != NULL)
    {
        glReadPixels(0,0, width, height,
                     GL_DEPTH_COMPONENT, GL_FLOAT, ranges);

        float az_deg0, az_deg1;
        glGetUniformfv(ctx->program, ctx->uniform_az_deg0,        &az_deg0);
        assert_opengl();
        glGetUniformfv(ctx->program, ctx->uniform_az_deg1,        &az_deg1);
        assert_opengl();

        // I just read the depth buffer. depth is in [0,1] and it describes
        // gl_Position.z/gl_Position.w in the vertex shader, except THAT
        // quantity is in [-1,1]. I convert each "depth" value to a "range"

        // In vertex.glsl we have:
        //
        // az = 0:     North
        // az = 90deg: East
        // xy coords are (e,n)
        /*
          en = { (lon - lon0) * Rearth * pi/180. * cos_viewer_lat,
                 (lat - lat0) * Rearth * pi/180. };

          az = atan(en.x, en.y);

          az_center = (az0 + az1)/2.;
          az_ndc    = (az - az_center) * 2 / (az1 - az0);

          aspect = width / height
          el_ndc = atan(z, length(en)) * aspect * 2 / (az1 - az0);

          depth = ((length(en) - znear) / (zfar - znear))
        */

        // The viewport is "width" pixels wide. The center of the first pixel is
        // at x=0.5. The center of the last pixel is at x=width-0.5
        for(int y=0; y<height; y++)
        {
            for(int x=0; x<width; x++)
            {
                float depth = ranges[y*width + x];
                if(depth == 1.0f)
                    ranges[y*width + x] = -1.0f;
                else
                {
                    float length_en = depth * (ZFAR-ZNEAR) + ZNEAR;

                    // float az_ndc = ((float)x + 0.5f) / (float)width * 2.f - 1.f;
                    // float az     = (az_ndc * (az_deg1-az_deg0) / 2.f + (az_deg1+az_deg0)/2.f) * M_PI/180.0f;

                    float el_ndc = ((float)y + 0.5f) / (float)height * 2.f - 1.f;
                    float aspect = (float)width / (float)height;
                    float el     = el_ndc * (az_deg1-az_deg0) / 2.f / aspect * M_PI/180.0f;

                    float z = tanf(el) * length_en;
                    ranges[y*width + x] = hypotf(length_en, z);
                }
            }
        }
    }

    return true;
}

bool horizonator_allinone_glut_loop( bool render_texture,
                            float viewer_lat, float viewer_lon,

                            // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                            // edges lie at the edges of the image. So for an image that's
                            // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                            // elevation extents will be chosen to keep the aspect ratio
                            // square.
                            float az_deg0, float az_deg1,
                            const char* dir_dems,
                            const char* dir_tiles,
                            bool allow_downloads)
{
    horizonator_context_t ctx;

    if( !horizonator_init( &ctx,
                           true,
                           -1, -1,
                           render_texture,
                           viewer_lat, viewer_lon,
                           dir_dems,
                           dir_tiles,
                           allow_downloads) )
        return false;

    if(!horizonator_pan_zoom( &ctx, az_deg0, az_deg1))
        return false;

    void window_display(void)
    {
        horizonator_redraw(&ctx);
        glutSwapBuffers();
    }

    GLenum winding = GL_CCW;
    const GLenum polygon_modes[] = {GL_FILL, GL_LINE, GL_POINT};
    int polygon_mode_idx = 0;
    void window_keyPressed(unsigned char key,
                           int x __attribute__((unused)) ,
                           int y __attribute__((unused)) )
    {
        switch (key)
        {
        case 'w':
            ;
            if(++polygon_mode_idx == sizeof(polygon_modes)/sizeof(polygon_modes[0]))
                polygon_mode_idx = 0;

            glPolygonMode(GL_FRONT_AND_BACK, polygon_modes[ polygon_mode_idx ] );
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

    void _horizonator_resized(int width, int height)
    {
        horizonator_resized(&ctx, width, height);
    }

    glutDisplayFunc (window_display);
    glutKeyboardFunc(window_keyPressed);
    glutReshapeFunc (_horizonator_resized);

    int res = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert( res == GL_FRAMEBUFFER_COMPLETE );

    glutMainLoop();

    return true;
}

// returns true if an intersection is found
bool horizonator_pick(const horizonator_context_t* ctx,

                      // output
                      float* lat, float* lon,

                      // input
                      // pixel coordinates in the render
                      int x, int y )
{
    // az = 0:     North
    // az = 90deg: East
    // xy coords are (e,n)
    // My projection function that I need to reverse:
    /*
      en = { (lon - lon0) * Rearth * pi/180. * cos_viewer_lat,
             (lat - lat0) * Rearth * pi/180. };

      az = atan(en.x, en.y);

      az_center = (az0 + az1)/2.;
      az_ndc    = (az - az_center) * 2 / (az1 - az0);

      aspect = width / height
      el_ndc = atan(z, length(en)) * aspect * 2 / (az1 - az0);

      depth = ((length(en) - znear) / (zfar - znear))
    */

    union
    {
        GLint viewport[4];
        struct
        {
            GLint x0,y0,width,height;
        };
    } u;
    glGetIntegerv(GL_VIEWPORT, u.viewport);

    float depth;
    glReadPixels(x, u.height-1 - y,
                 1,1,
                 GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    if(depth >= 1.0f)
        return false;

    // depth is in [0,1] and it describes gl_Position.z/gl_Position.w in the
    // vertex shader, except THAT quantity is in [-1,1]
    float length_en = depth * (ZFAR-ZNEAR) + ZNEAR;

    float az_deg0, az_deg1, cos_viewer_lat;
    glGetUniformfv(ctx->program, ctx->uniform_az_deg0,        &az_deg0);
    assert_opengl();
    glGetUniformfv(ctx->program, ctx->uniform_az_deg1,        &az_deg1);
    assert_opengl();
    glGetUniformfv(ctx->program, ctx->uniform_cos_viewer_lat, &cos_viewer_lat);
    assert_opengl();

    const float Rearth = 6371000.0;

    // The viewport is "width" pixels wide. The center of the first pixel is at
    // x=0.5. The center of the last pixel is at x=width-0.5
    float az_ndc = ((float)x + 0.5f) / (float)u.width * 2.f - 1.f;
    float az     = (az_ndc * (az_deg1-az_deg0) / 2.f + (az_deg1+az_deg0)/2.f) * M_PI/180.0f;

    // Could be useful, but I don't need these
    // float el_ndc = ((float)y + 0.5f) / (float)u.height * 2.f - 1.f;
    // float aspect = (float)u.width / (float)u.height;
    // float el     = el_ndc * (az_deg1-az_deg0) / 2.f / aspect * M_PI/180.0f;

    // I have some p = (e,n,z)
    //   e = length_en sin(az)
    //   n = length_en cos(az)
    //   z = length_en tan(el)
    float e = length_en * sinf(az);
    float n = length_en * cosf(az);

    *lon = ctx->viewer_lon + e / Rearth / M_PI * 180. / cos_viewer_lat;
    *lat = ctx->viewer_lat + n / Rearth / M_PI * 180.;
    return true;
}
