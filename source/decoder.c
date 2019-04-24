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
#include "halffloat.h"
#include "tags.h"
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
static PyObject * Decoder_decode_datestr(DecoderObject *);
static PyObject * Decoder_decode_timestamp(DecoderObject *);
static PyObject * Decoder_decode_fraction(DecoderObject *);
static PyObject * Decoder_decode_positive_bignum(DecoderObject *);
static PyObject * Decoder_decode_negative_bignum(DecoderObject *);
static PyObject * Decoder_decode_simplevalue(DecoderObject *);
static PyObject * Decoder_decode_float16(DecoderObject *);
static PyObject * Decoder_decode_float32(DecoderObject *);
static PyObject * Decoder_decode_float64(DecoderObject *);

static PyObject * Decoder_decode_shareable(DecoderObject *);
static PyObject * Decoder_decode_shared(DecoderObject *);
static PyObject * Decoder_decode_set(DecoderObject *);
static PyObject * Decoder_decode(DecoderObject *);
static PyObject * Decoder_decode_immutable(DecoderObject *);
static PyObject * Decoder_decode_unshared(DecoderObject *);
static PyObject * Decoder_decode_immutable_unshared(DecoderObject *);


// Constructors and destructors //////////////////////////////////////////////

// Decoder.__del__(self)
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


// Decoder.__new__(cls, *args, **kwargs)
static PyObject *
Decoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    DecoderObject *self;

    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
        return NULL;

    self = (DecoderObject *) type->tp_alloc(type, 0);
    if (self) {
        // self.shareables = []
        self->shareables = PyList_New(0);
        if (!self->shareables)
            goto error;
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
error:
    Py_DECREF(self);
    return NULL;
}


// Decoder.__init__(self, fp=None, tag_hook=None, object_hook=None)
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


// Property accessors ////////////////////////////////////////////////////////

// Decoder._get_fp(self)
static PyObject *
Decoder_get_fp(DecoderObject *self, void *closure)
{
    PyObject *ret = PyMethod_GET_SELF(self->read);
    Py_INCREF(ret);
    return ret;
}


// Decoder._set_fp(self, value)
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


// Decoder._get_tag_hook(self)
static PyObject *
Decoder_get_tag_hook(DecoderObject *self, void *closure)
{
    Py_INCREF(self->tag_hook);
    return self->tag_hook;
}


// Decoder._set_tag_hook(self, value)
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
        PyErr_Format(PyExc_ValueError,
                        "invalid tag_hook value %R (must be callable or "
                        "None", value);
        return -1;
    }

    tmp = self->tag_hook;
    Py_INCREF(value);
    self->tag_hook = value;
    Py_DECREF(tmp);
    return 0;
}


// Decoder._get_object_hook(self)
static PyObject *
Decoder_get_object_hook(DecoderObject *self, void *closure)
{
    Py_INCREF(self->object_hook);
    return self->object_hook;
}


// Decoder._set_object_hook(self, value)
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
        PyErr_Format(PyExc_ValueError,
                        "invalid object_hook value %R (must be callable or "
                        "None)", value);
        return -1;
    }

    tmp = self->object_hook;
    Py_INCREF(value);
    self->object_hook = value;
    Py_DECREF(tmp);
    return 0;
}


// Decoder._get_str_errors(self)
static PyObject *
Decoder_get_str_errors(DecoderObject *self, void *closure)
{
    return PyUnicode_DecodeASCII(
            PyBytes_AS_STRING(self->str_errors),
            PyBytes_GET_SIZE(self->str_errors), "strict");
}


// Decoder._set_str_errors(self, value)
static int
Decoder_set_str_errors(DecoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp, *bytes;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete str_errors attribute");
        return -1;
    }
    if (PyUnicode_Check(value)) {
        bytes = PyUnicode_AsASCIIString(value);
        if (bytes) {
            if (!strcmp(PyBytes_AS_STRING(bytes), "strict") ||
                    !strcmp(PyBytes_AS_STRING(bytes), "error") ||
                    !strcmp(PyBytes_AS_STRING(bytes), "replace")) {
                tmp = self->str_errors;
                self->str_errors = bytes;
                Py_DECREF(tmp);
                return 0;
            }
            Py_DECREF(bytes);
        }
    }
    PyErr_Format(PyExc_ValueError,
            "invalid str_errors value %R (must be one of 'strict', "
            "'error', or 'replace'", value);
    return -1;
}


// Utility methods ///////////////////////////////////////////////////////////

static int
Decoder__read(DecoderObject *self, char *buf, const uint64_t size)
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
        } else {
            PyErr_Format(PyExc_ValueError,
                    "premature end of stream (expected to read %d bytes, "
                    "got %d instead)", size, PyBytes_GET_SIZE(obj));
        }
        Py_DECREF(obj);
    }
    return ret;
}


static void
Decoder__set_shareable(DecoderObject *self, PyObject *value)
{
    if (self->shared_index != -1) {
        Py_INCREF(value);  // PyList_SetItem "steals" reference
        // TODO use weakrefs? or explicitly empty list?
#ifndef NDEBUG
        int ret =
#endif
        PyList_SetItem(self->shareables, self->shared_index, value);
        assert(!ret);
    }
}


static PyObject *
Decoder_set_shareable(DecoderObject *self, PyObject *value)
{
    Decoder__set_shareable(self, value);
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


// Major decoders ////////////////////////////////////////////////////////////

static PyObject *
Decoder__decode_uint(DecoderObject *self, uint8_t subtype)
{
    // major type 0
    uint64_t length;
    PyObject *ret;

    if (Decoder__decode_length(self, subtype, &length, NULL) == -1)
        return NULL;
    ret = PyLong_FromUnsignedLongLong(length);
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
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
                if (ret)
                    Decoder__set_shareable(self, ret);
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
    PyObject *ret;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        ret = Decoder__decode_indefinite_bytestrings(self);
    else
        ret = Decoder__decode_definite_bytestring(self, length);
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
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
    PyObject *ret;

    if (Decoder__decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        ret = Decoder__decode_indefinite_strings(self);
    else
        ret = Decoder__decode_definite_string(self, length);
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder__decode_indefinite_array(DecoderObject *self)
{
    PyObject *array, *item, *ret = NULL;

    array = PyList_New(0);
    if (array) {
        ret = array;
        Decoder__set_shareable(self, array);
        while (ret) {
            // XXX Recursion
            item = Decoder_decode_unshared(self);
            if (item == break_marker) {
                Py_DECREF(item);
                break;
            } else if (item) {
                if (PyList_Append(array, item) == -1)
                    ret = NULL;
                Py_DECREF(item);
            } else
                ret = NULL;
        }
        if (ret && self->immutable) {
            ret = PyList_AsTuple(array);
            if (ret) {
                Py_DECREF(array);
                // There's a potential here for an indefinite length recursive
                // array to wind up with a strange representation (the outer
                // being a tuple, the inners all being a list). However, a
                // recursive tuple isn't valid in the first place so it's a bit
                // of a waste of time searching for recursive references just
                // to throw an error
                Decoder__set_shareable(self, ret);
            } else
                ret = NULL;
        }
        if (!ret)
            Py_DECREF(array);
    }
    return ret;
}


static PyObject *
Decoder__decode_definite_array(DecoderObject *self, uint64_t length)
{
    Py_ssize_t i;
    PyObject *array, *item, *ret = NULL;

    if (self->immutable) {
        array = PyTuple_New(length);
        if (array) {
            ret = array;
            for (i = 0; i < length; ++i) {
                // XXX Recursion
                item = Decoder_decode_unshared(self);
                if (item)
                    PyTuple_SET_ITEM(array, i, item);
                else {
                    ret = NULL;
                    break;
                }
            }
        }
        // This is done *after* the construction of the tuple because while
        // it's valid for a tuple object to be shared, it's not valid for it to
        // contain a reference to itself (because a reference to it can't exist
        // during its own construction ... in Python at least; as can be seen
        // above this *is* theoretically possible at the C level).
        if (ret)
            Decoder__set_shareable(self, ret);
    } else {
        array = PyList_New(length);
        if (array) {
            ret = array;
            Decoder__set_shareable(self, array);
            for (i = 0; i < length; ++i) {
                // XXX Recursion
                item = Decoder_decode_unshared(self);
                if (item)
                    PyList_SET_ITEM(array, i, item);
                else {
                    ret = NULL;
                    break;
                }
            }
        }
    }
    if (!ret)
        Py_DECREF(array);
    return ret;
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
    bool indefinite = true;
    PyObject *map, *key, *value, *ret = NULL;

    map = PyDict_New();
    if (map) {
        ret = map;
        Decoder__set_shareable(self, map);
        if (Decoder__decode_length(self, subtype, &length, &indefinite) == 0) {
            if (indefinite) {
                while (ret) {
                    // XXX Recursion
                    key = Decoder_decode_immutable_unshared(self);
                    if (key == break_marker) {
                        Py_DECREF(key);
                        break;
                    } else if (key) {
                        // XXX Recursion
                        value = Decoder_decode_unshared(self);
                        if (value) {
                            if (PyDict_SetItem(map, key, value) == -1)
                                ret = NULL;
                            Py_DECREF(value);
                        } else
                            ret = NULL;
                        Py_DECREF(key);
                    } else
                        ret = NULL;
                }
            } else {
                while (ret && length--) {
                    // XXX Recursion
                    key = Decoder_decode_immutable_unshared(self);
                    if (key) {
                        // XXX Recursion
                        value = Decoder_decode_unshared(self);
                        if (value) {
                            if (PyDict_SetItem(map, key, value) == -1)
                                ret = NULL;
                            Py_DECREF(value);
                        } else
                            ret = NULL;
                        Py_DECREF(key);
                    } else
                        ret = NULL;
                }
            }
        } else
            ret = NULL;
        if (!ret)
            Py_DECREF(map);
    }
    if (ret && self->object_hook != Py_None) {
        map = PyObject_CallFunctionObjArgs(
                self->object_hook, self, ret, NULL);
        if (map) {
            Decoder__set_shareable(self, map);
            Py_DECREF(ret);
            ret = map;
        }
    }
    return ret;
}


// Semantic decoders /////////////////////////////////////////////////////////

static PyObject *
Decoder__decode_semantic(DecoderObject *self, uint8_t subtype)
{
    // major type 6
    uint64_t tagnum;
    PyObject *tag, *value, *ret = NULL;

    if (Decoder__decode_length(self, subtype, &tagnum, NULL) == 0) {
        switch (tagnum) {
            case 0:   ret = Decoder_decode_datestr(self);         break;
            case 1:   ret = Decoder_decode_timestamp(self);       break;
            case 2:   ret = Decoder_decode_positive_bignum(self); break;
            case 3:   ret = Decoder_decode_negative_bignum(self); break;
            case 4:   ret = Decoder_decode_fraction(self);        break;
            case 28:  ret = Decoder_decode_shareable(self);       break;
            case 29:  ret = Decoder_decode_shared(self);          break;
            case 258: ret = Decoder_decode_set(self);             break;
            default:
                tag = Tag_New(tagnum);
                if (tag) {
                    Decoder__set_shareable(self, tag);
                    // XXX Recursive call
                    value = Decoder_decode_unshared(self);
                    if (value) {
                        if (Tag_SetValue(tag, value) == 0) {
                            if (self->tag_hook == Py_None) {
                                Py_INCREF(tag);
                                ret = tag;
                            } else {
                                ret = PyObject_CallFunctionObjArgs(
                                        self->tag_hook, self, tag, NULL);
                                if (ret)
                                    Decoder__set_shareable(self, ret);
                            }
                        }
                        Py_DECREF(value);
                    }
                    Py_DECREF(tag);
                }
                break;
        }
    }
    return ret;
}


static int
Decoder__init_datestr_re(void)
{
    PyObject *re;

    // import re
    re = PyImport_ImportModule("re");
    if (!re)
        goto error;
    // datestr_re = re.compile("long-date-time-regex...")
    _CBOAR_datestr_re = PyObject_CallMethodObjArgs(
            re, _CBOAR_str_compile, _CBOAR_str_datestr_re, NULL);
    Py_DECREF(re);
    if (!_CBOAR_datestr_re)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError,
            "unable to import re and compile a regex");
    return -1;
}


static int
Decoder__init_timezone_utc(void)
{
    PyObject *datetime;

#if PY_MAJOR_VERSION > 3 || PY_MINOR_VERSION >= 7
    Py_INCREF(PyDateTime_TimeZone_UTC);
    _CBOAR_timezone_utc = PyDateTime_TimeZone_UTC;
    _CBOAR_timezone = NULL;
#else
    // from datetime import timezone
    datetime = PyImport_ImportModule("datetime");
    if (!datetime)
        goto error;
    _CBOAR_timezone = PyObject_GetAttr(datetime, _CBOAR_str_timezone);
    Py_DECREF(datetime);
    if (!_CBOAR_timezone)
        goto error;
    // utc = timezone.utc
    _CBOAR_timezone_utc = PyObject_GetAttr(_CBOAR_timezone, _CBOAR_str_utc);
    if (!_CBOAR_timezone_utc)
        goto error;
#endif
    return 0;
error:
    PyErr_SetString(PyExc_ImportError,
            "unable to import datetime and/or timezone");
    return -1;
}


static PyObject *
Decoder__parse_datestr(DecoderObject *self, PyObject *str)
{
    char *buf, *p;
    Py_ssize_t size;
    PyObject *tz, *delta, *ret = NULL;
    bool offset_sign;
    uint16_t Y;
    uint8_t m, d, H, M, S, offset_H, offset_M;
    uint32_t uS;

    if (!_CBOAR_timezone_utc && Decoder__init_timezone_utc() == -1)
        return NULL;
    buf = PyUnicode_AsUTF8AndSize(str, &size);
    if (buf) {
        Y = strtol(buf, NULL, 10);
        m = strtol(buf + 5, NULL, 10);
        d = strtol(buf + 8, NULL, 10);
        H = strtol(buf + 11, NULL, 10);
        M = strtol(buf + 14, NULL, 10);
        S = strtol(buf + 17, &p, 10);
        if (*p == '.') {
            uS = strtol(buf + 20, &p, 10);
            switch (p - (buf + 20)) {
                case 1: uS *= 100000; break;
                case 2: uS *= 10000; break;
                case 3: uS *= 1000; break;
                case 4: uS *= 100; break;
                case 5: uS *= 10; break;
            }
        } else
            uS = 0;
        if (*p == 'Z') {
            offset_sign = false;
            Py_INCREF(_CBOAR_timezone_utc);
            tz = _CBOAR_timezone_utc;
        } else {
            offset_sign = *p == '-';
            offset_H = strtol(p, &p, 10);
            offset_M = strtol(p + 1, &p, 10);
            delta = PyDelta_FromDSU(0,
                    (offset_sign ? -1 : 1) *
                    (offset_H * 3600 + offset_M * 60), 0);
            if (delta) {
#if PY_MAJOR_VERSION > 3 || PY_MINOR_VERSION >= 7
                tz = PyTimeZone_FromOffset(delta);
#else
                tz = PyObject_CallFunctionObjArgs(
                        _CBOAR_timezone, delta, NULL);
#endif
                Py_DECREF(delta);
            } else {
                tz = NULL;
            }
        }
        if (tz) {
            ret = PyDateTimeAPI->DateTime_FromDateAndTime(
                    Y, m, d, H, M, S, uS, tz, PyDateTimeAPI->DateTimeType);
            Py_DECREF(tz);
        }
    }
    return ret;
}


static PyObject *
Decoder_decode_datestr(DecoderObject *self)
{
    // semantic type 0
    PyObject *match, *str, *ret = NULL;

    if (!_CBOAR_datestr_re && Decoder__init_datestr_re() == -1)
        return NULL;
    // XXX Recursive call
    str = Decoder_decode(self);
    if (str) {
        if (PyUnicode_Check(str)) {
            match = PyObject_CallMethodObjArgs(
                    _CBOAR_datestr_re, _CBOAR_str_match, str, NULL);
            if (match) {
                if (match != Py_None)
                    ret = Decoder__parse_datestr(self, str);
                else
                    PyErr_Format(PyExc_ValueError,
                            "invalid datetime string %R", str);
                Py_DECREF(match);
            }
        } else
            PyErr_Format(PyExc_ValueError, "invalid datetime value %R", str);
        Py_DECREF(str);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_timestamp(DecoderObject *self)
{
    // semantic type 1
    PyObject *num, *tuple, *ret = NULL;

    if (!_CBOAR_timezone_utc && Decoder__init_timezone_utc() == -1)
        return NULL;
    // XXX Recursive call
    num = Decoder_decode(self);
    if (num) {
        if (PyNumber_Check(num)) {
            tuple = PyTuple_Pack(2, num, _CBOAR_timezone_utc);
            if (tuple) {
                ret = PyDateTime_FromTimestamp(tuple);
                Py_DECREF(tuple);
            }
        } else {
            PyErr_Format(PyExc_ValueError, "invalid timestamp value %R", num);
        }
        Py_DECREF(num);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_positive_bignum(DecoderObject *self)
{
    // semantic type 2
    PyObject *bytes, *ret = NULL;

    // XXX Recursive call
    bytes = Decoder_decode(self);
    if (bytes) {
        if (PyBytes_CheckExact(bytes))
            ret = PyObject_CallMethod(
                (PyObject*) &PyLong_Type, "from_bytes", "Os#", bytes, "big", 3);
        else
            PyErr_Format(PyExc_ValueError, "invalid bignum value %R", bytes);
        Py_DECREF(bytes);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_negative_bignum(DecoderObject *self)
{
    // semantic type 3
    PyObject *value, *one, *neg, *ret = NULL;

    value = Decoder_decode_positive_bignum(self);
    if (value) {
        one = PyLong_FromLong(1);
        if (one) {
            neg = PyNumber_Negative(value);
            if (neg) {
                ret = PyNumber_Subtract(neg, one);
                Py_DECREF(neg);
            }
            Py_DECREF(one);
        }
        Py_DECREF(value);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_fraction(DecoderObject *self)
{
    // semantic type 4
    PyObject *ten, *ret = NULL;

    // TODO
    return ret;
}


static PyObject *
Decoder_decode_shareable(DecoderObject *self)
{
    // semantic type 28
    int32_t old_index;
    PyObject *ret = NULL;

    old_index = self->shared_index;
    self->shared_index = PyList_GET_SIZE(self->shareables);
    if (PyList_Append(self->shareables, Py_None) == 0)
        // XXX Recursive call
        ret = Decoder_decode(self);
    self->shared_index = old_index;
    return ret;
}


static PyObject *
Decoder_decode_shared(DecoderObject *self)
{
    // semantic type 29
    PyObject *index, *ret = NULL;

    index = Decoder_decode_unshared(self);
    if (index) {
        if (PyLong_CheckExact(index)) {
            ret = PyList_GetItem(self->shareables, PyLong_AsSsize_t(index));
            if (ret) {
                if (ret == Py_None) {
                    PyErr_Format(PyExc_ValueError,
                            "shared value %R has not been initialized", index);
                    ret = NULL;
                } else {
                    // convert borrowed reference to new reference
                    Py_INCREF(ret);
                }
            } else {
                PyErr_Format(PyExc_ValueError,
                        "shared reference %R not found", index);
            }
        } else {
            PyErr_Format(PyExc_ValueError,
                    "invalid shared reference %R", index);
        }
        Py_DECREF(index);
    }
    return ret;
}


static PyObject *
Decoder_decode_set(DecoderObject *self)
{
    // semantic type 258
    PyObject *array, *ret = NULL;

    // XXX Recursive call
    array = Decoder_decode_immutable(self);
    if (array) {
        if (PyList_CheckExact(array) || PyTuple_CheckExact(array)) {
            if (self->immutable)
                ret = PyFrozenSet_New(array);
            else
                ret = PySet_New(array);
        } else
            PyErr_Format(PyExc_ValueError, "invalid set array %R", array);
        Py_DECREF(array);
    }
    // This can be done after construction of the set/frozenset because,
    // unlike lists/dicts a set cannot contain a reference to itself (a set
    // is unhashable). Nor can a frozenset contain a reference to itself
    // because it can't refer to itself during its own construction.
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


// Special decoders //////////////////////////////////////////////////////////

static PyObject *
Decoder__decode_special(DecoderObject *self, uint8_t subtype)
{
    // major type 7
    PyObject *tag, *ret = NULL;

    if ((subtype) < 20) {
        tag = PyStructSequence_New(&CBORSimpleValueType);
        if (tag) {
            PyStructSequence_SET_ITEM(tag, 0, PyLong_FromLong(subtype));
            if (PyStructSequence_GET_ITEM(tag, 0)) {
                Py_INCREF(tag);
                ret = tag;
            }
            Py_DECREF(tag);
            // XXX Set shareable?
        }
    } else {
        switch (subtype) {
            case 20: Py_RETURN_FALSE;
            case 21: Py_RETURN_TRUE;
            case 22: Py_RETURN_NONE;
            case 23: CBOAR_RETURN_UNDEFINED;
            case 24: ret = Decoder_decode_simplevalue(self); break;
            case 25: ret = Decoder_decode_float16(self);     break;
            case 26: ret = Decoder_decode_float32(self);     break;
            case 27: ret = Decoder_decode_float64(self);     break;
            case 31: CBOAR_RETURN_BREAK;
            default:
                // XXX Raise exception?
                break;
        }
    }
    return ret;
}


static PyObject *
Decoder_decode_simplevalue(DecoderObject *self)
{
    PyObject *tag, *ret = NULL;
    uint8_t buf;

    if (Decoder__read(self, (char*)&buf, sizeof(uint8_t)) == 0) {
        tag = PyStructSequence_New(&CBORSimpleValueType);
        if (tag) {
            PyStructSequence_SET_ITEM(tag, 0, PyLong_FromLong(buf));
            if (PyStructSequence_GET_ITEM(tag, 0)) {
                Py_INCREF(tag);
                ret = tag;
            }
            Py_DECREF(tag);
        }
    }
    // XXX Set shareable?
    return ret;
}


static PyObject *
Decoder_decode_float16(DecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint16_t i;
        char buf[sizeof(uint16_t)];
    } u;

    if (Decoder__read(self, u.buf, sizeof(uint16_t)) == 0)
        ret = PyFloat_FromDouble(read_float16(u.i));
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_float32(DecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint32_t i;
        float f;
        char buf[sizeof(float)];
    } u;

    if (Decoder__read(self, u.buf, sizeof(float)) == 0) {
        u.i = be32toh(u.i);
        ret = PyFloat_FromDouble((double)u.f);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


static PyObject *
Decoder_decode_float64(DecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint64_t i;
        double f;
        char buf[sizeof(double)];
    } u;

    if (Decoder__read(self, u.buf, sizeof(double)) == 0) {
        u.i = be64toh(u.i);
        ret = PyFloat_FromDouble(u.f);
    }
    if (ret)
        Decoder__set_shareable(self, ret);
    return ret;
}


// Decoder.decode(self) -> obj
static PyObject *
Decoder_decode(DecoderObject *self)
{
    LeadByte lead;
    PyObject *ret = NULL;

    if (Decoder__read(self, &lead.byte, 1) == 0) {
        switch (lead.major) {
            case 0: ret = Decoder__decode_uint(self, lead.subtype);       break;
            case 1: ret = Decoder__decode_negint(self, lead.subtype);     break;
            case 2: ret = Decoder__decode_bytestring(self, lead.subtype); break;
            case 3: ret = Decoder__decode_string(self, lead.subtype);     break;
            case 4: ret = Decoder__decode_array(self, lead.subtype);      break;
            case 5: ret = Decoder__decode_map(self, lead.subtype);        break;
            case 6: ret = Decoder__decode_semantic(self, lead.subtype);   break;
            case 7: ret = Decoder__decode_special(self, lead.subtype);    break;
            default: assert(0);
        }
    }
    return ret;
}


static PyObject *
Decoder_decode_unshared(DecoderObject *self)
{
    int32_t old_index;
    PyObject *ret;

    old_index = self->shared_index;
    self->shared_index = -1;
    ret = Decoder_decode(self);
    self->shared_index = old_index;
    return ret;
}


static PyObject *
Decoder_decode_immutable(DecoderObject *self)
{
    bool old_immutable;
    PyObject *ret;

    old_immutable = self->immutable;
    self->immutable = true;
    ret = Decoder_decode(self);
    self->immutable = old_immutable;
    return ret;
}


static PyObject *
Decoder_decode_immutable_unshared(DecoderObject *self)
{
    bool old_immutable;
    int32_t old_index;
    PyObject *ret;

    old_immutable = self->immutable;
    old_index = self->shared_index;
    self->immutable = true;
    self->shared_index = -1;
    ret = Decoder_decode(self);
    self->immutable = old_immutable;
    self->shared_index = old_index;
    return ret;
}


// Decoder class definition //////////////////////////////////////////////////

#define PUBLIC_MAJOR(type)                                                   \
    static PyObject *                                                        \
    Decoder_decode_##type(DecoderObject *self, PyObject *subtype)            \
    {                                                                        \
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

#undef PUBLIC_MAJOR

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
    {"decode_datestr", (PyCFunction) Decoder_decode_datestr, METH_NOARGS,
        "decode a date-time string from the input"},
    {"decode_timestamp", (PyCFunction) Decoder_decode_timestamp, METH_NOARGS,
        "decode a timestamp offset from the input"},
    {"decode_positive_bignum", (PyCFunction) Decoder_decode_positive_bignum, METH_NOARGS,
        "decode a positive big-integer from the input"},
    {"decode_negative_bignum", (PyCFunction) Decoder_decode_negative_bignum, METH_NOARGS,
        "decode a negative big-integer from the input"},
    {"decode_shareable", (PyCFunction) Decoder_decode_shareable, METH_NOARGS,
        "decode a shareable value from the input"},
    {"decode_shared", (PyCFunction) Decoder_decode_shared, METH_NOARGS,
        "decode a shared reference from the input"},
    {"decode_set", (PyCFunction) Decoder_decode_set, METH_NOARGS,
        "decode a set or frozenset from the input"},
    {"decode_simplevalue", (PyCFunction) Decoder_decode_simplevalue, METH_NOARGS,
        "decode a CBORSimpleValue from the input"},
    {"decode_float16", (PyCFunction) Decoder_decode_float16, METH_NOARGS,
        "decode a half-precision floating-point value from the input"},
    {"decode_float32", (PyCFunction) Decoder_decode_float32, METH_NOARGS,
        "decode a floating-point value from the input"},
    {"decode_float64", (PyCFunction) Decoder_decode_float64, METH_NOARGS,
        "decode a double-precision floating-point value from the input"},
    {"set_shareable", (PyCFunction) Decoder_set_shareable, METH_O,
        "set the specified object as the current shareable reference"},
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
