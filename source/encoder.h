#define PY_SSIZE_T_CLEAN
#include <stdbool.h>
#include <Python.h>

typedef struct {
    PyObject_HEAD
    PyObject *write;    // cached write() method of fp
    PyObject *encoders;
    PyObject *default_handler;
    PyObject *shared;
    PyObject *timezone;
    bool timestamp_format;
    bool value_sharing;
} EncoderObject;

PyTypeObject EncoderType;
