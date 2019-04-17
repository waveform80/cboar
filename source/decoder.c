#define PY_SSIZE_T_CLEAN
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include <stdint.h>
#include <math.h>
#include <Python.h>
#include <structmember.h>
#include <datetime.h>
#include "decoder.h"


static int Decoder_set_fp(DecoderObject *, PyObject *, void *);
static int Decoder_set_tag_hook(DecoderObject *, PyObject *, void *);
static int Decoder_set_object_hook(DecoderObject *, PyObject *, void *);


/* Decoder.__del__(self) */
static void
Decoder_dealloc(DecoderObject *self)
{
    Py_XDECREF(self->read);
    Py_XDECREF(self->tag_hook);
    Py_XDECREF(self->object_hook);
    Py_XDECREF(self->shareables);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


/* Decoder.__new__(cls, *args, **kwargs) */
static PyObject *
Decoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    DecoderObject *self;

    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
        return NULL;

    self = (DecoderObject *) type->tp_alloc(type, 0);
    if (self) {
        Py_INCREF(Py_None);
        self->read = Py_None;
        Py_INCREF(Py_None);
        self->tag_hook = Py_None;
        Py_INCREF(Py_None);
        self->object_hook = Py_None;
        Py_INCREF(Py_None);
        self->shareables = Py_None;
        self->immutable = false;
    }
    return (PyObject *) self;
}


/* Decoder.__init__(self, fp=None, tag_hook=None, object_hook=None) */
static int
Decoder_init(DecoderObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {
        "fp", "tag_hook", "object_hook", NULL
    };
    PyObject *fp = NULL, *tag_hook = NULL, *object_hook = NULL, *tmp;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OO", keywords,
                &fp, &tag_hook, &object_hook))
        return -1;

    if (Decoder_set_fp(self, fp, NULL) == -1)
        return -1;
    if (tag_hook && Decoder_set_tag_hook(self, tag_hook, NULL) == -1)
        return -1;
    if (object_hook && Decoder_set_object_hook(self, object_hook, NULL) == -1)
        return -1;

    tmp = self->shareables;
    self->shareables = PyDict_New();
    Py_XDECREF(tmp);
    if (!self->shareables)
        return -1;

    return 0;
}


/* Decoder._get_fp(self) */
static PyObject *
Decoder_get_fp(DecoderObject *self, void *closure)
{
    PyObject *ret = PyMethod_GET_SELF(self->read);
    Py_INCREF(ret);
    return ret;
}


/* Decoder._set_fp(self, value) */
static int
Decoder_set_fp(DecoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp, *read;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete fp attribute");
        return -1;
    }
    read = PyObject_GetAttrString(value, "read");
    if (!(read && PyCallable_Check(read))) {
        PyErr_SetString(PyExc_ValueError,
                        "fp object must have a callable read method");
        return -1;
    }

    // See note in encoder.c / Encoder_set_fp
    tmp = self->read;
    Py_INCREF(read);
    self->read = read;
    Py_DECREF(tmp);
    return 0;
}


/* Decoder._get_tag_hook(self) */
static PyObject *
Decoder_get_tag_hook(DecoderObject *self, void *closure)
{
    Py_INCREF(self->tag_hook);
    return self->tag_hook;
}


/* Decoder._set_tag_hook(self, value) */
static int
Decoder_set_tag_hook(DecoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete tag_hook attribute");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        PyErr_SetString(PyExc_ValueError,
                        "tag_hook must be callable or None");
        return -1;
    }

    tmp = self->tag_hook;
    Py_INCREF(value);
    self->tag_hook = value;
    Py_DECREF(tmp);
    return 0;
}


/* Decoder._get_object_hook(self) */
static PyObject *
Decoder_get_object_hook(DecoderObject *self, void *closure)
{
    Py_INCREF(self->object_hook);
    return self->object_hook;
}


/* Decoder._set_object_hook(self, value) */
static int
Decoder_set_object_hook(DecoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete object_hook attribute");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        PyErr_SetString(PyExc_ValueError,
                        "object_hook must be callable or None");
        return -1;
    }

    tmp = self->object_hook;
    Py_INCREF(value);
    self->object_hook = value;
    Py_DECREF(tmp);
    return 0;
}


static PyGetSetDef Decoder_getsetters[] = {
    {"fp", (getter) Decoder_get_fp, (setter) Decoder_set_fp,
        "input file-like object", NULL},
    {"tag_hook", (getter) Decoder_get_tag_hook, (setter) Decoder_set_tag_hook,
        "hook called when decoding an unknown semantic tag", NULL},
    {"object_hook", (getter) Decoder_get_object_hook, (setter) Decoder_set_object_hook,
        "hook called when decoding any dict", NULL},
    {NULL}
};

static PyMethodDef Decoder_methods[] = {
    /*{"decode_length", (PyCFunction) Decoder_decode_length, METH_VARARGS,
        "decode the length prefix from the input"},
    {"decode", (PyCFunction) Decoder_decode, METH_O,
        "decode the next value from the input"},
    {"decode_uint", (PyCFunction) Decoder_decode_uint, METH_O,
        "decode an unsigned integer from the input"},
    {"decode_negint", (PyCFunction) Decoder_decode_negint, METH_O,
        "decode a negative integer from the input"},
    {"decode_float", (PyCFunction) Decoder_decode_float, METH_O,
        "decode a floating-point value from the input"},
    {"decode_boolean", (PyCFunction) Decoder_decode_boolean, METH_O,
        "decode a boolean value from the input"},*/
    {NULL}
};

PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.Decoder",
    .tp_doc = "CBOAR encoder objects",
    .tp_basicsize = sizeof(DecoderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Decoder_new,
    .tp_init = (initproc) Decoder_init,
    .tp_dealloc = (destructor) Decoder_dealloc,
    .tp_getset = Decoder_getsetters,
    .tp_methods = Decoder_methods,
};

