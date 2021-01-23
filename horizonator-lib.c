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
#define RENDER_RADIUS       1000

// for texture rendering
#define OSM_RENDER_ZOOM     13
#define OSM_TILE_WIDTH      256
#define OSM_TILE_HEIGHT     256


#define assert_opengl()                                 \
    do {                                                \
        int error = glGetError();                       \
        if( error != GL_NO_ERROR )                      \
        {                                               \
            MSG("Error: %#x! Giving up", error);        \
            assert(0);                                  \
        }                                               \
    } while(0)


bool horizonator_init0_glut(bool double_buffered)
{

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
    MSG("glGetString(GL_VERSION) says we're using GL %s", version);
    MSG("Epoxy says we're using GL %d", epoxy_gl_version());

    if (version[0] == '1')
    {
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
    return true;
}

bool horizonator_init1( // output
                       horizonator_context_t* ctx,

                       // input
                       bool render_texture,

                       // Will be nudged a bit. The latlon we will use are
                       // returned in the context
                       float viewer_lat, float viewer_lon,

                       const char* dir_dems,
                       const char* dir_tiles,
                       bool allow_downloads)
{
    static_assert(sizeof(GLint) == sizeof(ctx->uniform_aspect),
                  "horizonator_context_t.uniform_aspect must be a GLint");

    bool          result             = false;
    bool          dem_context_inited = false;
    dem_context_t dem_context;
    float         viewer_cell[2];



    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0, 0, 1, 0);

    if( !dem_init( &dem_context,
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


    // The viewer elevation. I nudge it up a tiny bit to not see the stuff
    // immediately around me
    const float viewer_z = fmaxf( fmaxf(dem_sample( &dem_context, RENDER_RADIUS-1, RENDER_RADIUS-1),
                                        dem_sample( &dem_context, RENDER_RADIUS,   RENDER_RADIUS-1)),
                                  fmaxf(dem_sample( &dem_context, RENDER_RADIUS-1, RENDER_RADIUS  ),
                                        dem_sample( &dem_context, RENDER_RADIUS,   RENDER_RADIUS  )) ) + 1.0;

    // The spherical equirectangular latlon-to-projection equations (from
    // https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames):
    //
    //    xtile(lon) = n * (lon + pi)/(2pi)
    //    ytile(lat) = n/2 * (1 - log( (sin(lat) + 1)/cos(lat) ) / pi)
    //
    // The lon expression is linear, so I compute the two exact coefficients.
    // The lat expression is not linear. I compute the 2nd-order taylor-series
    // approximation (around the viewer position), and store those coefficients.
    // Let lat,lon be in radians.
    typedef struct
    {
        // latlon->tile coordinate conversion coefficients
        float lon0, lon1;
        float dlat0, dlat1, dlat2;

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

        void computeTextureMapInterpolationCoeffs(// output
                                                  texture_ctx_t* texture_ctx,

                                                  // input
                                                  float lat0)
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

            texture_ctx->lon0 = n / 2.0f;
            texture_ctx->lon1 = n / ((float)M_PI * 2.0f);

            // The derivatives are
            //
            //    ytile'(lat)  = -n/(2*pi*cos(lat))
            //    ytile''(lat) = -n/(2*pi)*tan(lat)/cos(lat)
            //
            // Let
            //    k = -n/(2*pi), c = cos(lat0), t = tan(lat0), dlat = lat-lat0
            //
            //    ytile(lat0)  = n/2 + k*log( t + 1/c )
            //    ytile'(lat0) = k / c
            //    ytile''(lat0)= k * t / c
            //
            // Thus
            //
            //    ytile(lat) ~ ytile(lat0) + ytile'(lat0)*dlat + 1/2*ytile''(lat0)*dlat^2
            lat0 *= (float)M_PI / 180.0f;
            float k = -n / ((float)M_PI * 2.0f);
            float t = tan( lat0 );
            float c = cos( lat0 );
            texture_ctx->dlat0 = n/2.0f + k*logf( t + 1.0f/c );
            texture_ctx->dlat1 = k / c;
            texture_ctx->dlat2 = k * t / c / 2.0f;
        }

        void getOSMTileID( // output tile indices
                          int* x, int* y,

                          // input
                          // latlon, in degrees
                          float E, float N,
                          const texture_ctx_t* texture_ctx )
        {
            // from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
            float n = (float)( 1 << OSM_RENDER_ZOOM);

            // convert E,N to radians. The interpolation coefficients assume this
            E *= (float)M_PI/180.0f;
            N *= (float)M_PI/180.0f;

            *x = (int)( fminf( n, fmaxf( 0.0f, E*texture_ctx->lon1 + texture_ctx->lon0 )));
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



        computeTextureMapInterpolationCoeffs(&texture_ctx,
                                             dem_context.viewer_lon_lat[1]);

        // My render data is in a grid centered on dem_context.viewer_lon_lat[1]/dem_context.viewer_lon_lat[0], branching
        // RENDER_RADIUS*DEG_PER_CELL degrees in all 4 directions
        float lowest_E  = dem_context.viewer_lon_lat[0] - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float lowest_N  = dem_context.viewer_lon_lat[1] - (float)RENDER_RADIUS/CELLS_PER_DEG;
        float highest_E = dem_context.viewer_lon_lat[0] + (float)RENDER_RADIUS/CELLS_PER_DEG;
        float highest_N = dem_context.viewer_lon_lat[1] + (float)RENDER_RADIUS/CELLS_PER_DEG;

        // ytile decreases with lat, so I treat it backwards
        getOSMTileID( &texture_ctx.osmtile_lowestXY[0],
                      &texture_ctx.osmtile_lowestXY[1],
                      lowest_E, highest_N, &texture_ctx );
        getOSMTileID( &texture_ctx.osmtile_highestXY[0],
                      &texture_ctx.osmtile_highestXY[1],
                      highest_E, lowest_N, &texture_ctx );

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

        for(int i=0; i<2; i++)
            viewer_cell[i] =
                (dem_context.viewer_lon_lat[i] - dem_context.origin_dem_lon_lat[i]) * CELLS_PER_DEG - dem_context.origin_dem_cellij[i];

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

        glLinkProgram(program); assert_opengl();
        glGetProgramInfoLog( program, sizeof(msg), &len, msg );
        if( strlen(msg) )
            printf("program info after glLinkProgram(): %s\n", msg);

        glUseProgram(program);  assert_opengl();
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
        make_uniform(f, sin_viewer_lat, sin( M_PI / 180.0f * dem_context.viewer_lon_lat[1] ));
        make_uniform(f, cos_viewer_lat, cos( M_PI / 180.0f * dem_context.viewer_lon_lat[1] ));

        // These may be modified at runtime, so I do it manually, without make_uniform()
        ctx->uniform_aspect  = glGetUniformLocation(program, "aspect");  assert_opengl();
        ctx->uniform_az_deg0 = glGetUniformLocation(program, "az_deg0"); assert_opengl();
        ctx->uniform_az_deg1 = glGetUniformLocation(program, "az_deg1"); assert_opengl();

        // For texturing. If we're not texturing, NtilesXY[0] will be 0
        make_uniform(f, viewer_lat,     dem_context.viewer_lon_lat[1] * M_PI / 180.0f );
        make_uniform(f, origin_cell_lon_deg,
                     (float)dem_context.origin_dem_lon_lat[0] +
                     (float)dem_context.origin_dem_cellij[0] / (float)CELLS_PER_DEG);
        make_uniform(f, origin_cell_lat_deg,
                     (float)dem_context.origin_dem_lon_lat[1] +
                     (float)dem_context.origin_dem_cellij[1] / (float)CELLS_PER_DEG);
        make_uniform(f, texturemap_lon0, texture_ctx.lon0);
        make_uniform(f, texturemap_lon1, texture_ctx.lon1);
        make_uniform(f, texturemap_dlat0,texture_ctx.dlat0);
        make_uniform(f, texturemap_dlat1,texture_ctx.dlat1);
        make_uniform(f, texturemap_dlat2,texture_ctx.dlat2);
        make_uniform(i, NtilesX,         texture_ctx.NtilesXY[0]);
        make_uniform(i, NtilesY,         texture_ctx.NtilesXY[1]);
        make_uniform(i, osmtile_lowestX, texture_ctx.osmtile_lowestXY[0]);
        make_uniform(i, osmtile_lowestY, texture_ctx.osmtile_lowestXY[1]);

#undef make_uniform
    }

    result = true;

 done:
    if(dem_context_inited)
        dem_deinit(&dem_context);

    return result;
}

bool horizonator_zoom(const horizonator_context_t* ctx,
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

static bool init( // output
                 horizonator_context_t* ctx,

                 // input
                 bool render_offscreen,
                 bool render_texture,

                 // Will be nudged a bit. The latlon we will use are
                 // returned in the context
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
    if(!horizonator_init0_glut( !render_offscreen ))
        return false;

    if(!horizonator_init1(ctx,
                          render_texture,
                          viewer_lat,       viewer_lon,
                          dir_dems,         dir_tiles,
                          allow_downloads))
        return false;

    if(!horizonator_zoom(ctx, az_deg0, az_deg1))
        return false;

    return true;
}

void horizonator_resized(const horizonator_context_t* ctx, int width, int height)
{
    glViewport(0, 0, width, height);
    glUniform1f(ctx->uniform_aspect, (float)width / (float)height);
}

void horizonator_redraw(const horizonator_context_t* ctx)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, ctx->Ntriangles*3, GL_UNSIGNED_INT, NULL);
}

// returns the rendered image buffer. NULL on error. It is the caller's
// responsibility to free() this buffer. The image data is packed
// 24-bits-per-pixel BGR data stored row-first.
char* horizonator_allinone_render_to_image(bool render_texture,
                                           float viewer_lat, float viewer_lon,

                                           // Bounds of the view. We expect az_deg1 > az_deg0. The azimuth
                                           // edges lie at the edges of the image. So for an image that's
                                           // W pixels wide, az0 is at x = -0.5 and az1 is at W-0.5. The
                                           // elevation extents will be chosen to keep the aspect ratio
                                           // square.
                                           float az_deg0, float az_deg1,

                                           int width, int height,
                                           const char* dir_dems,
                                           const char* dir_tiles,
                                           bool allow_downloads)
{
    char* result = NULL;
    char* img    = NULL;

    horizonator_context_t ctx;

    if( !init( &ctx,
               true,
               render_texture,
               viewer_lat, viewer_lon,
               az_deg0, az_deg1,
               dir_dems,
               dir_tiles,
               allow_downloads) )
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

    horizonator_resized(&ctx, width, height);
    horizonator_redraw(&ctx);

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

    if( !init( &ctx,
               false,
               render_texture,
               viewer_lat, viewer_lon,
               az_deg0, az_deg1,
               dir_dems,
               dir_tiles,
               allow_downloads) )
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
