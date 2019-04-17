#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>

typedef struct {
    PyObject_HEAD
    PyObject *read;    // cached read() method of fp
    PyObject *tag_hook;
    PyObject *object_hook;
    PyObject *shareables;
    bool immutable;
} DecoderObject;

PyTypeObject DecoderType;
