#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "module.h"
#include "encoder.h"
#include "decoder.h"


// Some notes on conventions in this code. All methods conform to a couple of
// return styles:
//
// * PyObject* (mostly for methods accessible from Python) in which case a
//   return value of NULL indicates an error, or
//
// * int (mostly for internal methods) in which case 0 indicates success and -1
//   an error. This is in keeping with most of Python's C-API.
//
// In an attempt to avoid leaks a particular coding style is used where
// possible:
//
// 1. As soon as a new reference to an object is generated / returned, a
//    block like this follows: if (ref) { ... Py_DECREF(ref); }
//
// 2. The result is calculated in the "ret" local and returned only at the
//    end of the function, once we're sure all references have been accounted
//    for.
//
// 3. No "return" is permitted before the end of the function, and "break" or
//    "goto" should be used over a minimal distance to ensure Py_DECREFs aren't
//    jumped over.
//
// 4. The above rules are broken occasionally where necessary for clarity :)
//
// While this style helps ensure fewer leaks, it's worth noting it results in
// rather "nested" code which looks a bit unusual / ugly for C. Furthermore,
// it's not fool-proof; there's probably some leaks left. Please file bugs for
// any leaks you detect!


PyTypeObject CBORTagType;

PyObject *_CBOAR_empty_bytes = NULL;
PyObject *_CBOAR_empty_str = NULL;
PyObject *_CBOAR_str_as_string = NULL;
PyObject *_CBOAR_str_as_tuple = NULL;
PyObject *_CBOAR_str_bit_length = NULL;
PyObject *_CBOAR_str_bytes = NULL;
PyObject *_CBOAR_str_denominator = NULL;
PyObject *_CBOAR_str_is_infinite = NULL;
PyObject *_CBOAR_str_is_nan = NULL;
PyObject *_CBOAR_str_isoformat = NULL;
PyObject *_CBOAR_str_join = NULL;
PyObject *_CBOAR_str_numerator = NULL;
PyObject *_CBOAR_str_OrderedDict = NULL;
PyObject *_CBOAR_str_pattern = NULL;
PyObject *_CBOAR_str_read = NULL;
PyObject *_CBOAR_str_timestamp = NULL;
PyObject *_CBOAR_str_utc_suffix = NULL;
PyObject *_CBOAR_str_write = NULL;


static PyObject *
break_marker_repr(PyObject *op)
{
    return PyUnicode_FromString("break_marker");
}

static void
break_marker_dealloc(PyObject *ignore)
{
    Py_FatalError("deallocating break_marker");
}

static PyObject *
break_marker_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    if (PyTuple_GET_SIZE(args) || (kwargs && PyDict_Size(kwargs))) {
        PyErr_SetString(PyExc_TypeError, "break_marker_type takes no arguments");
        return NULL;
    }
    Py_INCREF(break_marker);
    return break_marker;
}

static int
break_marker_bool(PyObject *v)
{
    return 0;
}

static PyNumberMethods break_marker_as_number = {
    .nb_bool = (inquiry) break_marker_bool,
};

PyTypeObject break_marker_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "break_marker_type",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = break_marker_new,
    .tp_dealloc = break_marker_dealloc,
    .tp_repr = break_marker_repr,
    .tp_as_number = &break_marker_as_number,
};

PyObject _break_marker_obj = {
    _PyObject_EXTRA_INIT
    1, &break_marker_type
};


static PyStructSequence_Field CBORTagFields[] = {
    {.name = "tag"},
    {.name = "value"},
    {NULL},
};

static PyStructSequence_Desc CBORTagDesc = {
    .name = "CBORTag",
    .doc = NULL,  // TODO
    .fields = CBORTagFields,
    .n_in_sequence = 2,
};


static struct PyModuleDef _cboarmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_cboar",
    .m_doc = NULL,
    .m_size = -1, // XXX Change to 0?
};


PyMODINIT_FUNC
PyInit__cboar(void)
{
    PyObject *module;
    if (PyType_Ready(&EncoderType) < 0)
        return NULL;
    if (PyType_Ready(&DecoderType) < 0)
        return NULL;

    module = PyModule_Create(&_cboarmodule);
    if (!module)
        return NULL;

    // Use PyStructSequence_InitType2 to workaround #34784 (dup of #28709)
    if (PyStructSequence_InitType2(&CBORTagType, &CBORTagDesc) == -1)
        goto error;
    Py_INCREF((PyObject *) &CBORTagType);
    if (PyModule_AddObject(module, "CBORTag", (PyObject *) &CBORTagType) == -1)
        goto error;

    Py_INCREF(&EncoderType);
    if (PyModule_AddObject(module, "Encoder", (PyObject *) &EncoderType) == -1)
        goto error;

    Py_INCREF(&DecoderType);
    if (PyModule_AddObject(module, "Decoder", (PyObject *) &DecoderType) == -1)
        goto error;

#define INTERN_STRING(name) \
    if (!_CBOAR_str_##name && \
            !(_CBOAR_str_##name = PyUnicode_InternFromString(#name))) \
        goto error;

    INTERN_STRING(as_string);
    INTERN_STRING(as_tuple);
    INTERN_STRING(bit_length);
    INTERN_STRING(bytes);
    INTERN_STRING(denominator);
    INTERN_STRING(is_infinite);
    INTERN_STRING(is_nan);
    INTERN_STRING(isoformat);
    INTERN_STRING(join);
    INTERN_STRING(numerator);
    INTERN_STRING(OrderedDict);
    INTERN_STRING(pattern);
    INTERN_STRING(read);
    INTERN_STRING(timestamp);
    INTERN_STRING(write);

    if (!_CBOAR_str_utc_suffix &&
            !(_CBOAR_str_utc_suffix = PyUnicode_InternFromString("+00:00")))
        goto error;
    if (!_CBOAR_empty_bytes &&
            !(_CBOAR_empty_bytes = PyBytes_FromStringAndSize(NULL, 0)))
        goto error;
    if (!_CBOAR_empty_str &&
            !(_CBOAR_empty_str = PyUnicode_FromStringAndSize(NULL, 0)))
        goto error;

    return module;
error:
    Py_DECREF(module);
    return NULL;
}