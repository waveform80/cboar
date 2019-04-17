#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include <stdint.h>
#include <math.h>
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


static int
Decoder__read(DecoderObject *self, char *buf, const uint32_t size)
{
    PyObject *obj;
    char *data;
    int ret = -1;

    obj = PyObject_CallFunction(self->read, "k", size);
    if (obj) {
        assert(PyBytes_CheckExact(obj));
        if (PyBytes_GET_SIZE(obj) == size) {
            data = PyBytes_AS_STRING(obj);
            memcpy(buf, data, size);
            ret = 0;
        }
        Py_DECREF(obj);
    }
    return ret;
}


static int
Decoder__decode_length(DecoderObject *self, uint8_t subtype,
        uint64_t *length, bool *indefinite)
{
    union {
        union { uint64_t value; char buf[sizeof(uint64_t)]; } u64;
        union { uint32_t value; char buf[sizeof(uint32_t)]; } u32;
        union { uint16_t value; char buf[sizeof(uint16_t)]; } u16;
        union { uint8_t value;  char buf[sizeof(uint8_t)];  } u8;
    } value;

    if (subtype < 28) {
        if (subtype < 24) {
            *length = subtype;
        } else if (subtype == 24) {
            if (Decoder__read(self, value.u8.buf, sizeof(uint8_t)) == -1)
                return -1;
            *length = value.u8.value;
        } else if (subtype == 25) {
            if (Decoder__read(self, value.u16.buf, sizeof(uint16_t)) == -1)
                return -1;
            *length = be16toh(value.u16.value);
        } else if (subtype == 26) {
            if (Decoder__read(self, value.u32.buf, sizeof(uint32_t)) == -1)
                return -1;
            *length = be32toh(value.u32.value);
        } else {
            if (Decoder__read(self, value.u64.buf, sizeof(uint64_t)) == -1)
                return -1;
            *length = be64toh(value.u64.value);
        }
        if (indefinite)
            *indefinite = false;
        return 0;
    } else if (subtype == 31 && indefinite && *indefinite) {
        // well, indefinite is already true so nothing to see here...
        return 0;
    } else {
        PyErr_Format(PyExc_ValueError, "unknown unsigned integer subtype 0x%x",
                     subtype);
        return -1;
    }
}


static PyObject *
Decoder__decode_uint(DecoderObject *self, uint8_t subtype)
{
    // major type 0
    uint64_t length;

    if (Decoder__decode_length(self, subtype, &length, NULL) == -1)
        return NULL;
    return PyLong_FromUnsignedLongLong(length);
}


static PyObject *
Decoder__decode_negint(DecoderObject *self, uint8_t subtype)
{
    // major type 1
    PyObject *value, *one, *ret = NULL;

    value = Decoder__decode_uint(self, subtype);
    if (value) {
        one = PyLong_FromLong(1);
        if (one) {
            ret = PyNumber_Negative(value);
            if (ret) {
                Py_DECREF(value);
                value = ret;
                ret = PyNumber_Subtract(value, one);
            }
            Py_DECREF(one);
        }
        Py_DECREF(value);
    }
    return ret;
}


static PyObject *
Decoder__decode_bytestring(DecoderObject *self, uint8_t subtype)
{
    // major type 2
    PyObject *ret = NULL;
    char *buf;
    uint64_t length;
    bool indefinite = true;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite) {
        // TODO
    } else {
        if (length > PY_SSIZE_T_MAX)
            return NULL;
        ret = PyBytes_FromStringAndSize(NULL, length);
        if (!ret)
            return NULL;
        buf = PyBytes_AS_STRING(ret);
        if (Decoder__read(self, buf, length) == -1) {
            Py_DECREF(ret);
            return NULL;
        }
        return ret;
    }
}


#define PUBLIC_METHOD(type) \
    static PyObject * \
    Decoder_decode_##type(DecoderObject *self, PyObject *subtype) \
    { \
        return Decoder__decode_##type(self, PyLong_AsUnsignedLong(subtype)); \
    }

PUBLIC_METHOD(uint);
PUBLIC_METHOD(negint);
PUBLIC_METHOD(bytestring);


/* Decoder.decode(self) -> obj */
static PyObject *
Decoder_decode(DecoderObject *self)
{
    union {
        struct {
            unsigned int subtype: 5;
            unsigned int major: 3;
        };
        char byte;
    } lead;

    if (Decoder__read(self, &lead.byte, 1) == -1)
        return NULL;
    switch (lead.major) {
        case 0: return Decoder__decode_uint(self, lead.subtype);
        case 1: return Decoder__decode_negint(self, lead.subtype);
        case 2: return Decoder__decode_bytestring(self, lead.subtype);
    }

    return NULL;
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
    {"decode", (PyCFunction) Decoder_decode, METH_NOARGS,
        "decode the next value from the input"},
    {"decode_uint", (PyCFunction) Decoder_decode_uint, METH_O,
        "decode an unsigned integer from the input"},
    {"decode_negint", (PyCFunction) Decoder_decode_negint, METH_O,
        "decode a negative integer from the input"},
    {"decode_bytestring", (PyCFunction) Decoder_decode_bytestring, METH_O,
        "decode a bytes string from the input"},
    /*{"decode_float", (PyCFunction) Decoder_decode_float, METH_O,
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

