#define NPY_NO_DEPRECATED_API NPY_API_VERSION

#include <stdbool.h>
#include <Python.h>
#include <structmember.h>
#include <numpy/arrayobject.h>
#include <signal.h>

#include "horizonator.h"
#include "util.h"


#define BARF(fmt, ...) PyErr_Format(PyExc_RuntimeError, "%s:%d %s(): "fmt, __FILE__, __LINE__, __func__, ## __VA_ARGS__)

// Python is silly. There's some nuance about signal handling where it sets a
// SIGINT (ctrl-c) handler to just set a flag, and the python layer then reads
// this flag and does the thing. Here I'm running C code, so SIGINT would set a
// flag, but not quit, so I can't interrupt the solver. Thus I reset the SIGINT
// handler to the default, and put it back to the python-specific version when
// I'm done
#define SET_SIGINT() struct sigaction sigaction_old;                    \
do {                                                                    \
    if( 0 != sigaction(SIGINT,                                          \
                       &(struct sigaction){ .sa_handler = SIG_DFL },    \
                       &sigaction_old) )                                \
    {                                                                   \
        PyErr_SetString(PyExc_RuntimeError, "sigaction() failed");      \
        goto done;                                                      \
    }                                                                   \
} while(0)
#define RESET_SIGINT() do {                                             \
    if( 0 != sigaction(SIGINT,                                          \
                       &sigaction_old, NULL ))                          \
        PyErr_SetString(PyExc_RuntimeError, "sigaction-restore failed"); \
} while(0)

#define PYMETHODDEF_ENTRY(function_prefix, name, args) {#name,          \
                                                        (PyCFunction)function_prefix ## name, \
                                                        args,           \
                                                        function_prefix ## name ## _docstring}



typedef struct {
    PyObject_HEAD
    horizonator_context_t ctx;
} py_horizonator_t;

static int
py_horizonator_init(py_horizonator_t* self, PyObject* args, PyObject* kwargs)
{
    // error by default
    int result = -1;

    double lat, lon;
    unsigned int width, height;
    int render_texture    = false;
    int allow_downloads   = true;
    const char* dir_dems  = NULL;
    const char* dir_tiles = NULL;

    char* keywords[] = {
        "lat", "lon",
        "width", "height",
        "render_texture",
        "dir_dems", "dir_tiles", "allow_downloads",
        NULL};


    static bool inited;
    if(inited)
    {
        BARF("I already inited one horizonator object in this process. I don't have code to deinit or to manage multiple objects yet. Giving up");
        goto done;
    }
    if(self->ctx.offscreen.inited)
    {
        BARF("Trying to init an already-inited object");
        goto done;
    }
    inited = true;


    if( !PyArg_ParseTupleAndKeywords(args, kwargs,
                                     "ddII|pssp", keywords,
                                     &lat, &lon, &width, &height,
                                     &render_texture, &dir_dems, &dir_tiles,
                                     &allow_downloads))
        goto done;

    if(! horizonator_init( &self->ctx,
                           lat, lon, width, height,
                           true, render_texture, dir_dems, dir_tiles,
                           allow_downloads ) )
        goto done;

    result = 0;

 done:

    return result;
}

static void py_horizonator_dealloc(py_horizonator_t* self)
{
    MSG("release not yet implemented");
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* py_horizonator_str(py_horizonator_t* self)
{
    return PyUnicode_FromFormat("Looking out from %f,%f",
                               self->ctx.viewer_lat, self->ctx.viewer_lon);
}

static PyObject*
render(py_horizonator_t* self, PyObject* args, PyObject* kwargs)
{
    // error by default
    PyObject* result = NULL;
    PyObject* image  = NULL;
    PyObject* ranges = NULL;

    double lat = -1000., lon = -1000.;
    double az_deg0, az_deg1;
    int return_image = true, return_ranges = true;
    int az_extents_use_pixel_centers = false;

    char* keywords[] = {
        "az_deg0", "az_deg1",
        "lat", "lon",
        "return_image", "return_ranges",
        "az_extents_use_pixel_centers",
        NULL};

    if( !PyArg_ParseTupleAndKeywords(args, kwargs,
                                     "dd|ddppp", keywords,
                                     &az_deg0, &az_deg1,
                                     &lat, &lon,
                                     &return_image, &return_ranges,
                                     &az_extents_use_pixel_centers) )
        goto done;

    if(!return_image && !return_ranges)
    {
        result = PyTuple_New(0);
        goto done;
    }

    if(az_extents_use_pixel_centers)
    {
        // The user gave me az_deg referring to the center of the pixels at the
        // edge. I need to convert them to represent the edges of the viewport.
        // That's 0.5 pixels extra on either side
        double az_per_pixel = (az_deg1 - az_deg0) / (double)(ctx->offscreen.width-1);
        az_deg0 -= az_per_pixel/2.;
        az_deg1 += az_per_pixel/2.;
    }

    if( !horizonator_pan_zoom( &self->ctx, az_deg0, az_deg1 ) )
    {
        BARF("horizonator_pan_zoom() failed");
        goto done;
    }

    if(lat > -1000.)
        if( !horizonator_move( &self->ctx, lat, lon ) )
        {
            BARF("horizonator_move() failed");
            goto done;
        }

    if(return_image)
    {
        image =
            PyArray_SimpleNew(3, ((npy_intp[]){self->ctx.offscreen.height,
                                               self->ctx.offscreen.width,
                                               3}),
                NPY_UINT8);
        if(image == NULL) goto done;
    }
    if(return_ranges)
    {
        ranges =
            PyArray_SimpleNew(2, ((npy_intp[]){self->ctx.offscreen.height,
                                               self->ctx.offscreen.width}),
                NPY_FLOAT32);
        if(ranges == NULL) goto done;
    }

    if( !horizonator_render_offscreen( &self->ctx,
                                       image  == NULL ? NULL :
                                         (char *)PyArray_DATA((PyArrayObject*)image),
                                       ranges == NULL ? NULL :
                                         (float*)PyArray_DATA((PyArrayObject*)ranges) ))
    {
        BARF("horizonator_render_offscreen() failed");
        goto done;
    }

    if(      return_image && !return_ranges) result = image;
    else if(!return_image &&  return_ranges) result = ranges;
    else
    {
        result = PyTuple_Pack(2, image, ranges);
        if(result == NULL) goto done;
        Py_DECREF(image);
        Py_DECREF(ranges);
    }

 done:
    if(result == NULL)
    {
        Py_XDECREF(image);
        Py_XDECREF(ranges);
    }
    return result;
}

static const char py_horizonator_docstring[] =
#include "horizonator.docstring.h"
    ;
static const char render_docstring[] =
#include "render.docstring.h"
    ;

static PyMethodDef py_horizonator_methods[] =
    {
        PYMETHODDEF_ENTRY(, render, METH_VARARGS | METH_KEYWORDS),
        {}
    };


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
// PyObject_HEAD_INIT throws
//   warning: missing braces around initializer []
// This isn't mine to fix, so I'm ignoring it
static PyTypeObject horizonator_type =
{
     PyObject_HEAD_INIT(NULL)
    .tp_name      = "horizonator",
    .tp_basicsize = sizeof(py_horizonator_t),
    .tp_new       = PyType_GenericNew,
    .tp_init      = (initproc)py_horizonator_init,
    .tp_dealloc   = (destructor)py_horizonator_dealloc,
    .tp_methods   = py_horizonator_methods,
    .tp_str       = (reprfunc)py_horizonator_str,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = py_horizonator_docstring,
};
#pragma GCC diagnostic pop


static struct PyModuleDef module_def =
    {
     PyModuleDef_HEAD_INIT,
     "horizonator",
     "SRTM terrain renderer",
     -1,
     NULL
    };

PyMODINIT_FUNC PyInit_horizonator(void)
{
    if (PyType_Ready(&horizonator_type) < 0)
        return NULL;

    PyObject* module = PyModule_Create(&module_def);
    Py_INCREF(&horizonator_type);
    PyModule_AddObject(module, "horizonator", (PyObject *)&horizonator_type);

    import_array();
    return module;
}
