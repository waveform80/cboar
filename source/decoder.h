#define PY_SSIZE_T_CLEAN
#include <stdbool.h>
#include <Python.h>

typedef struct {
    PyObject_HEAD
    PyObject *read;    // cached read() method of fp
    PyObject *tag_hook;
    PyObject *object_hook;
    PyObject *shareables;
    bool immutable;
} DecoderObject;

PyTypeObject DecoderType;
