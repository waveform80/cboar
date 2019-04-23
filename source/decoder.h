#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    PyObject_HEAD
    PyObject *read;    // cached read() method of fp
    PyObject *tag_hook;
    PyObject *object_hook;
    PyObject *shareables;
    PyObject *str_errors;
    PyObject *timezone;
    PyObject *utc;
    PyObject *datestr_re;
    bool immutable;
    int32_t shared_index;
} DecoderObject;

PyTypeObject DecoderType;
