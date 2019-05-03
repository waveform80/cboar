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


static int _CBORDecoder_set_fp(CBORDecoderObject *, PyObject *, void *);
static int _CBORDecoder_set_tag_hook(CBORDecoderObject *, PyObject *, void *);
static int _CBORDecoder_set_object_hook(CBORDecoderObject *, PyObject *, void *);
static int _CBORDecoder_set_str_errors(CBORDecoderObject *, PyObject *, void *);

static PyObject * decode_bytestring(CBORDecoderObject *, uint8_t);
static PyObject * decode_string(CBORDecoderObject *, uint8_t);
static PyObject * CBORDecoder_decode_datestr(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_timestamp(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_fraction(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_bigfloat(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_rational(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_regexp(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_uuid(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_mime(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_positive_bignum(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_negative_bignum(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_simplevalue(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_float16(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_float32(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_float64(CBORDecoderObject *);

static PyObject * CBORDecoder_decode_shareable(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_shared(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_set(CBORDecoderObject *);
static PyObject * CBORDecoder_decode(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_immutable(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_unshared(CBORDecoderObject *);
static PyObject * CBORDecoder_decode_immutable_unshared(CBORDecoderObject *);

// TODO Docstrings


// Constructors and destructors //////////////////////////////////////////////

static int
CBORDecoder_traverse(CBORDecoderObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->read);
    Py_VISIT(self->tag_hook);
    Py_VISIT(self->object_hook);
    Py_VISIT(self->shareables);
    // No need to visit str_errors; it's only a string and can't reference us
    // or other objects
    return 0;
}

static int
CBORDecoder_clear(CBORDecoderObject *self)
{
    Py_CLEAR(self->read);
    Py_CLEAR(self->tag_hook);
    Py_CLEAR(self->object_hook);
    Py_CLEAR(self->shareables);
    Py_CLEAR(self->str_errors);
    return 0;
}


// CBORDecoder.__del__(self)
static void
CBORDecoder_dealloc(CBORDecoderObject *self)
{
    PyObject_GC_UnTrack(self);
    CBORDecoder_clear(self);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


// CBORDecoder.__new__(cls, *args, **kwargs)
static PyObject *
CBORDecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    CBORDecoderObject *self;

    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
        return NULL;

    self = (CBORDecoderObject *) type->tp_alloc(type, 0);
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


// CBORDecoder.__init__(self, fp=None, tag_hook=None, object_hook=None)
static int
CBORDecoder_init(CBORDecoderObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {
        "fp", "tag_hook", "object_hook", "str_errors", NULL
    };
    PyObject *fp = NULL, *tag_hook = NULL, *object_hook = NULL,
             *str_errors = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OOO", keywords,
                &fp, &tag_hook, &object_hook, &str_errors))
        return -1;

    if (_CBORDecoder_set_fp(self, fp, NULL) == -1)
        return -1;
    if (tag_hook && _CBORDecoder_set_tag_hook(self, tag_hook, NULL) == -1)
        return -1;
    if (object_hook && _CBORDecoder_set_object_hook(self, object_hook, NULL) == -1)
        return -1;
    if (str_errors && _CBORDecoder_set_str_errors(self, str_errors, NULL) == -1)
        return -1;

    return 0;
}


// Property accessors ////////////////////////////////////////////////////////

// CBORDecoder._get_fp(self)
static PyObject *
_CBORDecoder_get_fp(CBORDecoderObject *self, void *closure)
{
    PyObject *ret = PyMethod_GET_SELF(self->read);
    Py_INCREF(ret);
    return ret;
}


// CBORDecoder._set_fp(self, value)
static int
_CBORDecoder_set_fp(CBORDecoderObject *self, PyObject *value, void *closure)
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

    // See notes in encoder.c / _CBOREncoder_set_fp
    tmp = self->read;
    self->read = read;
    Py_DECREF(tmp);
    return 0;
}


// CBORDecoder._get_tag_hook(self)
static PyObject *
_CBORDecoder_get_tag_hook(CBORDecoderObject *self, void *closure)
{
    Py_INCREF(self->tag_hook);
    return self->tag_hook;
}


// CBORDecoder._set_tag_hook(self, value)
static int
_CBORDecoder_set_tag_hook(CBORDecoderObject *self, PyObject *value,
                          void *closure)
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


// CBORDecoder._get_object_hook(self)
static PyObject *
_CBORDecoder_get_object_hook(CBORDecoderObject *self, void *closure)
{
    Py_INCREF(self->object_hook);
    return self->object_hook;
}


// CBORDecoder._set_object_hook(self, value)
static int
_CBORDecoder_set_object_hook(CBORDecoderObject *self, PyObject *value,
                             void *closure)
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


// CBORDecoder._get_str_errors(self)
static PyObject *
_CBORDecoder_get_str_errors(CBORDecoderObject *self, void *closure)
{
    return PyUnicode_DecodeASCII(
            PyBytes_AS_STRING(self->str_errors),
            PyBytes_GET_SIZE(self->str_errors), "strict");
}


// CBORDecoder._set_str_errors(self, value)
static int
_CBORDecoder_set_str_errors(CBORDecoderObject *self, PyObject *value,
                            void *closure)
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


// Utility functions /////////////////////////////////////////////////////////

static int
fp_read(CBORDecoderObject *self, char *buf, const uint64_t size)
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


// CBORDecoder.read(self, length) -> bytes
static PyObject *
CBORDecoder_read(CBORDecoderObject *self, PyObject *length)
{
    PyObject *ret = NULL;
    Py_ssize_t len;

    if (!PyLong_Check(length)) {
        PyErr_SetString(PyExc_ValueError, "expected integer length to read");
        return NULL;
    }
    len = PyLong_AsSsize_t(length);
    if (PyErr_Occurred())
        return NULL;
    ret = PyBytes_FromStringAndSize(NULL, len);
    if (ret) {
        if (fp_read(self, PyBytes_AS_STRING(ret), len) == -1) {
            Py_DECREF(ret);
            ret = NULL;
        }
    }
    return ret;
}


static inline void
set_shareable(CBORDecoderObject *self, PyObject *value)
{
    if (value && self->shared_index != -1) {
        Py_INCREF(value);  // PyList_SetItem "steals" reference
        // TODO use weakrefs? or explicitly empty list?
#ifndef NDEBUG
        int ret =
#endif
        PyList_SetItem(self->shareables, self->shared_index, value);
        assert(!ret);
    }
}


// CBORDecoder.set_shareable(self, value)
static PyObject *
CBORDecoder_set_shareable(CBORDecoderObject *self, PyObject *value)
{
    set_shareable(self, value);
    Py_RETURN_NONE;
}


static int
decode_length(CBORDecoderObject *self, uint8_t subtype,
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
            if (fp_read(self, value.u8.buf, sizeof(uint8_t)) == -1)
                return -1;
            *length = value.u8.value;
        } else if (subtype == 25) {
            if (fp_read(self, value.u16.buf, sizeof(uint16_t)) == -1)
                return -1;
            *length = be16toh(value.u16.value);
        } else if (subtype == 26) {
            if (fp_read(self, value.u32.buf, sizeof(uint32_t)) == -1)
                return -1;
            *length = be32toh(value.u32.value);
        } else {
            if (fp_read(self, value.u64.buf, sizeof(uint64_t)) == -1)
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
decode_uint(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 0
    uint64_t length;
    PyObject *ret;

    if (decode_length(self, subtype, &length, NULL) == -1)
        return NULL;
    ret = PyLong_FromUnsignedLongLong(length);
    set_shareable(self, ret);
    return ret;
}


static PyObject *
decode_negint(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 1
    PyObject *value, *one, *ret = NULL;

    value = decode_uint(self, subtype);
    if (value) {
        one = PyLong_FromLong(1);
        if (one) {
            ret = PyNumber_Negative(value);
            if (ret) {
                Py_DECREF(value);
                value = ret;
                ret = PyNumber_Subtract(value, one);
                set_shareable(self, ret);
            }
            Py_DECREF(one);
        }
        Py_DECREF(value);
    }
    return ret;
}


static PyObject *
decode_definite_bytestring(CBORDecoderObject *self, uint64_t length)
{
    PyObject *ret = NULL;

    if (length > PY_SSIZE_T_MAX)
        return NULL;
    ret = PyBytes_FromStringAndSize(NULL, length);
    if (!ret)
        return NULL;
    if (fp_read(self, PyBytes_AS_STRING(ret), length) == -1) {
        Py_DECREF(ret);
        return NULL;
    }
    return ret;
}


static PyObject *
decode_indefinite_bytestrings(CBORDecoderObject *self)
{
    PyObject *list, *ret = NULL;
    LeadByte lead;

    list = PyList_New(0);
    if (list) {
        while (1) {
            if (fp_read(self, &lead.byte, 1) == -1)
                break;
            if (lead.major == 2) {
                ret = decode_bytestring(self, lead.subtype);
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
decode_bytestring(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 2
    uint64_t length;
    bool indefinite = true;
    PyObject *ret;

    if (decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        ret = decode_indefinite_bytestrings(self);
    else
        ret = decode_definite_bytestring(self, length);
    set_shareable(self, ret);
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
decode_definite_string(CBORDecoderObject *self, uint64_t length)
{
    PyObject *ret = NULL;
    char *buf;

    if (length > PY_SSIZE_T_MAX)
        return NULL;
    buf = PyMem_Malloc(length);
    if (!buf)
        return PyErr_NoMemory();

    if (fp_read(self, buf, length) == 0)
        ret = PyUnicode_DecodeUTF8(
                buf, length, PyBytes_AS_STRING(self->str_errors));
    PyMem_Free(buf);
    return ret;
}


static PyObject *
decode_indefinite_strings(CBORDecoderObject *self)
{
    PyObject *list, *ret = NULL;
    LeadByte lead;

    list = PyList_New(0);
    if (list) {
        while (1) {
            if (fp_read(self, &lead.byte, 1) == -1)
                break;
            if (lead.major == 3) {
                ret = decode_string(self, lead.subtype);
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
decode_string(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 3
    uint64_t length;
    bool indefinite = true;
    PyObject *ret;

    if (decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        ret = decode_indefinite_strings(self);
    else
        ret = decode_definite_string(self, length);
    set_shareable(self, ret);
    return ret;
}


static PyObject *
decode_indefinite_array(CBORDecoderObject *self)
{
    PyObject *array, *item, *ret = NULL;

    array = PyList_New(0);
    if (array) {
        ret = array;
        set_shareable(self, array);
        while (ret) {
            item = CBORDecoder_decode_unshared(self);
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
                set_shareable(self, ret);
            } else
                ret = NULL;
        }
        if (!ret)
            Py_DECREF(array);
    }
    return ret;
}


static PyObject *
decode_definite_array(CBORDecoderObject *self, uint64_t length)
{
    Py_ssize_t i;
    PyObject *array, *item, *ret = NULL;

    if (self->immutable) {
        array = PyTuple_New(length);
        if (array) {
            ret = array;
            for (i = 0; i < length; ++i) {
                item = CBORDecoder_decode_unshared(self);
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
        set_shareable(self, ret);
    } else {
        array = PyList_New(length);
        if (array) {
            ret = array;
            set_shareable(self, array);
            for (i = 0; i < length; ++i) {
                item = CBORDecoder_decode_unshared(self);
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
decode_array(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 4
    uint64_t length;
    bool indefinite = true;

    if (decode_length(self, subtype, &length, &indefinite) == -1)
        return NULL;
    if (indefinite)
        return decode_indefinite_array(self);
    else
        return decode_definite_array(self, length);
}


static PyObject *
decode_map(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 5
    uint64_t length;
    bool indefinite = true;
    PyObject *map, *key, *value, *ret = NULL;

    map = PyDict_New();
    if (map) {
        ret = map;
        set_shareable(self, map);
        if (decode_length(self, subtype, &length, &indefinite) == 0) {
            if (indefinite) {
                while (ret) {
                    key = CBORDecoder_decode_immutable_unshared(self);
                    if (key == break_marker) {
                        Py_DECREF(key);
                        break;
                    } else if (key) {
                        value = CBORDecoder_decode_unshared(self);
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
                    key = CBORDecoder_decode_immutable_unshared(self);
                    if (key) {
                        value = CBORDecoder_decode_unshared(self);
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
            set_shareable(self, map);
            Py_DECREF(ret);
            ret = map;
        }
    }
    return ret;
}


// Semantic decoders /////////////////////////////////////////////////////////

static PyObject *
decode_semantic(CBORDecoderObject *self, uint8_t subtype)
{
    // major type 6
    uint64_t tagnum;
    PyObject *tag, *value, *ret = NULL;

    if (decode_length(self, subtype, &tagnum, NULL) == 0) {
        switch (tagnum) {
            case 0:   ret = CBORDecoder_decode_datestr(self);         break;
            case 1:   ret = CBORDecoder_decode_timestamp(self);       break;
            case 2:   ret = CBORDecoder_decode_positive_bignum(self); break;
            case 3:   ret = CBORDecoder_decode_negative_bignum(self); break;
            case 4:   ret = CBORDecoder_decode_fraction(self);        break;
            case 5:   ret = CBORDecoder_decode_bigfloat(self);        break;
            case 28:  ret = CBORDecoder_decode_shareable(self);       break;
            case 29:  ret = CBORDecoder_decode_shared(self);          break;
            case 30:  ret = CBORDecoder_decode_rational(self);        break;
            case 35:  ret = CBORDecoder_decode_regexp(self);          break;
            case 36:  ret = CBORDecoder_decode_mime(self);            break;
            case 37:  ret = CBORDecoder_decode_uuid(self);            break;
            case 258: ret = CBORDecoder_decode_set(self);             break;
            default:
                tag = CBORTag_New(tagnum);
                if (tag) {
                    set_shareable(self, tag);
                    value = CBORDecoder_decode_unshared(self);
                    if (value) {
                        if (CBORTag_SetValue(tag, value) == 0) {
                            if (self->tag_hook == Py_None) {
                                Py_INCREF(tag);
                                ret = tag;
                            } else {
                                ret = PyObject_CallFunctionObjArgs(
                                        self->tag_hook, self, tag, NULL);
                                set_shareable(self, ret);
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


static PyObject *
parse_datestr(CBORDecoderObject *self, PyObject *str)
{
    char *buf, *p;
    Py_ssize_t size;
    PyObject *tz, *delta, *ret = NULL;
    bool offset_sign;
    uint16_t Y;
    uint8_t m, d, H, M, S, offset_H, offset_M;
    uint32_t uS;

    if (!_CBOAR_timezone_utc && _CBOAR_init_timezone_utc() == -1)
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
#if PY_VERSION_HEX >= 0x03070000
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


// CBORDecoder.decode_datestr(self)
static PyObject *
CBORDecoder_decode_datestr(CBORDecoderObject *self)
{
    // semantic type 0
    PyObject *match, *str, *ret = NULL;

    if (!_CBOAR_datestr_re && _CBOAR_init_re_compile() == -1)
        return NULL;
    str = CBORDecoder_decode(self);
    if (str) {
        if (PyUnicode_Check(str)) {
            match = PyObject_CallMethodObjArgs(
                    _CBOAR_datestr_re, _CBOAR_str_match, str, NULL);
            if (match) {
                if (match != Py_None)
                    ret = parse_datestr(self, str);
                else
                    PyErr_Format(PyExc_ValueError,
                            "invalid datetime string %R", str);
                Py_DECREF(match);
            }
        } else
            PyErr_Format(PyExc_ValueError, "invalid datetime value %R", str);
        Py_DECREF(str);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_timestamp(self)
static PyObject *
CBORDecoder_decode_timestamp(CBORDecoderObject *self)
{
    // semantic type 1
    PyObject *num, *tuple, *ret = NULL;

    if (!_CBOAR_timezone_utc && _CBOAR_init_timezone_utc() == -1)
        return NULL;
    num = CBORDecoder_decode(self);
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
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_positive_bignum(self)
static PyObject *
CBORDecoder_decode_positive_bignum(CBORDecoderObject *self)
{
    // semantic type 2
    PyObject *bytes, *ret = NULL;

    bytes = CBORDecoder_decode(self);
    if (bytes) {
        if (PyBytes_CheckExact(bytes))
            ret = PyObject_CallMethod(
                (PyObject*) &PyLong_Type, "from_bytes", "Os#", bytes, "big", 3);
        else
            PyErr_Format(PyExc_ValueError, "invalid bignum value %R", bytes);
        Py_DECREF(bytes);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_negative_bignum(self)
static PyObject *
CBORDecoder_decode_negative_bignum(CBORDecoderObject *self)
{
    // semantic type 3
    PyObject *value, *one, *neg, *ret = NULL;

    value = CBORDecoder_decode_positive_bignum(self);
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
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_fraction(self)
static PyObject *
CBORDecoder_decode_fraction(CBORDecoderObject *self)
{
    // semantic type 4
    PyObject *tuple, *tmp, *sig, *exp, *ten, *ret = NULL;

    if (!_CBOAR_Decimal && _CBOAR_init_Decimal() == -1)
        return NULL;
    // NOTE: There's no particular necessity for this to be immutable, it's
    // just a performance choice
    tuple = CBORDecoder_decode_immutable_unshared(self);
    if (tuple) {
        if (PyTuple_CheckExact(tuple) && PyTuple_GET_SIZE(tuple) == 2) {
            exp = PyTuple_GET_ITEM(tuple, 0);
            sig = PyTuple_GET_ITEM(tuple, 1);
            ten = PyObject_CallFunction(_CBOAR_Decimal, "i", 10);
            if (ten) {
                tmp = PyNumber_Power(ten, exp, Py_None);
                if (tmp) {
                    ret = PyNumber_Multiply(sig, tmp);
                    Py_DECREF(tmp);
                }
                Py_DECREF(ten);
            }
        }
        Py_DECREF(tuple);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_bigfloat
static PyObject *
CBORDecoder_decode_bigfloat(CBORDecoderObject *self)
{
    // semantic type 5
    PyObject *tuple, *tmp, *sig, *exp, *two, *ret = NULL;

    if (!_CBOAR_Decimal && _CBOAR_init_Decimal() == -1)
        return NULL;
    // NOTE: see semantic type 4
    tuple = CBORDecoder_decode_immutable_unshared(self);
    if (tuple) {
        if (PyTuple_CheckExact(tuple) && PyTuple_GET_SIZE(tuple) == 2) {
            exp = PyTuple_GET_ITEM(tuple, 0);
            sig = PyTuple_GET_ITEM(tuple, 1);
            two = PyObject_CallFunction(_CBOAR_Decimal, "i", 2);
            if (two) {
                tmp = PyNumber_Power(two, exp, Py_None);
                if (tmp) {
                    ret = PyNumber_Multiply(sig, tmp);
                    Py_DECREF(tmp);
                }
                Py_DECREF(two);
            }
        }
        Py_DECREF(tuple);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_shareable(self)
static PyObject *
CBORDecoder_decode_shareable(CBORDecoderObject *self)
{
    // semantic type 28
    int32_t old_index;
    PyObject *ret = NULL;

    old_index = self->shared_index;
    self->shared_index = PyList_GET_SIZE(self->shareables);
    if (PyList_Append(self->shareables, Py_None) == 0)
        ret = CBORDecoder_decode(self);
    self->shared_index = old_index;
    return ret;
}


// CBORDecoder.decode_shared(self)
static PyObject *
CBORDecoder_decode_shared(CBORDecoderObject *self)
{
    // semantic type 29
    PyObject *index, *ret = NULL;

    index = CBORDecoder_decode_unshared(self);
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


// CBORDecoder.decode_rational(self)
static PyObject *
CBORDecoder_decode_rational(CBORDecoderObject *self)
{
    // semantic type 30
    PyObject *tuple, *ret = NULL;

    if (!_CBOAR_Fraction && _CBOAR_init_Fraction() == -1)
        return NULL;
    // NOTE: see semantic type 4
    tuple = CBORDecoder_decode_immutable_unshared(self);
    if (tuple) {
        if (PyTuple_CheckExact(tuple) && PyTuple_GET_SIZE(tuple) == 2) {
            ret = PyObject_CallFunctionObjArgs(
                    _CBOAR_Fraction,
                    PyTuple_GET_ITEM(tuple, 0),
                    PyTuple_GET_ITEM(tuple, 1),
                    NULL);
        }
        Py_DECREF(tuple);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_regexp(self)
static PyObject *
CBORDecoder_decode_regexp(CBORDecoderObject *self)
{
    // semantic type 35
    PyObject *pattern, *ret = NULL;

    if (!_CBOAR_re_compile && _CBOAR_init_re_compile() == -1)
        return NULL;
    // NOTE: see semantic type 4
    pattern = CBORDecoder_decode_immutable_unshared(self);
    if (pattern) {
        ret = PyObject_CallFunctionObjArgs(_CBOAR_re_compile, pattern, NULL);
        Py_DECREF(pattern);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_mime(self)
static PyObject *
CBORDecoder_decode_mime(CBORDecoderObject *self)
{
    // semantic type 36
    PyObject *value, *parser, *ret = NULL;

    if (!_CBOAR_Parser && _CBOAR_init_Parser() == -1)
        return NULL;
    // NOTE: see semantic type 4
    value = CBORDecoder_decode_immutable_unshared(self);
    if (value) {
        parser = PyObject_CallFunctionObjArgs(_CBOAR_Parser, NULL);
        if (parser) {
            ret = PyObject_CallMethodObjArgs(parser,
                    _CBOAR_str_parsestr, value, NULL);
            Py_DECREF(parser);
        }
        Py_DECREF(value);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_uuid(self)
static PyObject *
CBORDecoder_decode_uuid(CBORDecoderObject *self)
{
    // semantic type 37
    PyObject *bytes, *ret = NULL;

    if (!_CBOAR_UUID && _CBOAR_init_UUID() == -1)
        return NULL;
    // NOTE: see semantic type 4
    bytes = CBORDecoder_decode_immutable_unshared(self);
    if (bytes) {
        ret = PyObject_CallFunctionObjArgs(_CBOAR_UUID, Py_None, bytes, NULL);
        Py_DECREF(bytes);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_set(self)
static PyObject *
CBORDecoder_decode_set(CBORDecoderObject *self)
{
    // semantic type 258
    PyObject *array, *ret = NULL;

    array = CBORDecoder_decode_immutable(self);
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
    set_shareable(self, ret);
    return ret;
}


// Special decoders //////////////////////////////////////////////////////////

static PyObject *
decode_special(CBORDecoderObject *self, uint8_t subtype)
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
            case 24: return CBORDecoder_decode_simplevalue(self);
            case 25: return CBORDecoder_decode_float16(self);
            case 26: return CBORDecoder_decode_float32(self);
            case 27: return CBORDecoder_decode_float64(self);
            case 31: CBOAR_RETURN_BREAK;
            default:
                // XXX Raise exception?
                break;
        }
    }
    return ret;
}


// CBORDecoder.decode_simplevalue(self)
static PyObject *
CBORDecoder_decode_simplevalue(CBORDecoderObject *self)
{
    PyObject *tag, *ret = NULL;
    uint8_t buf;

    if (fp_read(self, (char*)&buf, sizeof(uint8_t)) == 0) {
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


// CBORDecoder.decode_float16(self)
static PyObject *
CBORDecoder_decode_float16(CBORDecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint16_t i;
        char buf[sizeof(uint16_t)];
    } u;

    if (fp_read(self, u.buf, sizeof(uint16_t)) == 0)
        ret = PyFloat_FromDouble(unpack_float16(u.i));
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_float32(self)
static PyObject *
CBORDecoder_decode_float32(CBORDecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint32_t i;
        float f;
        char buf[sizeof(float)];
    } u;

    if (fp_read(self, u.buf, sizeof(float)) == 0) {
        u.i = be32toh(u.i);
        ret = PyFloat_FromDouble(u.f);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode_float64(self)
static PyObject *
CBORDecoder_decode_float64(CBORDecoderObject *self)
{
    PyObject *ret = NULL;
    union {
        uint64_t i;
        double f;
        char buf[sizeof(double)];
    } u;

    if (fp_read(self, u.buf, sizeof(double)) == 0) {
        u.i = be64toh(u.i);
        ret = PyFloat_FromDouble(u.f);
    }
    set_shareable(self, ret);
    return ret;
}


// CBORDecoder.decode(self) -> obj
static PyObject *
CBORDecoder_decode(CBORDecoderObject *self)
{
    PyObject *ret = NULL;
    LeadByte lead;

    if (Py_EnterRecursiveCall(" in CBORDecoder.decode"))
        return NULL;

    if (fp_read(self, &lead.byte, 1) == 0) {
        switch (lead.major) {
            case 0: ret = decode_uint(self, lead.subtype);       break;
            case 1: ret = decode_negint(self, lead.subtype);     break;
            case 2: ret = decode_bytestring(self, lead.subtype); break;
            case 3: ret = decode_string(self, lead.subtype);     break;
            case 4: ret = decode_array(self, lead.subtype);      break;
            case 5: ret = decode_map(self, lead.subtype);        break;
            case 6: ret = decode_semantic(self, lead.subtype);   break;
            case 7: ret = decode_special(self, lead.subtype);    break;
            default: assert(0);
        }
    }
    Py_LeaveRecursiveCall();
    return ret;
}


// TODO Consolidate these utility methods into arguments on decode?
static PyObject *
CBORDecoder_decode_unshared(CBORDecoderObject *self)
{
    int32_t old_index;
    PyObject *ret;

    old_index = self->shared_index;
    self->shared_index = -1;
    ret = CBORDecoder_decode(self);
    self->shared_index = old_index;
    return ret;
}


static PyObject *
CBORDecoder_decode_immutable(CBORDecoderObject *self)
{
    bool old_immutable;
    PyObject *ret;

    old_immutable = self->immutable;
    self->immutable = true;
    ret = CBORDecoder_decode(self);
    self->immutable = old_immutable;
    return ret;
}


static PyObject *
CBORDecoder_decode_immutable_unshared(CBORDecoderObject *self)
{
    bool old_immutable;
    int32_t old_index;
    PyObject *ret;

    old_immutable = self->immutable;
    old_index = self->shared_index;
    self->immutable = true;
    self->shared_index = -1;
    ret = CBORDecoder_decode(self);
    self->immutable = old_immutable;
    self->shared_index = old_index;
    return ret;
}


// Decoder class definition //////////////////////////////////////////////////

#define PUBLIC_MAJOR(type)                                                   \
    static PyObject *                                                        \
    CBORDecoder_decode_##type(CBORDecoderObject *self, PyObject *subtype)    \
    {                                                                        \
        return decode_##type(self, PyLong_AsUnsignedLong(subtype));          \
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

static PyGetSetDef CBORDecoder_getsetters[] = {
    {"fp",
        (getter) _CBORDecoder_get_fp, (setter) _CBORDecoder_set_fp,
        "input file-like object", NULL},
    {"tag_hook",
        (getter) _CBORDecoder_get_tag_hook, (setter) _CBORDecoder_set_tag_hook,
        "hook called when decoding an unknown semantic tag", NULL},
    {"object_hook",
        (getter) _CBORDecoder_get_object_hook, (setter) _CBORDecoder_set_object_hook,
        "hook called when decoding any dict", NULL},
    {"str_errors",
        (getter) _CBORDecoder_get_str_errors, (setter) _CBORDecoder_set_str_errors,
        "the error mode to use when decoding UTF-8 encoded strings"},
    {NULL}
};

static PyMethodDef CBORDecoder_methods[] = {
    {"read", (PyCFunction) CBORDecoder_read, METH_O,
        "read the specified number of bytes from the input"},
    // Decoding methods
    {"decode", (PyCFunction) CBORDecoder_decode, METH_NOARGS,
        "decode the next value from the input"},
    {"decode_uint", (PyCFunction) CBORDecoder_decode_uint, METH_O,
        "decode an unsigned integer from the input"},
    {"decode_negint", (PyCFunction) CBORDecoder_decode_negint, METH_O,
        "decode a negative integer from the input"},
    {"decode_bytestring", (PyCFunction) CBORDecoder_decode_bytestring, METH_O,
        "decode a bytes string from the input"},
    {"decode_string", (PyCFunction) CBORDecoder_decode_string, METH_O,
        "decode a unicode string from the input"},
    {"decode_array", (PyCFunction) CBORDecoder_decode_array, METH_O,
        "decode a list or tuple from the input"},
    {"decode_map", (PyCFunction) CBORDecoder_decode_map, METH_O,
        "decode a dict from the input"},
    {"decode_semantic", (PyCFunction) CBORDecoder_decode_semantic, METH_O,
        "decode a semantically tagged value from the input"},
    {"decode_special", (PyCFunction) CBORDecoder_decode_special, METH_O,
        "decode a special value from the input"},
    {"decode_datestr", (PyCFunction) CBORDecoder_decode_datestr, METH_NOARGS,
        "decode a date-time string from the input"},
    {"decode_timestamp", (PyCFunction) CBORDecoder_decode_timestamp, METH_NOARGS,
        "decode a timestamp offset from the input"},
    {"decode_positive_bignum",
        (PyCFunction) CBORDecoder_decode_positive_bignum, METH_NOARGS,
        "decode a positive big-integer from the input"},
    {"decode_negative_bignum",
        (PyCFunction) CBORDecoder_decode_negative_bignum, METH_NOARGS,
        "decode a negative big-integer from the input"},
    {"decode_shareable",
        (PyCFunction) CBORDecoder_decode_shareable, METH_NOARGS,
        "decode a shareable value from the input"},
    {"decode_shared", (PyCFunction) CBORDecoder_decode_shared, METH_NOARGS,
        "decode a shared reference from the input"},
    {"decode_set", (PyCFunction) CBORDecoder_decode_set, METH_NOARGS,
        "decode a set or frozenset from the input"},
    {"decode_simplevalue",
        (PyCFunction) CBORDecoder_decode_simplevalue, METH_NOARGS,
        "decode a CBORSimpleValue from the input"},
    {"decode_float16", (PyCFunction) CBORDecoder_decode_float16, METH_NOARGS,
        "decode a half-precision floating-point value from the input"},
    {"decode_float32", (PyCFunction) CBORDecoder_decode_float32, METH_NOARGS,
        "decode a floating-point value from the input"},
    {"decode_float64", (PyCFunction) CBORDecoder_decode_float64, METH_NOARGS,
        "decode a double-precision floating-point value from the input"},
    {"set_shareable", (PyCFunction) CBORDecoder_set_shareable, METH_O,
        "set the specified object as the current shareable reference"},
    {NULL}
};

PyDoc_STRVAR(CBORDecoder__doc__,
"The CBORDecoder class implements a fully featured `CBOR`_ decoder with\n"
"several extensions for handling shared references, big integers,\n"
"rational numbers and so on. Typically the class is not used directly,\n"
"but the :func:`cboar.load` and :func:`cboar.loads` functions are called\n"
"to indirectly construct and use the class.\n"
"\n"
"When the class is constructed manually, the main entry points are\n"
":meth:`decode` and :meth:`decode_from_bytes`.\n"
"\n"
":param tag_hook:\n"
"    callable that takes 2 arguments: the decoder instance, and the\n"
"    :class:`_cboar.CBORTag` to be decoded. This callback is invoked for\n"
"    any tags for which there is no built-in decoder. The return value is\n"
"    substituted for the :class:`_cboar.CBORTag` object in the\n"
"    deserialized output\n"
":param object_hook:\n"
"    callable that takes 2 arguments: the decoder instance, and a\n"
"    dictionary. This callback is invoked for each deserialized\n"
"    :class:`dict` object. The return value is substituted for the dict\n"
"    in the deserialized output.\n"
"\n"
".. _CBOR: https://cbor.io/\n"
);

PyTypeObject CBORDecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.CBORDecoder",
    .tp_doc = CBORDecoder__doc__,
    .tp_basicsize = sizeof(CBORDecoderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = CBORDecoder_new,
    .tp_init = (initproc) CBORDecoder_init,
    .tp_dealloc = (destructor) CBORDecoder_dealloc,
    .tp_traverse = (traverseproc) CBORDecoder_traverse,
    .tp_clear = (inquiry) CBORDecoder_clear,
    .tp_getset = CBORDecoder_getsetters,
    .tp_methods = CBORDecoder_methods,
};
