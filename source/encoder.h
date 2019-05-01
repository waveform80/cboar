#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>

typedef struct {
    PyObject_HEAD
    PyObject *write;    // cached write() method of fp
    PyObject *output;
    PyObject *encoders;
    PyObject *default_handler;
    PyObject *shared;
    PyObject *timezone;
    PyObject *shared_handler;
    uint8_t enc_style;  // 0=regular, 1=canonical, 2=custom
    bool timestamp_format;
    bool value_sharing;
} EncoderObject;

PyTypeObject EncoderType;
