#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <limits.h>
#include <endian.h>
#include <stdint.h>
#include <math.h>
#include "structmember.h"


typedef struct {
    PyObject_HEAD
    PyObject *write;    // cached write() method of fp
    PyObject *encoders;
    PyObject *default_handler;
    PyObject *shared;
    int timestamp_format;
    bool value_sharing;
} EncoderObject;


// Forward declarations of various functions
typedef PyObject * (EncodeFunction)(EncoderObject *, PyObject *);

static PyObject * Encoder_encode(EncoderObject *, PyObject *);
static PyObject * Encoder_encode_int(EncoderObject *, PyObject *);

static int Encoder_setfp(EncoderObject *, PyObject *, void *);
static int Encoder_setdefault(EncoderObject *, PyObject *, void *);


/* Encoder.__del__(self) */
static void
Encoder_dealloc(EncoderObject *self)
{
    Py_XDECREF(self->encoders);
    Py_XDECREF(self->write);
    Py_XDECREF(self->default_handler);
    Py_XDECREF(self->shared);
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
        self->write = Py_None;
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

    if (Encoder_setfp(self, fp, NULL) == -1)
        return -1;
    if (default_handler && Encoder_setdefault(self, default_handler, NULL) == -1)
        return -1;

    collections = PyImport_ImportModule("collections");
    if (!collections)
        return -1;
    ordered_dict = PyObject_GetAttrString(collections, "OrderedDict");
    Py_DECREF(collections);
    if (!ordered_dict)
        return -1;

    tmp = self->encoders;
    self->encoders = PyObject_CallObject(ordered_dict, NULL);
    Py_DECREF(ordered_dict);
    if (!self->encoders)
        return -1;
    Py_XDECREF(tmp);

    tmp = self->shared;
    self->shared = PyDict_New();
    if (!self->shared)
        return -1;
    Py_XDECREF(tmp);

    return 0;
}


/* Encoder._get_fp(self) */
static PyObject *
Encoder_getfp(EncoderObject *self, void *closure)
{
    PyObject *ret = PyMethod_GET_SELF(self->write);
    Py_INCREF(ret);
    return ret;
}


/* Encoder._set_fp(self, value) */
static int
Encoder_setfp(EncoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp, *write;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete fp attribute");
        return -1;
    }
    write = PyObject_GetAttrString(value, "write");
    if (!(write && PyCallable_Check(write))) {
        PyErr_SetString(PyExc_ValueError,
                        "fp object must have a callable write method");
        return -1;
    }

    // It's a bit naughty caching the write method, but it does provide a
    // notable speed boost avoiding the lookup of the method on every write.
    // Still, it is theoretically valid for an object to change its write()
    // method in the middle of a dump. But unless someone actually complains
    // about this I'm loathe to change it...
    tmp = self->write;
    Py_INCREF(write);
    self->write = write;
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
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete default_handler attribute");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        PyErr_SetString(PyExc_ValueError,
                        "default_handler must be callable or None");
        return -1;
    }

    tmp = self->default_handler;
    Py_INCREF(value);
    self->default_handler = value;
    Py_DECREF(tmp);
    return 0;
}


static int
Encoder__write(EncoderObject *self, const char *buf, const uint32_t length)
{
    PyObject *obj;

    obj = PyObject_CallFunction(self->write, "y#", buf, length);
    Py_XDECREF(obj);
    return obj == NULL ? -1 : 0;
}


static PyObject *
Encoder__load_type(PyObject *type_tuple)
{
    PyObject *mod_name, *module, *type_name, *type, *import_list;

    if (PyTuple_GET_SIZE(type_tuple) != 2) {
        PyErr_SetString(PyExc_ValueError,
                        "deferred load encoder types must be a 2-tuple");
        return NULL;
    }
    mod_name = PyTuple_GET_ITEM(type_tuple, 0);
    if (!PyUnicode_Check(mod_name)) {
        PyErr_SetString(PyExc_ValueError,
                        "deferred load element 0 is not a string");
        return NULL;
    }
    type_name = PyTuple_GET_ITEM(type_tuple, 1);
    if (!PyUnicode_Check(type_name)) {
        PyErr_SetString(PyExc_ValueError,
                        "deferred load element 1 is not a string");
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
    // This function is unusual in that enc_type is a borrowed reference on
    // entry, and the return value (a transformed enc_type) is also a borrowed
    // reference; hence we have to INCREF enc_type to ensure it doesn't
    // disappear when removing it from the encoders dict (which might be the
    // only reference to it)
    Py_INCREF(enc_type);
    if (PyObject_DelItem(self->encoders, enc_type) == 0) {
        ret = Encoder__load_type(enc_type);
        if (ret && PyObject_SetItem(self->encoders, ret, encoder) == 0)
            // This DECREF might look unusual but at this point the encoders
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


static int
Encoder__encode_length(EncoderObject *self, const uint8_t major_tag,
                       const uint64_t length)
{
    char buf[sizeof(uint64_t) + 1];

    if (length < 24) {
        buf[0] = major_tag | length;
        return Encoder__write(self, buf, 1);
    } else if (length <= UCHAR_MAX) {
        buf[0] = major_tag | 24;
        buf[1] = length;
        return Encoder__write(self, buf, sizeof(uint8_t) + 1);
    } else if (length <= USHRT_MAX) {
        buf[0] = major_tag | 25;
        *((uint16_t*)(buf + 1)) = htobe16(length);
        return Encoder__write(self, buf, sizeof(uint16_t) + 1);
    } else if (length <= UINT_MAX) {
        buf[0] = major_tag | 26;
        *((uint32_t*)(buf + 1)) = htobe32(length);
        return Encoder__write(self, buf, sizeof(uint32_t) + 1);
    } else {
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
    // XXX Potential recursion
    obj = Encoder_encode(self, value);
    Py_XDECREF(obj);
    return obj == NULL ? -1 : 0;
}


static PyObject *
Encoder__encode_shared(EncoderObject *self, EncodeFunction *encoder,
                       PyObject *value)
{
    PyObject *id, *container, *index, *tuple, *ret = NULL;

    id = PyLong_FromVoidPtr(value);
    if (id) {
        tuple = PyDict_GetItem(self->shared, id);
        if (tuple) {
            container = PyTuple_GET_ITEM(tuple, 0);
            index = PyTuple_GET_ITEM(tuple, 1);
        }
        if (self->value_sharing) {
            // XXX Do we need to test container == value? I'm not sure...
            if (tuple && container == value) {
                if (Encoder__encode_length(self, 0xD8, 0x1D) == 0)
                    ret = Encoder_encode_int(self, index);
            } else {
                index = PyLong_FromSsize_t(PyDict_Size(self->shared));
                if (index) {
                    tuple = PyTuple_Pack(2, value, index);
                    if (tuple) {
                        if (PyDict_SetItem(self->shared, id, tuple) == 0)
                            if (Encoder__encode_length(self, 0xD8, 0x1C) == 0)
                                ret = encoder(self, value);
                        Py_DECREF(tuple);
                    }
                    Py_DECREF(index);
                }
            }
        } else {
            if (tuple && container == value) {
                PyErr_SetString(PyExc_ValueError,
                                "cyclic data structure detected but "
                                "value_sharing is False");
            } else {
                tuple = PyTuple_Pack(2, value, Py_None);
                if (tuple) {
                    if (PyDict_SetItem(self->shared, id, tuple) == 0) {
                        ret = encoder(self, value);
                        PyDict_DelItem(self->shared, id);
                    }
                    Py_DECREF(tuple);
                }
            }
        }
        Py_DECREF(id);
    }
    return ret;
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


/* Encoder.encode_semantic(self, (tag, value)) */
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
    PyObject *neg, *one, *ret = NULL;

    // return -value - 1
    one = PyLong_FromLong(1);
    if (one) {
        neg = PyNumber_Negative(value);
        if (neg) {
            ret = PyNumber_Subtract(neg, one);
            Py_DECREF(neg);
        }
        Py_DECREF(one);
    }
    return ret;
}


/* Encoder.encode_int(self, value) */
static PyObject *
Encoder_encode_int(EncoderObject *self, PyObject *value)
{
    PyObject *ret = NULL;
    long val;
    int overflow;

    val = PyLong_AsLongAndOverflow(value, &overflow);
    if (overflow == 0) {
        // fast-path: technically this branch isn't needed, but longs are much
        // faster than long longs on some archs and it's likely the *vast*
        // majority of ints encoded will fall into this size
        if (val != -1 || !PyErr_Occurred()) {
            if (val >= 0) {
                if (Encoder__encode_length(self, 0, val) == 0)
                    ret = Py_None;
            } else {
                // avoid overflow in the case where int_value == -2^31
                val = -(val + 1);
                if (Encoder__encode_length(self, 0x20, val) == 0)
                    ret = Py_None;
            }
        }
    } else {
        // fits in 64-bits case: this case isn't technically correct in as
        // much as it skips to "big nums" for anything fully 64-bit (because
        // long long is signed), but I figure that's acceptable
        long long ll_val;

        ll_val = PyLong_AsLongLongAndOverflow(value, &overflow);
        if (overflow == 0) {
            if (ll_val != -1 || !PyErr_Occurred()) {
                if (ll_val >= 0) {
                    if (Encoder__encode_length(self, 0, ll_val) == 0)
                        ret = Py_None;
                } else {
                    // avoid overflow in the case where int_value == -2^63
                    ll_val = -(ll_val + 1);
                    if (Encoder__encode_length(self, 0x20, ll_val) == 0)
                        ret = Py_None;
                }
            }
        } else {
            uint32_t major_tag;

            if (overflow == -1) {
                major_tag = 3;
                value = Encoder__encode_negative(value);
                // value is now an owned reference, instead of borrowed
            } else {
                major_tag = 2;
                // convert value to an owned reference; this isn't strictly
                // necessary but simplifies memory handling in the next bit
                Py_INCREF(value);
            }
            if (value) {
                PyObject *bits = PyObject_CallMethod(value, "bit_length", NULL);
                if (bits) {
                    long length = PyLong_AsLong(bits);
                    if (!PyErr_Occurred()) {
                        PyObject *buf = PyObject_CallMethod(
                                value, "to_bytes", "ls", (length + 7) / 8, "big");
                        if (buf) {
                            if (Encoder__encode_semantic(self, major_tag, buf) == 0)
                                ret = Py_None;
                            Py_DECREF(buf);
                        }
                    }
                    Py_DECREF(bits);
                }
                Py_DECREF(value);
            }
        }
    }
    Py_XINCREF(ret);
    return ret;
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


/* Encoder.encode_float(self, value) */
static PyObject *
Encoder_encode_float(EncoderObject *self, PyObject *value)
{
    union {
        double f;
        uint64_t i;
    } u;

    u.f = PyFloat_AsDouble(value);
    if (u.f == -1.0 && PyErr_Occurred())
        return NULL;
    switch (fpclassify(u.f)) {
        case FP_NAN:
            if (Encoder__write(self, "\xF9\x7E\x00", 3) == -1)
                return NULL;
            break;
        case FP_INFINITE:
            if (u.f > 0) {
                if (Encoder__write(self, "\xF9\x7C\x00", 3) == -1)
                    return NULL;
            } else {
                if (Encoder__write(self, "\xF9\xFC\x00", 3) == -1)
                    return NULL;
            }
            break;
        default:
            if (Encoder__write(self, "\xFB", 1) == -1)
                return NULL;
            u.i = htobe64(u.i);
            if (Encoder__write(self, (char*)(&u.i), sizeof(uint64_t)) == -1)
                return NULL;
            break;
    }
    Py_RETURN_NONE;
}


/* Encoder.encode_boolean(self, value) */
static PyObject *
Encoder_encode_boolean(EncoderObject *self, PyObject *value)
{
    if (PyObject_IsTrue(value)) {
        if (Encoder__write(self, "\xF5", 1) == -1)
            return NULL;
    } else {
        if (Encoder__write(self, "\xF4", 1) == -1)
            return NULL;
    }
    Py_RETURN_NONE;
}


/* Encoder.encode_none(self, value) */
static PyObject *
Encoder_encode_none(EncoderObject *self, PyObject *value)
{
    if (Encoder__write(self, "\xF6", 1) == -1)
        return NULL;
    Py_RETURN_NONE;
}


/* Encoder.encode_undefined(self, value) */
static PyObject *
Encoder_encode_undefined(EncoderObject *self, PyObject *value)
{
    if (Encoder__write(self, "\xF7", 1) == -1)
        return NULL;
    Py_RETURN_NONE;
}


/* Encoder.encode_simple(self, (value,)) */
static PyObject *
Encoder_encode_simple(EncoderObject *self, PyObject *args)
{
    uint8_t value;

    if (!PyArg_ParseTuple(args, "k", &value))
        return NULL;
    if (value < 20) {
        value |= 0xE0;
        if (Encoder__write(self, (char *)&value, 1) == -1)
            return NULL;
    } else {
        if (Encoder__write(self, "0\xF8", 1) == -1)
            return NULL;
        if (Encoder__write(self, (char *)&value, 1) == -1)
            return NULL;
    }
    Py_RETURN_NONE;
}


/* Encoder.encode_rational(self, value) */
static PyObject *
Encoder_encode_rational(EncoderObject *self, PyObject *value)
{
    bool sharing;
    PyObject *tuple, *num, *den, *ret = NULL;

    num = PyObject_GetAttrString(value, "numerator");
    if (num) {
        den = PyObject_GetAttrString(value, "denominator");
        if (den) {
            tuple = PyTuple_Pack(2, num, den);
            if (tuple) {
                sharing = self->value_sharing;
                self->value_sharing = false;
                if (Encoder__encode_semantic(self, 30, tuple) == 0)
                    ret = Py_None;
                self->value_sharing = sharing;
                Py_DECREF(tuple);
            }
            Py_DECREF(den);
        }
        Py_DECREF(num);
    }
    Py_XINCREF(ret);
    return ret;
}


/* Encoder.encode_regex(self, value) */
static PyObject *
Encoder_encode_regex(EncoderObject *self, PyObject *value)
{
    PyObject *pattern, *ret = NULL;

    pattern = PyObject_GetAttrString(value, "pattern");
    if (pattern) {
        if (Encoder__encode_semantic(self, 35, pattern) == 0)
            ret = Py_None;
        Py_DECREF(pattern);
    }
    Py_XINCREF(ret);
    return ret;
}


/* Encoder.encode_mime(self, value) */
static PyObject *
Encoder_encode_mime(EncoderObject *self, PyObject *value)
{
    PyObject *buf, *ret = NULL;

    buf = PyObject_CallMethod(value, "as_string", NULL);
    if (buf) {
        if (Encoder__encode_semantic(self, 36, buf) == 0)
            ret = Py_None;
        Py_DECREF(buf);
    }
    Py_XINCREF(ret);
    return ret;
}


/* Encoder.encode_uuid(self, value) */
static PyObject *
Encoder_encode_uuid(EncoderObject *self, PyObject *value)
{
    PyObject *bytes, *ret = NULL;

    bytes = PyObject_GetAttrString(value, "bytes");
    if (bytes) {
        if (Encoder__encode_semantic(self, 37, bytes) == 0)
            ret = Py_None;
        Py_DECREF(bytes);
    }
    Py_XINCREF(ret);
    return ret;
}


static PyObject *
Encoder__encode_array(EncoderObject *self, PyObject *value)
{
    PyObject *f, *ret = NULL;

    f = PySequence_Fast(value, "argument must be iterable");
    if (f) {
        Py_ssize_t length = PySequence_Fast_GET_SIZE(f);
        PyObject **items = PySequence_Fast_ITEMS(f);
        if (Encoder__encode_length(self, 0x80, length) == 0) {
            while (length) {
                if (!Encoder_encode(self, *items))
                    goto error;
                items++;
                length--;
            }
            ret = Py_None;
            Py_INCREF(ret);
        }
error:
        Py_DECREF(f);
    }
    return ret;
}

/* Encoder.encode_array(self, value) */
static PyObject *
Encoder_encode_array(EncoderObject *self, PyObject *value)
{
    return Encoder__encode_shared(self, &Encoder__encode_array, value);
}


static PyObject *
Encoder__encode_map(EncoderObject *self, PyObject *value)
{
    PyObject *ret = NULL;

    if (PyDict_Check(value)) {
        if (Encoder__encode_length(self, 0xA0, PyDict_Size(value)) == 0) {
            PyObject *key, *val;
            Py_ssize_t pos = 0;

            while (PyDict_Next(value, &pos, &key, &val)) {
                if (!Encoder_encode(self, key))
                    return NULL;
                if (!Encoder_encode(self, val))
                    return NULL;
            }
            ret = Py_None;
            Py_INCREF(ret);
        }
    } else {
        PyObject *list;

        list = PyMapping_Items(value);
        if (list) {
            PyObject *f;

            f = PySequence_Fast(list, "internal error");
            if (f) {
                Py_ssize_t length = PySequence_Fast_GET_SIZE(f);
                PyObject **items = PySequence_Fast_ITEMS(f);
                if (Encoder__encode_length(self, 0xA0, length) == 0) {
                    while (length) {
                        if (!Encoder_encode(self, PyTuple_GET_ITEM(*items, 0)))
                            goto error;
                        if (!Encoder_encode(self, PyTuple_GET_ITEM(*items, 1)))
                            goto error;
                        items++;
                        length--;
                    }
                    ret = Py_None;
                    Py_INCREF(ret);
                }
error:
                Py_DECREF(f);
            }
            Py_DECREF(list);
        }
    }
    return ret;
}

/* Encoder.encode_map(self, value) */
static PyObject *
Encoder_encode_map(EncoderObject *self, PyObject *value)
{
    return Encoder__encode_shared(self, &Encoder__encode_map, value);
}


/* Encoder.encode(self, value) */
static PyObject *
Encoder_encode(EncoderObject *self, PyObject *value)
{
    PyObject *encoder, *ret = NULL;

    encoder = Encoder__find_encoder(self, (PyObject *)Py_TYPE(value));
    if (encoder) {
        ret = PyObject_CallFunctionObjArgs(encoder, self, value, NULL);
        Py_DECREF(encoder);
    }
    return ret;
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
    {"encode_float", (PyCFunction) Encoder_encode_float, METH_O,
        "encode the specified floating-point *value* to the output"},
    {"encode_boolean", (PyCFunction) Encoder_encode_boolean, METH_O,
        "encode the specified boolean *value* to the output"},
    {"encode_none", (PyCFunction) Encoder_encode_none, METH_O,
        "encode the None value to the output"},
    {"encode_undefined", (PyCFunction) Encoder_encode_undefined, METH_O,
        "encode the undefined value to the output"},
    {"encode_bytes", (PyCFunction) Encoder_encode_bytes, METH_O,
        "encode the specified bytes *value* to the output"},
    {"encode_bytearray", (PyCFunction) Encoder_encode_bytearray, METH_O,
        "encode the specified bytearray *value* to the output"},
    {"encode_string", (PyCFunction) Encoder_encode_string, METH_O,
        "encode the specified string *value* to the output"},
    {"encode_array", (PyCFunction) Encoder_encode_array, METH_O,
        "encode the specified sequence *value* to the output"},
    {"encode_map", (PyCFunction) Encoder_encode_map, METH_O,
        "encode the specified mapping *value* to the output"},
    {"encode_length", (PyCFunction) Encoder_encode_length, METH_VARARGS,
        "encode the specified *major_tag* with the specified *length* to the output"},
    {"encode_semantic", (PyCFunction) Encoder_encode_semantic, METH_VARARGS,
        "encode the specified CBORTag to the output"},
    {"encode_simple", (PyCFunction) Encoder_encode_simple, METH_O,
        "encode the specified CBORSimpleValue to the output"},
    {"encode_rational", (PyCFunction) Encoder_encode_rational, METH_O,
        "encode the specified fraction to the output"},
    {"encode_regex", (PyCFunction) Encoder_encode_regex, METH_O,
        "encode the specified regular expression object to the output"},
    {"encode_mime", (PyCFunction) Encoder_encode_mime, METH_O,
        "encode the specified MIME message object to the output"},
    {"encode_uuid", (PyCFunction) Encoder_encode_uuid, METH_O,
        "encode the specified UUID to the output"},
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
