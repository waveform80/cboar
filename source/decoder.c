#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include <stdint.h>
#include <math.h>
#include <structmember.h>
#include <datetime.h>
#include "module.h"
#include "decoder.h"


typedef
    union {
        struct {
            unsigned int subtype: 5;
            unsigned int major: 3;
        };
        char byte;
    } LeadByte;

static int Decoder_set_fp(DecoderObject *, PyObject *, void *);
static int Decoder_set_tag_hook(DecoderObject *, PyObject *, void *);
static int Decoder_set_object_hook(DecoderObject *, PyObject *, void *);
static int Decoder_set_str_errors(DecoderObject *, PyObject *, void *);

static PyObject * Decoder__decode_bytestring(DecoderObject *, uint8_t);
static PyObject * Decoder__decode_string(DecoderObject *, uint8_t);

static PyObject * Decoder_decode_shareable(DecoderObject *);
static PyObject * Decoder_decode_set(DecoderObject *);
static PyObject * Decoder_decode(DecoderObject *);


/* Decoder.__del__(self) */
static void
Decoder_dealloc(DecoderObject *self)
{
    Py_XDECREF(self->read);
    Py_XDECREF(self->tag_hook);
    Py_XDECREF(self->object_hook);
    Py_XDECREF(self->shareables);
    Py_XDECREF(self->str_errors);
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
        self->shareables = PyDict_New();
        if (!self->shareables) {
            Py_DECREF(self);
            return NULL;
        }
        Py_INCREF(Py_None);
        self->read = Py_None;
        Py_INCREF(Py_None);
        self->tag_hook = Py_None;
        Py_INCREF(Py_None);
        self->object_hook = Py_None;
        self->str_errors = PyBytes_FromString("strict");
        self->immutable = false;
        self->shared_index = -1;
    }
    return (PyObject *) self;
}


/* Decoder.__init__(self, fp=None, tag_hook=None, object_hook=None) */
static int
Decoder_init(DecoderObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {
        "fp", "tag_hook", "object_hook", "str_errors", NULL
    };
    PyObject *fp = NULL, *tag_hook = NULL, *object_hook = NULL,
             *str_errors = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OOO", keywords,
                &fp, &tag_hook, &object_hook, &str_errors))
        return -1;

    if (Decoder_set_fp(self, fp, NULL) == -1)
        return -1;
    if (tag_hook && Decoder_set_tag_hook(self, tag_hook, NULL) == -1)
        return -1;
    if (object_hook && Decoder_set_object_hook(self, object_hook, NULL) == -1)
        return -1;
    if (str_errors && Decoder_set_str_errors(self, str_errors, NULL) == -1)
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
    read = PyObject_GetAttr(value, _CBOAR_str_read);
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


/* Decoder._get_str_errors(self) */
static PyObject *
Decoder_get_str_errors(DecoderObject *self, void *closure)
{
    return PyUnicode_DecodeASCII(
            PyBytes_AS_STRING(self->str_errors),
            PyBytes_GET_SIZE(self->str_errors), "strict");
}


/* Decoder._set_str_errors(self, value) */
static int
Decoder_set_str_errors(DecoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp, *bytes;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete str_errors attribute");
        return -1;
    }
    if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_ValueError, "str_errors must be a str value");
        return -1;
    }
    bytes = PyUnicode_AsASCIIString(value);
    if (!bytes) {
        PyErr_SetString(PyExc_ValueError,
                "str_errors must be one of the strings: 'strict', 'error', "
                "or 'replace'");
        return -1;
    }
    if (strcmp(PyBytes_AS_STRING(bytes), "strict") &&
            strcmp(PyBytes_AS_STRING(bytes), "error") &&
            strcmp(PyBytes_AS_STRING(bytes), "replace")) {
        PyErr_SetString(PyExc_ValueError,
                "str_errors must be one of the strings: 'strict', 'error', "
                "or 'replace'");
        return -1;
    }

    tmp = self->str_errors;
    self->str_errors = bytes;
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


static PyObject *
Decoder__set_shareable(DecoderObject *self, PyObject *container)
{
    PyObject *key;

    if (self->shared_index == -1)
        Py_RETURN_NONE;
    // TODO use weakrefs? or explicitly empty dict?
    key = PyLong_FromLong(self->shared_index);
    if (key) {
        PyDict_SetItem(self->shareables, key, container);
        Py_DECREF(key);
    }
    Py_RETURN_NONE;
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
Decoder__decode_definite_bytestring(DecoderObject *self, uint64_t length)
{
    PyObject *ret = NULL;

    if (length > PY_SSIZE_T_MAX)
        return NULL;
    ret = PyBytes_FromStringAndSize(NULL, length);
    if (!ret)
        return NULL;
    if (Decoder__read(self, PyBytes_AS_STRING(ret), length) == -1) {
        Py_DECREF(ret);
        return NULL;
    }
    return ret;
}


static PyObject *
Decoder__decode_indefinite_bytestrings(DecoderObject *self)
{
    PyObject *list, *ret = NULL;
    LeadByte lead;

    list = PyList_New(0);
    if (list) {
        while (1) {
            if (Decoder__read(self, &lead.byte, 1) == -1)
                break;
            if (lead.major == 2) {
                ret = Decoder__decode_bytestring(self, lead.subtype);
                if (ret) {
                    PyList_Append(list, ret);
                    Py_DECREF(ret);
                    ret = NULL;
                }
            } else if (lead.major == 7 && lead.subtype == 31) { // break-code
                ret = PyObject_CallMethodObjArgs(
                        _CBOAR_empty_bytes, _CBOAR_str_join, list, NULL);
                break;
            } else {
                PyErr_SetString(PyExc_ValueError,
                                "non-bytestring found in indefinite length "
                                "bytestring");
                break;
            }
        }
        Py_DECREF(list);
    }
    return ret;
}


static PyObject *
Decoder__decode_bytestring(DecoderObject *self, uint8_t subtype)
{
    // major type 2
    uint64_t length;
    bool indefinite = true;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        return Decoder__decode_indefinite_bytestrings(self);
    else
        return Decoder__decode_definite_bytestring(self, length);
}


// NOTE: It may seem redundant to repeat the definite and indefinite routines
// to handle UTF-8 strings but there is a reason to do this separately.
// Specifically, the CBOR spec states (in sec. 2.2):
//
//     Text strings with indefinite lengths act the same as byte strings with
//     indefinite lengths, except that all their chunks MUST be definite-length
//     text strings.  Note that this implies that the bytes of a single UTF-8
//     character cannot be spread between chunks: a new chunk can only be
//     started at a character boundary.
//
// This precludes using the indefinite bytestring decoder above as that would
// happily ignore UTF-8 characters split across chunks.


static PyObject *
Decoder__decode_definite_string(DecoderObject *self, uint64_t length)
{
    PyObject *ret = NULL;
    char *buf;

    if (length > PY_SSIZE_T_MAX)
        return NULL;
    buf = PyMem_Malloc(length);
    if (!buf)
        return PyErr_NoMemory();

    if (Decoder__read(self, buf, length) == 0)
        ret = PyUnicode_DecodeUTF8(
                buf, length, PyBytes_AS_STRING(self->str_errors));
    PyMem_Free(buf);
    return ret;
}


static PyObject *
Decoder__decode_indefinite_strings(DecoderObject *self)
{
    PyObject *list, *ret = NULL;
    LeadByte lead;

    list = PyList_New(0);
    if (list) {
        while (1) {
            if (Decoder__read(self, &lead.byte, 1) == -1)
                break;
            if (lead.major == 3) {
                ret = Decoder__decode_string(self, lead.subtype);
                if (ret) {
                    PyList_Append(list, ret);
                    Py_DECREF(ret);
                    ret = NULL;
                }
            } else if (lead.major == 7 && lead.subtype == 31) { // break-code
                ret = PyObject_CallMethodObjArgs(
                        _CBOAR_empty_str, _CBOAR_str_join, list, NULL);
                break;
            } else {
                PyErr_SetString(PyExc_ValueError,
                                "non-string found in indefinite length string");
                break;
            }
        }
        Py_DECREF(list);
    }
    return ret;
}


static PyObject *
Decoder__decode_string(DecoderObject *self, uint8_t subtype)
{
    // major type 3
    uint64_t length;
    bool indefinite = true;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        return Decoder__decode_indefinite_strings(self);
    else
        return Decoder__decode_definite_string(self, length);
}


static PyObject *
Decoder__decode_indefinite_array(DecoderObject *self)
{
    PyObject *item, *ret = NULL;
    bool fail;

    ret = PyList_New(0);
    if (ret) {
        Decoder__set_shareable(self, ret);
        while (1) {
            // XXX Recursion
            item = Decoder_decode(self);
            if (item == break_marker) {
                Py_DECREF(item);
                break;
            } else if (item) {
                fail = PyList_Append(ret, item);
                Py_DECREF(item);
                if (fail)
                    goto error;
            } else
                goto error;
        }
        if (self->immutable) {
            PyObject *tmp = PyList_AsTuple(ret);
            if (tmp) {
                Py_DECREF(ret);
                ret = tmp;
            } else
                goto error;
        }
    }
    return ret;
error:
    Py_DECREF(ret);
    return NULL;
}


static PyObject *
Decoder__decode_definite_array(DecoderObject *self, uint64_t length)
{
    PyObject *item, *ret = NULL;

    if (self->immutable) {
        ret = PyTuple_New(length);
        // XXX Danger Will Robinson! It's perfectly valid to share a tuple
        // value, but this opens up the possibility of a tuple containing
        // itself recursively which is normally impossible...
        Decoder__set_shareable(self, ret);
        if (ret) {
            for (Py_ssize_t i = 0; i < length; ++i) {
                // XXX Recursion
                item = Decoder_decode(self);
                if (item)
                    PyTuple_SET_ITEM(ret, i, item);
                else
                    goto error;
            }
        }
    } else {
        ret = PyList_New(length);
        Decoder__set_shareable(self, ret);
        if (ret) {
            for (Py_ssize_t i = 0; i < length; ++i) {
                // XXX Recursion
                item = Decoder_decode(self);
                if (item)
                    PyList_SET_ITEM(ret, i, item);
                else
                    goto error;
            }
        }
    }
    return ret;
error:
    Py_DECREF(ret);
    return NULL;
}


static PyObject *
Decoder__decode_array(DecoderObject *self, uint8_t subtype)
{
    // major type 4
    uint64_t length;
    bool indefinite = true;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        return Decoder__decode_indefinite_array(self);
    else
        return Decoder__decode_definite_array(self, length);
}


static PyObject *
Decoder__decode_map(DecoderObject *self, uint8_t subtype)
{
    // major type 5
    uint64_t length;
    bool old_immutable;
    bool fail, indefinite = true;
    PyObject *key, *value, *ret = NULL;

    ret = PyDict_New();
    if (ret) {
        Decoder__set_shareable(self, ret);
        if (Decoder__decode_length(self, subtype, &length, &indefinite) == 0) {
            if (indefinite) {
                while (1) {
                    old_immutable = self->immutable;
                    self->immutable = true;
                    // XXX Recursion
                    key = Decoder_decode(self);
                    self->immutable = old_immutable;
                    if (key == break_marker) {
                        Py_DECREF(key);
                        break;
                    } else if (key) {
                        // XXX Recursion
                        value = Decoder_decode(self);
                        if (value) {
                            fail = PyDict_SetItem(ret, key, value);
                            Py_DECREF(value);
                        } else
                            fail = true;
                        Py_DECREF(key);
                    } else
                        goto error;
                    if (fail)
                        goto error;
                }
            } else {
                while (length--) {
                    old_immutable = self->immutable;
                    self->immutable = true;
                    // XXX Recursion
                    key = Decoder_decode(self);
                    self->immutable = old_immutable;
                    if (key) {
                        // XXX Recursion
                        value = Decoder_decode(self);
                        if (value) {
                            fail = PyDict_SetItem(ret, key, value);
                            Py_DECREF(value);
                        } else
                            fail = true;
                        Py_DECREF(key);
                    } else
                        goto error;
                    if (fail)
                        goto error;
                }
            }
        } else
            goto error;
    }
    return ret;
error:
    Py_DECREF(ret);
    return NULL;
}


static PyObject *
Decoder__decode_semantic(DecoderObject *self, uint8_t subtype)
{
    // major type 6
    uint64_t tagnum;
    PyObject *tag, *ret = NULL;

    if (Decoder__decode_length(self, subtype, &tagnum, NULL) == -1)
        return NULL;
    switch (tagnum) {
        case 28: ret = Decoder_decode_shareable(self);
        case 258: ret = Decoder_decode_set(self);
        // TODO
        default:
            tag = PyStructSequence_New(&CBORTagType);
            if (tag) {
                PyStructSequence_SET_ITEM(tag, 0, PyLong_FromUnsignedLongLong(tagnum));
                if (PyStructSequence_GET_ITEM(tag, 0)) {
                    PyStructSequence_SET_ITEM(tag, 1, Decoder_decode(self));
                    if (PyStructSequence_GET_ITEM(tag, 1)) {
                        if (self->tag_hook == Py_None) {
                            Py_INCREF(tag);
                            ret = tag;
                        } else
                            ret = PyObject_CallFunctionObjArgs(self->tag_hook, self, tag, NULL);
                    }
                }
                Py_DECREF(tag);
            }
    }
    return ret;
}


static PyObject *
Decoder__decode_special(DecoderObject *self, uint8_t subtype)
{
    // major type 7
    // TODO
    switch (subtype) {
        case 20:
            Py_RETURN_FALSE;
        case 21:
            Py_RETURN_TRUE;
        case 22:
            Py_RETURN_NONE;
        case 31:
            Py_INCREF(break_marker);
            return break_marker;
        default:
            return NULL;
    }
}


static PyObject *
Decoder_decode_shareable(DecoderObject *self)
{
    // semantic type 28
    int32_t old_index;
    PyObject *key, *ret = NULL;

    old_index = self->shared_index;
    self->shared_index = PyDict_Size(self->shareables);
    key = PyLong_FromLong(self->shared_index);
    if (key) {
        if (PyDict_SetItem(self->shareables, key, Py_None) == 0) {
            // XXX Recursive call
            ret = Decoder_decode(self);
        }
        Py_DECREF(key);
    }
    self->shared_index = old_index;
    return ret;
}


static PyObject *
Decoder_decode_set(DecoderObject *self)
{
    // semantic type 258
    bool old_immutable;
    PyObject *tmp, *ret = NULL;

    // TODO Warn/error when shared_index != 1
    old_immutable = self->immutable;
    self->immutable = true;
    // XXX Recursive call
    tmp = Decoder_decode(self);
    self->immutable = old_immutable;
    if (tmp) {
        if (self->immutable)
            ret = PyFrozenSet_New(tmp);
        else
            ret = PySet_New(tmp);
        Py_DECREF(tmp);
    }
    return ret;
}


/* Decoder.decode(self) -> obj */
static PyObject *
Decoder_decode(DecoderObject *self)
{
    LeadByte lead;

    if (Decoder__read(self, &lead.byte, 1) == -1)
        return NULL;
    switch (lead.major) {
        case 0: return Decoder__decode_uint(self, lead.subtype);
        case 1: return Decoder__decode_negint(self, lead.subtype);
        case 2: return Decoder__decode_bytestring(self, lead.subtype);
        case 3: return Decoder__decode_string(self, lead.subtype);
        case 4: return Decoder__decode_array(self, lead.subtype);
        case 5: return Decoder__decode_map(self, lead.subtype);
        case 6: return Decoder__decode_semantic(self, lead.subtype);
        case 7: return Decoder__decode_special(self, lead.subtype);
    }

    return NULL;
}


#define PUBLIC_MAJOR(type) \
    static PyObject * \
    Decoder_decode_##type(DecoderObject *self, PyObject *subtype) \
    { \
        return Decoder__decode_##type(self, PyLong_AsUnsignedLong(subtype)); \
    }

PUBLIC_MAJOR(uint);
PUBLIC_MAJOR(negint);
PUBLIC_MAJOR(bytestring);
PUBLIC_MAJOR(string);
PUBLIC_MAJOR(array);
PUBLIC_MAJOR(map);
PUBLIC_MAJOR(semantic);
PUBLIC_MAJOR(special);


static PyGetSetDef Decoder_getsetters[] = {
    {"fp", (getter) Decoder_get_fp, (setter) Decoder_set_fp,
        "input file-like object", NULL},
    {"tag_hook", (getter) Decoder_get_tag_hook, (setter) Decoder_set_tag_hook,
        "hook called when decoding an unknown semantic tag", NULL},
    {"object_hook", (getter) Decoder_get_object_hook, (setter) Decoder_set_object_hook,
        "hook called when decoding any dict", NULL},
    {"str_errors", (getter) Decoder_get_str_errors, (setter) Decoder_set_str_errors,
        "the error mode to use when decoding UTF-8 encoded strings"},
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
    {"decode_string", (PyCFunction) Decoder_decode_string, METH_O,
        "decode a unicode string from the input"},
    {"decode_array", (PyCFunction) Decoder_decode_array, METH_O,
        "decode a list or tuple from the input"},
    {"decode_map", (PyCFunction) Decoder_decode_map, METH_O,
        "decode a dict from the input"},
    {"decode_semantic", (PyCFunction) Decoder_decode_semantic, METH_O,
        "decode a semantically tagged value from the input"},
    {"decode_special", (PyCFunction) Decoder_decode_special, METH_O,
        "decode a special value from the input"},
    /*{"decode_float", (PyCFunction) Decoder_decode_float, METH_O,
        "decode a floating-point value from the input"},
    {"decode_boolean", (PyCFunction) Decoder_decode_boolean, METH_O,
        "decode a boolean value from the input"},*/
    {NULL}
};

PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.Decoder",
    .tp_doc = "CBOAR decoder objects",
    .tp_basicsize = sizeof(DecoderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Decoder_new,
    .tp_init = (initproc) Decoder_init,
    .tp_dealloc = (destructor) Decoder_dealloc,
    .tp_getset = Decoder_getsetters,
    .tp_methods = Decoder_methods,
};
