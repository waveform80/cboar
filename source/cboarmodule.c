#include <Python.h>
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include "structmember.h"


typedef struct {
    PyObject_HEAD
    PyObject *encoders;
    PyObject *fp;       // current output file-like object
    PyObject *default_handler;
    int timestamp_format;
    bool value_sharing;
} EncoderObject;


/* Encoder.__del__(self) */
static void
Encoder_dealloc(EncoderObject *self)
{
    Py_XDECREF(self->fp);
    Py_XDECREF(self->default_handler);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


/* Encoder.__new__(cls, *args, **kwargs) */
static PyObject *
Encoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    EncoderObject *self;

    self = (EncoderObject *) type->tp_alloc(type, 0);
    if (self) {
        Py_INCREF(Py_None);
        self->fp = Py_None;
        Py_INCREF(Py_None);
        self->default_handler = Py_None;
        self->timestamp_format = 0;
        self->value_sharing = false;
    }
    return (PyObject *) self;
}


/* Encoder.__init__(self, fp=None, default_handler=None, timestamp_format=0,
 *                  value_sharing=False) */
static int
Encoder_init(EncoderObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {
        "fp", "default_handler", "timestamp_format", "value_sharing", NULL
    };
    PyObject *fp = NULL, *default_handler = NULL, *tmp;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Oip", keywords,
                &fp, &default_handler, &self->timestamp_format,
                &self->value_sharing))
        return -1;
    tmp = PyObject_GetAttrString(fp, "write");
    if (!(tmp && PyCallable_Check(tmp))) {
        PyErr_SetString(PyExc_ValueError, "fp object must have a write method");
        return -1;
    }
    if (default_handler && default_handler != Py_None && !PyCallable_Check(default_handler)) {
        PyErr_SetString(PyExc_ValueError, "default_handler must be callable or None");
        return -1;
    }

    tmp = self->fp;
    Py_INCREF(fp);
    self->fp = fp;
    Py_XDECREF(tmp);
    if (default_handler) {
        tmp = self->default_handler;
        Py_INCREF(default_handler);
        self->default_handler = default_handler;
        Py_XDECREF(tmp);
    }
    return 0;
}


/* Encoder._get_fp(self) */
static PyObject *
Encoder_getfp(EncoderObject *self, void *closure)
{
    Py_INCREF(self->fp);
    return self->fp;
}


/* Encoder._set_fp(self, value) */
static int
Encoder_setfp(EncoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete fp attribute");
        return -1;
    }
    tmp = PyObject_GetAttrString(value, "write");
    if (!(tmp && PyCallable_Check(tmp))) {
        PyErr_SetString(PyExc_ValueError, "fp object must have a write method");
        return -1;
    }

    tmp = self->fp;
    Py_INCREF(value);
    self->fp = value;
    Py_DECREF(tmp);
    return 0;
}


/* Encoder._get_default(self) */
static PyObject *
Encoder_getdefault(EncoderObject *self, void *closure)
{
    Py_INCREF(self->default_handler);
    return self->default_handler;
}


/* Encoder._set_default(self, value) */
static int
Encoder_setdefault(EncoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete default_handler attribute");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        PyErr_SetString(PyExc_ValueError, "default_handler must be callable or None");
        return -1;
    }

    tmp = self->default_handler;
    Py_INCREF(value);
    self->default_handler = value;
    Py_DECREF(tmp);
    return 0;
}


/* Encoder.encode(self, value) */
static PyObject *
Encoder_encode(EncoderObject *self, PyObject *value)
{
    // TODO
    Py_RETURN_NONE;
}


static int
Encoder__write(EncoderObject *self, char *buf, uint32_t length)
{
    PyObject *obj;

    // XXX Cache the write method? It'd be a bit weird if it changed in the
    // middle of encoding ... although technically that would still be valid
    // behaviour
    obj = PyObject_CallMethod(self->fp, "write", "y#", buf, length);
    Py_XDECREF(obj);
    return obj == NULL ? -1 : 0;
}


static int
Encoder__encode_length(EncoderObject *self, uint8_t major_tag, uint64_t length)
{
    char buf[sizeof(uint64_t) + 1];

    if (length < 24) {
        buf[0] = major_tag | length;
        return Encoder__write(self, buf, 1);
    }
    else if (length <= UCHAR_MAX) {
        buf[0] = major_tag | 24;
        buf[1] = length;
        return Encoder__write(self, buf, sizeof(uint8_t) + 1);
    }
    else if (length <= USHRT_MAX) {
        buf[0] = major_tag | 25;
        *((uint16_t*)(buf + 1)) = htobe16(length);
        return Encoder__write(self, buf, sizeof(uint16_t) + 1);
    }
    else if (length <= UINT_MAX) {
        buf[0] = major_tag | 26;
        *((uint32_t*)(buf + 1)) = htobe32(length);
        return Encoder__write(self, buf, sizeof(uint32_t) + 1);
    }
    else {
        buf[0] = major_tag | 27;
        *((uint64_t*)(buf + 1)) = htobe64(length);
        return Encoder__write(self, buf, sizeof(uint64_t) + 1);
    }
}


static int
Encoder__encode_semantic(EncoderObject *self, uint32_t tag, PyObject *value)
{
    PyObject *obj;

    if (Encoder__encode_length(self, 0xC0, tag) == -1)
        return -1;
    obj = Encoder_encode(self, value);
    Py_XDECREF(obj);
    return obj == NULL ? -1 : 0;
}


/* Encoder.encode_length(self, major_tag, length) */
static PyObject *
Encoder_encode_length(EncoderObject *self, PyObject *args)
{
    uint8_t major_tag;
    uint64_t length;

    if (!PyArg_ParseTuple(args, "BK", &major_tag, &length))
        return NULL;
    if (Encoder__encode_length(self, major_tag, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


/* Encoder.encode_semantic(self, tag, buf) */
static PyObject *
Encoder_encode_semantic(EncoderObject *self, PyObject *args)
{
    uint32_t tag;
    PyObject *value;

    if (!PyArg_ParseTuple(args, "kO", &tag, &value))
        return NULL;
    if (Encoder__encode_semantic(self, tag, value) == -1)
        return NULL;
    Py_RETURN_NONE;
}


static PyObject *
Encoder__encode_negative(PyObject *value) {
    PyObject *neg, *one;

    // return -value - 1
    one = PyLong_FromLong(1);
    if (!one)
        return NULL;
    neg = PyNumber_Negative(value);
    if (!neg) {
        Py_DECREF(one);
        return NULL;
    }
    value = PyNumber_Subtract(neg, one);
    Py_DECREF(one);
    Py_DECREF(neg);
    return value;
}


/* Encoder.encode_int(self, value) */
static PyObject *
Encoder_encode_int(EncoderObject *self, PyObject *value)
{
    long long int_value;
    int overflow;

    if (!PyLong_Check(value))
        return NULL;
    int_value = PyLong_AsLongLongAndOverflow(value, &overflow);
    if (overflow == 0) {
        if (Encoder__encode_length(self, 0, int_value) == -1)
            return NULL;
    }
    else {
        uint32_t major_tag;
        long length;
        PyObject *bits = NULL, *buf = NULL;

        if (overflow == -1) {
            major_tag = 3;
            value = Encoder__encode_negative(value);
            if (!value)
                return NULL;
            // value is now an owned reference, instead of borrowed
        }
        else {
            major_tag = 2;
            // convert value to an owned reference; this isn't strictly
            // necessary but simplifies memory handling in the next bit
            Py_INCREF(value);
        }
        bits = PyObject_CallMethod(value, "bit_length", NULL);
        if (!bits)
            goto cleanup;
        length = PyLong_AsLong(bits);
        if (PyErr_Occurred())
            goto cleanup;
        buf = PyObject_CallMethod(value, "to_bytes", "ls", (length + 7) / 8, "big");
        if (!buf)
            goto cleanup;
        if (Encoder__encode_semantic(self, major_tag, buf) == -1) {
            Py_DECREF(buf);
            buf = NULL; // indicating error
        }
        else
            Py_DECREF(buf);
cleanup:
        Py_XDECREF(bits);
        Py_DECREF(value);
        if (!buf)
            return NULL;
    }
    Py_RETURN_NONE;
}


/* Encoder.encode_bytes(self, value) */
static PyObject *
Encoder_encode_bytes(EncoderObject *self, PyObject *value)
{
    char *buf;
    Py_ssize_t length;

    if (PyBytes_AsStringAndSize(value, &buf, &length) == -1)
        return NULL;
    if (Encoder__encode_length(self, 0x40, length) == -1)
        return NULL;
    if (Encoder__write(self, buf, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// TODO encode_bytearray

// TODO encode_strings


static PyMemberDef Encoder_members[] = {
    {"timestamp_format", T_INT, offsetof(EncoderObject, timestamp_format), 0,
        "the sub-type to use when encoding datetime objects"},
    {"value_sharing", T_BOOL, offsetof(EncoderObject, value_sharing), 0,
        "if True, then efficiently encode recursive structures"},
    {NULL}
};

static PyGetSetDef Encoder_getsetters[] = {
    {"fp", (getter) Encoder_getfp, (setter) Encoder_setfp,
        "output file-like object", NULL},
    {"default_handler", (getter) Encoder_getdefault, (setter) Encoder_setdefault,
        "default handler called when encoding unknown objects", NULL},
    {NULL}
};

static PyMethodDef Encoder_methods[] = {
    {"encode", (PyCFunction) Encoder_encode, METH_O,
        "encode the specified *value* to the output"},
    {"encode_int", (PyCFunction) Encoder_encode_int, METH_O,
        "encode the specified integer *value* to the output"},
    {"encode_bytes", (PyCFunction) Encoder_encode_bytes, METH_O,
        "encode the specified bytes *value* to the output"},
    {"encode_length", (PyCFunction) Encoder_encode_length, METH_VARARGS,
        "encode the specified *major_tag* with the specified *length* to the output"},
    {"encode_semantic", (PyCFunction) Encoder_encode_semantic, METH_VARARGS,
        "encode the specified semantic *semantic_tag* with the primitive *value* to the output"},
    {NULL}
};

static PyTypeObject EncoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.Encoder",
    .tp_doc = "CBOAR encoder objects",
    .tp_basicsize = sizeof(EncoderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Encoder_new,
    .tp_init = (initproc) Encoder_init,
    .tp_dealloc = (destructor) Encoder_dealloc,
    .tp_members = Encoder_members,
    .tp_getset = Encoder_getsetters,
    .tp_methods = Encoder_methods,
};


static struct PyModuleDef _cboarmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_cboar",
    .m_doc = NULL,
    .m_size = -1,                   // size of per-interpreter state
};


PyMODINIT_FUNC
PyInit__cboar(void)
{
    PyObject *m;

    if (PyType_Ready(&EncoderType) < 0)
        return NULL;

    m = PyModule_Create(&_cboarmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&EncoderType);
    PyModule_AddObject(m, "Encoder", (PyObject *) &EncoderType);

    return m;
}
