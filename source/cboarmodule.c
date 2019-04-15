#define PY_SSIZE_T_CLEAN
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
    Py_XDECREF(self->encoders);
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
    PyObject *fp = NULL, *default_handler = NULL, *tmp, *collections,
             *ordered_dict;

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

    collections = PyImport_ImportModule("collections");
    if (!collections)
        return -1;
    ordered_dict = PyObject_GetAttrString(collections, "OrderedDict");
    if (!ordered_dict) {
        Py_DECREF(collections);
        return -1;
    }

    tmp = self->encoders;
    self->encoders = PyObject_CallObject(ordered_dict, NULL);
    if (!self->encoders) {
        Py_DECREF(collections);
        return -1;
    }
    Py_DECREF(collections);
    Py_XDECREF(tmp);
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


static PyObject *
Encoder__load_type(PyObject *type_tuple)
{
    PyObject *mod_name, *module, *type_name, *type, *import_list;

    if (PyTuple_GET_SIZE(type_tuple) != 2) {
        PyErr_SetString(PyExc_ValueError, "deferred load encoder types must be a 2-tuple");
        return NULL;
    }
    mod_name = PyTuple_GET_ITEM(type_tuple, 0);
    if (!PyUnicode_Check(mod_name)) {
        PyErr_SetString(PyExc_ValueError, "deferred load element 0 is not a string");
        return NULL;
    }
    type_name = PyTuple_GET_ITEM(type_tuple, 1);
    if (!PyUnicode_Check(type_name)) {
        PyErr_SetString(PyExc_ValueError, "deferred load element 1 is not a string");
        return NULL;
    }

    import_list = PyList_New(1);
    if (!import_list)
        return NULL;
    Py_INCREF(type_name);
    PyList_SET_ITEM(import_list, 0, type_name);
    module = PyImport_ImportModuleLevelObject(mod_name, NULL, NULL, import_list, 0);
    Py_DECREF(import_list);
    if (!module)
        return NULL;
    type = PyObject_GetAttr(module, type_name);
    Py_DECREF(module);
    return type;
}


static PyObject *
Encoder__replace_type(EncoderObject *self, PyObject *item)
{
    PyObject *ret = NULL, *enc_type, *encoder;

    enc_type = PyTuple_GET_ITEM(item, 0);
    encoder = PyTuple_GET_ITEM(item, 1);
    // this function is unusual in that enc_type is a borrowed reference on
    // entry, and the return value (a transformed enc_type) is also a borrowed
    // reference; hence we have to INCREF enc_type to ensure it doesn't
    // disappear when removing it from the encoders dict (which might be the
    // only reference to it)
    Py_INCREF(enc_type);
    if (PyObject_DelItem(self->encoders, enc_type) == 0) {
        ret = Encoder__load_type(enc_type);
        if (ret && PyObject_SetItem(self->encoders, ret, encoder) == 0)
            // this DECREF might look unusual but at this point the encoders
            // dict has a ref to the new enc_type, so we want "our" ref to
            // enc_type to be borrowed just as the original was on entry
            Py_DECREF(ret);
        Py_DECREF(enc_type);
    }
    return ret;
}


/* Encoder._find_encoder(type) */
static PyObject *
Encoder__find_encoder(EncoderObject *self, PyObject *type)
{
    PyObject *ret, *enc_type, *items, *iter, *item;

    ret = PyObject_GetItem(self->encoders, type);
    if (!ret && PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyErr_Clear();
        items = PyMapping_Items(self->encoders);
        if (items) {
            iter = PyObject_GetIter(items);
            if (iter) {
                while (!ret && (item = PyIter_Next(iter))) {
                    enc_type = PyTuple_GET_ITEM(item, 0);

                    if (PyTuple_Check(enc_type))
                        enc_type = Encoder__replace_type(self, item);
                    if (enc_type)
                        switch (PyObject_IsSubclass(type, enc_type)) {
                            case 1:
                                ret = PyTuple_GET_ITEM(item, 1);
                                if (PyObject_SetItem(self->encoders, type, ret) == 0)
                                    break;
                                // fall-thru to error case
                            case -1:
                                enc_type = NULL;
                                ret = NULL;
                                break;
                        }
                    Py_DECREF(item);
                    if (!enc_type)
                        break;
                }
                Py_DECREF(iter);
            }
            Py_DECREF(items);
        }
        if (ret)
            Py_XINCREF(ret);
        else if (!PyErr_Occurred())
            PyErr_SetObject(PyExc_KeyError, type);
    }
    return ret;
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
    PyObject *encoder, *ret = NULL;

    encoder = Encoder__find_encoder(self, (PyObject *)Py_TYPE(value));
    if (encoder) {
        PyObject *args = PyTuple_Pack(2, self, value);
        if (args) {
            ret = PyObject_Call(encoder, args, NULL);
            Py_DECREF(args);
        }
        Py_DECREF(encoder);
    }
    return ret;
}


static int
Encoder__write(EncoderObject *self, const char *buf, const uint32_t length)
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
Encoder__encode_length(EncoderObject *self, const uint8_t major_tag,
        const uint64_t length)
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
Encoder__encode_semantic(EncoderObject *self, const uint32_t tag,
        PyObject *value)
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

    int_value = PyLong_AsLongLongAndOverflow(value, &overflow);
    if (overflow == 0) {
        if (value >= 0) {
            if (Encoder__encode_length(self, 0, int_value) == -1)
                return NULL;
        }
        else {
            // avoid overflow in the case where int_value == -2^63
            int_value = -(int_value + 1);
            if (Encoder__encode_length(self, 0x20, int_value) == -1)
                return NULL;
        }
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


/* Encoder.encode_bytearray(self, value) */
static PyObject *
Encoder_encode_bytearray(EncoderObject *self, PyObject *value)
{
    Py_ssize_t length;

    if (!PyByteArray_Check(value)) {
        PyErr_SetString(PyExc_ValueError, "expected bytearray");
        return NULL;
    }
    length = PyByteArray_GET_SIZE(value);
    if (Encoder__encode_length(self, 0x40, length) == -1)
        return NULL;
    if (Encoder__write(self, PyByteArray_AS_STRING(value), length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


/* Encoder.encode_string(self, value) */
static PyObject *
Encoder_encode_string(EncoderObject *self, PyObject *value)
{
    char *buf;
    Py_ssize_t length;

    buf = PyUnicode_AsUTF8AndSize(value, &length);
    if (!buf)
        return NULL;
    if (Encoder__encode_length(self, 0x60, length) == -1)
        return NULL;
    if (Encoder__write(self, buf, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


static PyMemberDef Encoder_members[] = {
    {"encoders", T_OBJECT_EX, offsetof(EncoderObject, encoders), READONLY,
        "the ordered dict mapping types to encoder functions"},
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
    {"_find_encoder", (PyCFunction) Encoder__find_encoder, METH_O,
        "find an encoding function for the specified type"},
    {"encode", (PyCFunction) Encoder_encode, METH_O,
        "encode the specified *value* to the output"},
    {"encode_int", (PyCFunction) Encoder_encode_int, METH_O,
        "encode the specified integer *value* to the output"},
    {"encode_bytes", (PyCFunction) Encoder_encode_bytes, METH_O,
        "encode the specified bytes *value* to the output"},
    {"encode_bytearray", (PyCFunction) Encoder_encode_bytearray, METH_O,
        "encode the specified bytearray *value* to the output"},
    {"encode_string", (PyCFunction) Encoder_encode_string, METH_O,
        "encode the specified string *value* to the output"},
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
    .m_size = -1, // XXX Change to 0?
};


PyMODINIT_FUNC
PyInit__cboar(void)
{
    PyObject *module;

    if (PyType_Ready(&EncoderType) < 0)
        return NULL;

    module = PyModule_Create(&_cboarmodule);
    if (!module)
        return NULL;

    Py_INCREF(&EncoderType);
    if (PyModule_AddObject(module, "Encoder", (PyObject *) &EncoderType) == -1) {
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
