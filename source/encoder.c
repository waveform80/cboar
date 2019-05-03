#define PY_SSIZE_T_CLEAN
#include <Python.h>
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
#include "encoder.h"


typedef PyObject * (EncodeFunction)(CBOREncoderObject *, PyObject *);

static PyObject * CBOREncoder_encode(CBOREncoderObject *, PyObject *);
static PyObject * CBOREncoder_encode_to_bytes(CBOREncoderObject *, PyObject *);
static PyObject * CBOREncoder_encode_int(CBOREncoderObject *, PyObject *);

static int _CBOREncoder_set_fp(CBOREncoderObject *, PyObject *, void *);
static int _CBOREncoder_set_default(CBOREncoderObject *, PyObject *, void *);
static int _CBOREncoder_set_timezone(CBOREncoderObject *, PyObject *, void *);

// TODO Docstrings


// Constructors and destructors //////////////////////////////////////////////

static int
CBOREncoder_traverse(CBOREncoderObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->write);
    Py_VISIT(self->encoders);
    Py_VISIT(self->default_handler);
    Py_VISIT(self->shared);
    Py_VISIT(self->timezone);
    Py_VISIT(self->shared_handler);
    return 0;
}

static int
CBOREncoder_clear(CBOREncoderObject *self)
{
    Py_CLEAR(self->write);
    Py_CLEAR(self->encoders);
    Py_CLEAR(self->default_handler);
    Py_CLEAR(self->shared);
    Py_CLEAR(self->timezone);
    Py_CLEAR(self->shared_handler);
    return 0;
}

// CBOREncoder.__del__(self)
static void
CBOREncoder_dealloc(CBOREncoderObject *self)
{
    PyObject_GC_UnTrack(self);
    CBOREncoder_clear(self);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


// CBOREncoder.__new__(cls, *args, **kwargs)
static PyObject *
CBOREncoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    CBOREncoderObject *self;

    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
        return NULL;

    if (!_CBOAR_OrderedDict && _CBOAR_init_OrderedDict() == -1)
        return NULL;

    self = (CBOREncoderObject *) type->tp_alloc(type, 0);
    if (self) {
        self->encoders = PyObject_CallObject(_CBOAR_OrderedDict, NULL);
        if (!self->encoders)
            goto error;
        self->shared = PyDict_New();
        if (!self->shared)
            goto error;
        Py_INCREF(Py_None);
        self->write = Py_None;
        Py_INCREF(Py_None);
        self->default_handler = Py_None;
        Py_INCREF(Py_None);
        self->timezone = Py_None;
        self->enc_style = 0;
        self->timestamp_format = false;
        self->value_sharing = false;
        self->shared_handler = NULL;
    }
    return (PyObject *) self;
error:
    Py_DECREF(self);
    return NULL;
}


// CBOREncoder.__init__(self, fp=None, default_handler=None,
//                      timestamp_format=0, value_sharing=False)
static int
CBOREncoder_init(CBOREncoderObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {
        "fp", "datetime_as_timestamp", "timezone", "value_sharing", "default",
        "enc_style", NULL
    };
    PyObject *fp = NULL, *default_handler = NULL, *timezone = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pOpOB", keywords,
                &fp, &self->timestamp_format, &timezone, &self->value_sharing,
                &default_handler, &self->enc_style))
        return -1;

    if (_CBOREncoder_set_fp(self, fp, NULL) == -1)
        return -1;
    if (default_handler && _CBOREncoder_set_default(self, default_handler, NULL) == -1)
        return -1;
    if (timezone && _CBOREncoder_set_timezone(self, timezone, NULL) == -1)
        return -1;

    return 0;
}


// Property accessors ////////////////////////////////////////////////////////

// CBOREncoder._get_fp(self)
static PyObject *
_CBOREncoder_get_fp(CBOREncoderObject *self, void *closure)
{
    PyObject *ret = PyMethod_GET_SELF(self->write);
    Py_INCREF(ret);
    return ret;
}


// CBOREncoder._set_fp(self, value)
static int
_CBOREncoder_set_fp(CBOREncoderObject *self, PyObject *value, void *closure)
{
    PyObject *tmp, *write;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete fp attribute");
        return -1;
    }
    write = PyObject_GetAttr(value, _CBOAR_str_write);
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


// CBOREncoder._get_default(self)
static PyObject *
_CBOREncoder_get_default(CBOREncoderObject *self, void *closure)
{
    Py_INCREF(self->default_handler);
    return self->default_handler;
}


// CBOREncoder._set_default(self, value)
static int
_CBOREncoder_set_default(CBOREncoderObject *self, PyObject *value,
                         void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete default_handler attribute");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        PyErr_Format(PyExc_ValueError,
                        "invalid default_handler value %R (must be callable "
                        "or None)", value);
        return -1;
    }

    tmp = self->default_handler;
    Py_INCREF(value);
    self->default_handler = value;
    Py_DECREF(tmp);
    return 0;
}


// CBOREncoder._get_timezone(self)
static PyObject *
_CBOREncoder_get_timezone(CBOREncoderObject *self, void *closure)
{
    Py_INCREF(self->timezone);
    return self->timezone;
}


// CBOREncoder._set_timezone(self, value)
static int
_CBOREncoder_set_timezone(CBOREncoderObject *self, PyObject *value,
                          void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot delete timezone attribute");
        return -1;
    }
    if (!PyTZInfo_Check(value) && value != Py_None) {
        PyErr_Format(PyExc_ValueError,
                        "invalid timezone value %R (must be tzinfo instance "
                        "or None)", value);
        return -1;
    }
    tmp = self->timezone;
    Py_INCREF(value);
    self->timezone = value;
    Py_DECREF(tmp);
    return 0;
}


// Utility methods ///////////////////////////////////////////////////////////

static int
fp_write(CBOREncoderObject *self, const char *buf, const int length)
{
    PyObject *bytes, *ret = NULL;

    bytes = PyBytes_FromStringAndSize(buf, length);
    if (bytes) {
        ret = PyObject_CallFunctionObjArgs(self->write, bytes, NULL);
        Py_XDECREF(ret);
        Py_DECREF(bytes);
    }
    return ret ? 0 : -1;
}


// Given a deferred type tuple (module-name, type-name), import the specified
// module, get the specified type from within it and return it as a new
// reference. Returns NULL and sets an appropriate error if the module cannot
// be imported or the specified type cannot be found within it
static PyObject *
load_type(PyObject *type_tuple)
{
    PyObject *mod_name, *mod, *type_name, *type, *import_list;

    if (PyTuple_GET_SIZE(type_tuple) == 2) {
        mod_name = PyTuple_GET_ITEM(type_tuple, 0);
        type_name = PyTuple_GET_ITEM(type_tuple, 1);
        if (PyUnicode_Check(mod_name) && PyUnicode_Check(type_name)) {
            import_list = PyList_New(1);
            if (!import_list)
                return NULL;
            Py_INCREF(type_name);
            PyList_SET_ITEM(import_list, 0, type_name); // steals ref
            mod = PyImport_ImportModuleLevelObject(
                mod_name, NULL, NULL, import_list, 0);
            Py_DECREF(import_list);
            if (!mod)
                return NULL;
            type = PyObject_GetAttr(mod, type_name);
            Py_DECREF(mod);
            return type;
        }
    }
    PyErr_Format(PyExc_ValueError,
            "invalid deferred encoder type %R (must be a 2-tuple of module "
            "name and type name, e.g. ('collections', 'defaultdict'))",
            type_tuple);
    return NULL;
}


// Given a deferred type item tuple from the self->encoders dictionary, attempt
// to load the specified type (by calling load_type) and replace the
// entry in the dictionary with the loaded type, mapping to the same handler
static PyObject *
replace_type(CBOREncoderObject *self, PyObject *item)
{
    PyObject *enc_type, *encoder, *ret = NULL;

    enc_type = PyTuple_GET_ITEM(item, 0);
    encoder = PyTuple_GET_ITEM(item, 1);
    // This function is unusual in that enc_type is a borrowed reference on
    // entry, and the return value (a transformed enc_type) is also a borrowed
    // reference; hence we have to INCREF enc_type to ensure it doesn't
    // disappear when removing it from the encoders dict (which might be the
    // only reference to it)
    Py_INCREF(enc_type);
    if (PyObject_DelItem(self->encoders, enc_type) == 0) {
        ret = load_type(enc_type);
        if (ret && PyObject_SetItem(self->encoders, ret, encoder) == 0)
            // This DECREF might look unusual but at this point the encoders
            // dict has a ref to the new enc_type, so we want "our" ref to
            // enc_type to be borrowed just as the original was on entry
            Py_DECREF(ret);
        Py_DECREF(enc_type);
    }
    return ret;
}


// CBOREncoder._find_encoder(type)
static PyObject *
CBOREncoder_find_encoder(CBOREncoderObject *self, PyObject *type)
{
    PyObject *enc_type, *items, *iter, *item, *ret;

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
                        enc_type = replace_type(self, item);
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
        if (!ret && !PyErr_Occurred())
            ret = Py_None;
        if (ret)
            Py_INCREF(ret);
    }
    return ret;
}


// Regular encoding methods //////////////////////////////////////////////////

static int
encode_length(CBOREncoderObject *self, const uint8_t major_tag,
              const uint64_t length)
{
    LeadByte *lead;
    char buf[sizeof(LeadByte) + sizeof(uint64_t)];

    lead = (LeadByte*)buf;
    lead->major = major_tag;
    if (length < 24) {
        lead->subtype = length;
        return fp_write(self, buf, 1);
    } else if (length <= UCHAR_MAX) {
        lead->subtype = 24;
        buf[1] = length;
        return fp_write(self, buf, sizeof(uint8_t) + 1);
    } else if (length <= USHRT_MAX) {
        lead->subtype = 25;
        *((uint16_t*)(buf + 1)) = htobe16(length);
        return fp_write(self, buf, sizeof(uint16_t) + 1);
    } else if (length <= UINT_MAX) {
        lead->subtype = 26;
        *((uint32_t*)(buf + 1)) = htobe32(length);
        return fp_write(self, buf, sizeof(uint32_t) + 1);
    } else {
        lead->subtype = 27;
        *((uint64_t*)(buf + 1)) = htobe64(length);
        return fp_write(self, buf, sizeof(uint64_t) + 1);
    }
}


// CBOREncoder.encode_length(self, major_tag, length)
static PyObject *
CBOREncoder_encode_length(CBOREncoderObject *self, PyObject *args)
{
    uint8_t major_tag;
    uint64_t length;

    if (!PyArg_ParseTuple(args, "BK", &major_tag, &length))
        return NULL;
    if (encode_length(self, major_tag, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


static int
encode_semantic(CBOREncoderObject *self, const uint64_t tag, PyObject *value)
{
    PyObject *obj;

    if (encode_length(self, 6, tag) == -1)
        return -1;
    obj = CBOREncoder_encode(self, value);
    Py_XDECREF(obj);
    return obj == NULL ? -1 : 0;
}


// CBOREncoder.encode_semantic(self, tag)
static PyObject *
CBOREncoder_encode_semantic(CBOREncoderObject *self, PyObject *value)
{
    CBORTagObject *tag;

    if (!CBORTag_CheckExact(value))
        return NULL;
    tag = (CBORTagObject *) value;
    if (encode_semantic(self, tag->tag, tag->value) == -1)
        return NULL;
    Py_RETURN_NONE;
}


static PyObject *
encode_shared(CBOREncoderObject *self, EncodeFunction *encoder,
              PyObject *value)
{
    PyObject *id, *index, *tuple, *ret = NULL;

    id = PyLong_FromVoidPtr(value);
    if (id) {
        tuple = PyDict_GetItem(self->shared, id);
        if (self->value_sharing) {
            if (tuple) {
                if (encode_length(self, 6, 29) == 0)
                    ret = CBOREncoder_encode_int(
                            self, PyTuple_GET_ITEM(tuple, 1));
            } else {
                index = PyLong_FromSsize_t(PyDict_Size(self->shared));
                if (index) {
                    tuple = PyTuple_Pack(2, value, index);
                    if (tuple) {
                        if (PyDict_SetItem(self->shared, id, tuple) == 0)
                            if (encode_length(self, 6, 28) == 0)
                                ret = encoder(self, value);
                        Py_DECREF(tuple);
                    }
                    Py_DECREF(index);
                }
            }
        } else {
            if (tuple) {
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


static PyObject *
shared_callback(CBOREncoderObject *self, PyObject *value)
{
    if (PyCallable_Check(self->shared_handler)) {
        return PyObject_CallFunctionObjArgs(
                self->shared_handler, self, value, NULL);
    } else {
        PyErr_Format(PyExc_ValueError,
                "non-callable passed as shared encoding method");
        return NULL;
    }
}


// CBOREncoder.encode_shared(self, encode_method, value)
static PyObject *
CBOREncoder_encode_shared(CBOREncoderObject *self, PyObject *args)
{
    PyObject *method, *value, *tmp, *ret = NULL;

    if (PyArg_ParseTuple(args, "OO", &method, &value)) {
        Py_INCREF(method);
        tmp = self->shared_handler;
        self->shared_handler = method;
        ret = encode_shared(self, &shared_callback, value);
        self->shared_handler = tmp;
        Py_DECREF(method);
    }
    return ret;
}


static PyObject *
encode_negative_int(PyObject *value)
{
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


static PyObject *
encode_larger_int(CBOREncoderObject *self, PyObject *value)
{
    PyObject *zero, *bits, *buf, *tmp, *ret = NULL;
    uint8_t major_tag;
    unsigned long long val;

    zero = PyLong_FromLong(0);
    if (zero) {
        major_tag = 0;
        // This isn't strictly required for the positive case, but ensuring
        // value is a new (instead of "borrowed") reference simplifies the
        // ref counting later
        Py_INCREF(value);
        switch (PyObject_RichCompareBool(value, zero, Py_LT)) {
            case 1:
                major_tag = 1;
                tmp = encode_negative_int(value);
                Py_DECREF(value);
                value = tmp;
                // fall-thru to positive case
            case 0:
                val = PyLong_AsUnsignedLongLong(value);
                if (!PyErr_Occurred()) {
                    if (encode_length(self, major_tag, val) == 0) {
                        Py_INCREF(Py_None);
                        ret = Py_None;
                        break;
                    }
                }
                // fall-thru to error case
            case -1:
                // if error is overflow encode a big-num
                if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
                    PyErr_Clear();
                    major_tag += 2;
                    bits = PyObject_CallMethodObjArgs(
                            value, _CBOAR_str_bit_length, NULL);
                    if (bits) {
                        long length = PyLong_AsLong(bits);
                        if (!PyErr_Occurred()) {
                            buf = PyObject_CallMethod(
                                    value, "to_bytes", "ls#",
                                    (length + 7) / 8, "big", 3);
                            if (buf) {
                                if (encode_semantic(self, major_tag, buf) == 0) {
                                    Py_INCREF(Py_None);
                                    ret = Py_None;
                                }
                                Py_DECREF(buf);
                            }
                        }
                        Py_DECREF(bits);
                    }
                }
                break;
            default:
                assert(0);
        }
        Py_DECREF(value);
    }
    return ret;
}



// CBOREncoder.encode_int(self, value)
static PyObject *
CBOREncoder_encode_int(CBOREncoderObject *self, PyObject *value)
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
                if (encode_length(self, 0, val) == 0) {
                    Py_INCREF(Py_None);
                    ret = Py_None;
                }
            } else {
                // avoid overflow in the case where int_value == -2^31
                val = -(val + 1);
                if (encode_length(self, 1, val) == 0) {
                    Py_INCREF(Py_None);
                    ret = Py_None;
                }
            }
        }
    } else
        ret = encode_larger_int(self, value);
    return ret;
}


// CBOREncoder.encode_bytes(self, value)
static PyObject *
CBOREncoder_encode_bytes(CBOREncoderObject *self, PyObject *value)
{
    char *buf;
    Py_ssize_t length;

    if (PyBytes_AsStringAndSize(value, &buf, &length) == -1)
        return NULL;
    if (encode_length(self, 2, length) == -1)
        return NULL;
    if (fp_write(self, buf, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// CBOREncoder.encode_bytearray(self, value)
static PyObject *
CBOREncoder_encode_bytearray(CBOREncoderObject *self, PyObject *value)
{
    Py_ssize_t length;

    if (!PyByteArray_Check(value)) {
        PyErr_Format(PyExc_ValueError, "invalid bytearray value %R", value);
        return NULL;
    }
    length = PyByteArray_GET_SIZE(value);
    if (encode_length(self, 2, length) == -1)
        return NULL;
    if (fp_write(self, PyByteArray_AS_STRING(value), length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// CBOREncoder.encode_string(self, value)
static PyObject *
CBOREncoder_encode_string(CBOREncoderObject *self, PyObject *value)
{
    char *buf;
    Py_ssize_t length;

    buf = PyUnicode_AsUTF8AndSize(value, &length);
    if (!buf)
        return NULL;
    if (encode_length(self, 3, length) == -1)
        return NULL;
    if (fp_write(self, buf, length) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// CBOREncoder.encode_float(self, value)
static PyObject *
CBOREncoder_encode_float(CBOREncoderObject *self, PyObject *value)
{
    union {
        double f;
        uint64_t i;
        char buf[sizeof(double)];
    } u;

    u.f = PyFloat_AS_DOUBLE(value);
    if (u.f == -1.0 && PyErr_Occurred())
        return NULL;
    switch (fpclassify(u.f)) {
        case FP_NAN:
            if (fp_write(self, "\xF9\x7E\x00", 3) == -1)
                return NULL;
            break;
        case FP_INFINITE:
            if (u.f > 0) {
                if (fp_write(self, "\xF9\x7C\x00", 3) == -1)
                    return NULL;
            } else {
                if (fp_write(self, "\xF9\xFC\x00", 3) == -1)
                    return NULL;
            }
            break;
        default:
            if (fp_write(self, "\xFB", 1) == -1)
                return NULL;
            u.i = htobe64(u.i);
            if (fp_write(self, u.buf, sizeof(double)) == -1)
                return NULL;
            break;
    }
    Py_RETURN_NONE;
}


// A variant of fp_classify for the decimal.Decimal type
static int
decimal_classify(PyObject *value)
{
    PyObject *tmp;

    tmp = PyObject_CallMethodObjArgs(value, _CBOAR_str_is_nan, NULL);
    if (tmp) {
        if (PyObject_IsTrue(tmp)) {
            Py_DECREF(tmp);
            return DC_NAN;
        } else {
            Py_DECREF(tmp);
            tmp = PyObject_CallMethodObjArgs(
                    value, _CBOAR_str_is_infinite, NULL);
            if (tmp) {
                if (PyObject_IsTrue(tmp)) {
                    Py_DECREF(tmp);
                    return DC_INFINITE;
                } else {
                    Py_DECREF(tmp);
                    return DC_NORMAL;
                }
            }
        }
    }
    return DC_ERROR;
}


// Returns 1 if the decimal.Decimal value is < 0, 0 if it's >= 0, and -1 on
// error
static int
decimal_negative(PyObject *value)
{
    PyObject *zero;
    int ret = -1;

    zero = PyLong_FromLong(0);
    if (zero) {
        ret = PyObject_RichCompareBool(value, zero, Py_GT);
        Py_DECREF(zero);
    }
    return ret;
}


static PyObject *
encode_decimal_digits(CBOREncoderObject *self, PyObject *value)
{
    PyObject *tuple, *digits, *exp, *sig, *ten, *tmp, *ret = NULL;
    bool sign, sharing;

    tuple = PyObject_CallMethodObjArgs(value, _CBOAR_str_as_tuple, NULL);
    if (tuple) {
        if (PyArg_ParseTuple(tuple, "pOO", &sign, &digits, &exp)) {
            sig = PyLong_FromLong(0);
            if (sig) {
                ten = PyLong_FromLong(10);
                if (ten) {
                    Py_ssize_t length = PyTuple_GET_SIZE(digits);
                    // for digit in digits: sig = (sig * 10) + digit
                    for (Py_ssize_t i = 0; i < length; ++i) {
                        tmp = PyNumber_Multiply(sig, ten);
                        if (!tmp)
                            break;
                        Py_DECREF(sig);
                        sig = tmp;
                        tmp = PyNumber_Add(sig, PyTuple_GET_ITEM(digits, i));
                        if (!tmp)
                            break;
                        Py_DECREF(sig);
                        sig = tmp;
                    }
                    Py_DECREF(ten);
                    // if sign: sig = -sig
                    if (tmp && sign) {
                        tmp = PyNumber_Negative(sig);
                        if (tmp) {
                            Py_DECREF(sig);
                            sig = tmp;
                        }
                    }
                    if (tmp) {
                        sharing = self->value_sharing;
                        self->value_sharing = false;
                        value = PyTuple_Pack(2, exp, sig);
                        if (value) {
                            if (encode_semantic(self, 4, value) == 0) {
                                Py_INCREF(Py_None);
                                ret = Py_None;
                            }
                            Py_DECREF(value);
                        }
                        self->value_sharing = sharing;
                    }
                }
                Py_DECREF(sig);
            }
        }
        Py_DECREF(tuple);
    }
    return ret;
}


// CBOREncoder.encode_decimal(self, value)
static PyObject *
CBOREncoder_encode_decimal(CBOREncoderObject *self, PyObject *value)
{
    switch (decimal_classify(value)) {
        case DC_NAN:
            if (fp_write(self, "\xF9\x7E\x00", 3) == -1)
                return NULL;
            break;
        case DC_INFINITE:
            switch (decimal_negative(value)) {
                case 1:
                    if (fp_write(self, "\xF9\x7C\x00", 3) == -1)
                        return NULL;
                    break;
                case 0:
                    if (fp_write(self, "\xF9\xFC\x00", 3) == -1)
                        return NULL;
                    break;
                case -1:
                    return NULL;
                default:
                    assert(0);
            }
            break;
        case DC_NORMAL:
            return encode_decimal_digits(self, value);
        case DC_ERROR:
            return NULL;
        default:
            assert(0);
    }
    Py_RETURN_NONE;
}


static PyObject *
encode_timestamp(CBOREncoderObject *self, PyObject *timestamp)
{
    PyObject *ret = NULL;

    if (fp_write(self, "\xC1", 1) == 0) {
        double d = PyFloat_AS_DOUBLE(timestamp);
        if (d == trunc(d)) {
            PyObject *i = PyLong_FromDouble(d);
            if (i) {
                ret = CBOREncoder_encode_int(self, i);
                Py_DECREF(i);
            }
        } else {
            ret = CBOREncoder_encode_float(self, timestamp);
        }
    }
    return ret;
}


static PyObject *
encode_datestr(CBOREncoderObject *self, PyObject *datestr)
{
    char *buf;
    Py_ssize_t length, match;

    match = PyUnicode_Tailmatch(
        datestr, _CBOAR_str_utc_suffix, PyUnicode_GET_LENGTH(datestr) - 6,
        PyUnicode_GET_LENGTH(datestr), 1);
    if (match != -1) {
        buf = PyUnicode_AsUTF8AndSize(datestr, &length);
        if (buf) {
            if (fp_write(self, "\xC0", 1) == 0) {
                if (match) {
                    if (encode_length(self, 3, length - 5) == 0)
                        if (fp_write(self, buf, length - 6) == 0)
                            if (fp_write(self, "Z", 1) == 0)
                                Py_RETURN_NONE;
                } else {
                    if (encode_length(self, 3, length) == 0)
                        if (fp_write(self, buf, length) == 0)
                            Py_RETURN_NONE;
                }
            }
        }
    }
    return NULL;
}


// CBOREncoder.encode_datetime(self, value)
static PyObject *
CBOREncoder_encode_datetime(CBOREncoderObject *self, PyObject *value)
{
    PyObject *tmp, *ret = NULL;

    if (PyDateTime_Check(value)) {
        if (!((PyDateTime_DateTime*)value)->hastzinfo) {
            if (self->timezone != Py_None) {
                value = PyDateTimeAPI->DateTime_FromDateAndTime(
                        PyDateTime_GET_YEAR(value),
                        PyDateTime_GET_MONTH(value),
                        PyDateTime_GET_DAY(value),
                        PyDateTime_DATE_GET_HOUR(value),
                        PyDateTime_DATE_GET_MINUTE(value),
                        PyDateTime_DATE_GET_SECOND(value),
                        PyDateTime_DATE_GET_MICROSECOND(value),
                        self->timezone,
                        PyDateTimeAPI->DateTimeType);
            } else {
                PyErr_Format(PyExc_ValueError,
                                "naive datetime %R encountered and no default "
                                "timezone has been set", value);
                value = NULL;
            }
        } else {
            // convert value from borrowed to a new reference to simplify our
            // cleanup later
            Py_INCREF(value);
        }

        if (value) {
            if (self->timestamp_format) {
                tmp = PyObject_CallMethodObjArgs(
                        value, _CBOAR_str_timestamp, NULL);
                if (tmp)
                    ret = encode_timestamp(self, tmp);
            } else {
                tmp = PyObject_CallMethodObjArgs(
                        value, _CBOAR_str_isoformat, NULL);
                if (tmp)
                    ret = encode_datestr(self, tmp);
            }
            Py_XDECREF(tmp);
            Py_DECREF(value);
        }
    }
    return ret;
}


// CBOREncoder.encode_date(self, value)
static PyObject *
CBOREncoder_encode_date(CBOREncoderObject *self, PyObject *value)
{
    PyObject *datetime, *ret = NULL;

    if (PyDate_Check(value)) {
        datetime = PyDateTimeAPI->DateTime_FromDateAndTime(
                PyDateTime_GET_YEAR(value),
                PyDateTime_GET_MONTH(value),
                PyDateTime_GET_DAY(value),
                0, 0, 0, 0, self->timezone,
                PyDateTimeAPI->DateTimeType);
        if (datetime) {
            ret = CBOREncoder_encode_datetime(self, datetime);
            Py_DECREF(datetime);
            return ret;
        }
    }
    return NULL;
}


// CBOREncoder.encode_boolean(self, value)
static PyObject *
CBOREncoder_encode_boolean(CBOREncoderObject *self, PyObject *value)
{
    if (PyObject_IsTrue(value)) {
        if (fp_write(self, "\xF5", 1) == -1)
            return NULL;
    } else {
        if (fp_write(self, "\xF4", 1) == -1)
            return NULL;
    }
    Py_RETURN_NONE;
}


// CBOREncoder.encode_none(self, value)
static PyObject *
CBOREncoder_encode_none(CBOREncoderObject *self, PyObject *value)
{
    if (fp_write(self, "\xF6", 1) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// CBOREncoder.encode_undefined(self, value)
static PyObject *
CBOREncoder_encode_undefined(CBOREncoderObject *self, PyObject *value)
{
    if (fp_write(self, "\xF7", 1) == -1)
        return NULL;
    Py_RETURN_NONE;
}


// CBOREncoder.encode_simple(self, (value,))
static PyObject *
CBOREncoder_encode_simple(CBOREncoderObject *self, PyObject *args)
{
    uint8_t value;

    if (!PyArg_ParseTuple(args, "B", &value))
        return NULL;
    if (value < 20) {
        value |= 0xE0;
        if (fp_write(self, (char *)&value, 1) == -1)
            return NULL;
    } else {
        if (fp_write(self, "\xF8", 1) == -1)
            return NULL;
        if (fp_write(self, (char *)&value, 1) == -1)
            return NULL;
    }
    Py_RETURN_NONE;
}


// CBOREncoder.encode_rational(self, value)
static PyObject *
CBOREncoder_encode_rational(CBOREncoderObject *self, PyObject *value)
{
    PyObject *tuple, *num, *den, *ret = NULL;
    bool sharing;

    num = PyObject_GetAttr(value, _CBOAR_str_numerator);
    if (num) {
        den = PyObject_GetAttr(value, _CBOAR_str_denominator);
        if (den) {
            tuple = PyTuple_Pack(2, num, den);
            if (tuple) {
                sharing = self->value_sharing;
                self->value_sharing = false;
                if (encode_semantic(self, 30, tuple) == 0)
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


// CBOREncoder.encode_regex(self, value)
static PyObject *
CBOREncoder_encode_regex(CBOREncoderObject *self, PyObject *value)
{
    PyObject *pattern, *ret = NULL;

    pattern = PyObject_GetAttr(value, _CBOAR_str_pattern);
    if (pattern) {
        if (encode_semantic(self, 35, pattern) == 0)
            ret = Py_None;
        Py_DECREF(pattern);
    }
    Py_XINCREF(ret);
    return ret;
}


// CBOREncoder.encode_mime(self, value)
static PyObject *
CBOREncoder_encode_mime(CBOREncoderObject *self, PyObject *value)
{
    PyObject *buf, *ret = NULL;

    buf = PyObject_CallMethodObjArgs(value, _CBOAR_str_as_string, NULL);
    if (buf) {
        if (encode_semantic(self, 36, buf) == 0)
            ret = Py_None;
        Py_DECREF(buf);
    }
    Py_XINCREF(ret);
    return ret;
}


// CBOREncoder.encode_uuid(self, value)
static PyObject *
CBOREncoder_encode_uuid(CBOREncoderObject *self, PyObject *value)
{
    PyObject *bytes, *ret = NULL;

    bytes = PyObject_GetAttr(value, _CBOAR_str_bytes);
    if (bytes) {
        if (encode_semantic(self, 37, bytes) == 0)
            ret = Py_None;
        Py_DECREF(bytes);
    }
    Py_XINCREF(ret);
    return ret;
}


static PyObject *
encode_array(CBOREncoderObject *self, PyObject *value)
{
    PyObject **items, *fast, *ret = NULL;
    Py_ssize_t length;

    fast = PySequence_Fast(value, "argument must be iterable");
    if (fast) {
        length = PySequence_Fast_GET_SIZE(fast);
        items = PySequence_Fast_ITEMS(fast);
        if (encode_length(self, 4, length) == 0) {
            while (length) {
                ret = CBOREncoder_encode(self, *items);
                if (ret)
                    Py_DECREF(ret);
                else
                    goto error;
                items++;
                length--;
            }
            Py_INCREF(Py_None);
            ret = Py_None;
        }
error:
        Py_DECREF(fast);
    }
    return ret;
}


// CBOREncoder.encode_array(self, value)
static PyObject *
CBOREncoder_encode_array(CBOREncoderObject *self, PyObject *value)
{
    return encode_shared(self, &encode_array, value);
}


static PyObject *
encode_dict(CBOREncoderObject *self, PyObject *value)
{
    PyObject *key, *val, *ret;
    Py_ssize_t pos = 0;

    if (encode_length(self, 5, PyDict_Size(value)) == 0) {
        while (PyDict_Next(value, &pos, &key, &val)) {
            Py_INCREF(key);
            ret = CBOREncoder_encode(self, key);
            Py_DECREF(key);
            if (ret)
                Py_DECREF(ret);
            else
                return NULL;
            Py_INCREF(val);
            ret = CBOREncoder_encode(self, val);
            Py_DECREF(val);
            if (ret)
                Py_DECREF(ret);
            else
                return NULL;
        }
    }
    Py_RETURN_NONE;
}


static PyObject *
encode_mapping(CBOREncoderObject *self, PyObject *value)
{
    PyObject **items, *list, *fast, *ret = NULL;
    Py_ssize_t length;

    list = PyMapping_Items(value);
    if (list) {
        fast = PySequence_Fast(list, "internal error");
        if (fast) {
            length = PySequence_Fast_GET_SIZE(fast);
            items = PySequence_Fast_ITEMS(fast);
            if (encode_length(self, 5, length) == 0) {
                while (length) {
                    ret = CBOREncoder_encode(self, PyTuple_GET_ITEM(*items, 0));
                    if (ret)
                        Py_DECREF(ret);
                    else
                        goto error;
                    ret = CBOREncoder_encode(self, PyTuple_GET_ITEM(*items, 1));
                    if (ret)
                        Py_DECREF(ret);
                    else
                        goto error;
                    items++;
                    length--;
                }
                ret = Py_None;
                Py_INCREF(ret);
            }
error:
            Py_DECREF(fast);
        }
        Py_DECREF(list);
    }
    return ret;
}


static PyObject *
CBOREncoder__encode_map(CBOREncoderObject *self, PyObject *value)
{
    if (PyDict_Check(value))
        return encode_dict(self, value);
    else
        return encode_mapping(self, value);
}


// CBOREncoder.encode_map(self, value)
static PyObject *
CBOREncoder_encode_map(CBOREncoderObject *self, PyObject *value)
{
    return encode_shared(self, &CBOREncoder__encode_map, value);
}


static PyObject *
encode_set(CBOREncoderObject *self, PyObject *value)
{
    Py_ssize_t length;
    PyObject *iter, *item, *ret = NULL;

    length = PySet_Size(value);
    if (length != -1) {
        iter = PyObject_GetIter(value);
        if (iter) {
            if (encode_length(self, 6, 258) == 0) {
                if (encode_length(self, 4, length) == 0) {
                    while ((item = PyIter_Next(iter))) {
                        ret = CBOREncoder_encode(self, item);
                        Py_DECREF(item);
                        if (ret)
                            Py_DECREF(ret);
                        else
                            goto error;
                    }
                    if (!PyErr_Occurred()) {
                        Py_INCREF(Py_None);
                        ret = Py_None;
                    }
                }
            }
error:
            Py_DECREF(iter);
        }
    }
    return ret;
}


// CBOREncoder.encode_set(self, value)
static PyObject *
CBOREncoder_encode_set(CBOREncoderObject *self, PyObject *value)
{
    return encode_shared(self, &encode_set, value);
}


// Canonical encoding methods ////////////////////////////////////////////////

// CBOREncoder.encode_minimal_float(self, value)
static PyObject *
CBOREncoder_encode_minimal_float(CBOREncoderObject *self, PyObject *value)
{
    union {
        double f;
        uint64_t i;
        char buf[sizeof(double)];
    } u_double;

    union {
        float f;
        uint32_t i;
        char buf[sizeof(float)];
    } u_single;

    union {
        uint16_t i;
        char buf[sizeof(uint16_t)];
    } u_half;

    u_double.f = PyFloat_AS_DOUBLE(value);
    if (u_double.f == -1.0 && PyErr_Occurred())
        return NULL;
    switch (fpclassify(u_double.f)) {
        case FP_NAN:
            if (fp_write(self, "\xF9\x7E\x00", 3) == -1)
                return NULL;
            break;
        case FP_INFINITE:
            if (u_double.f > 0) {
                if (fp_write(self, "\xF9\x7C\x00", 3) == -1)
                    return NULL;
            } else {
                if (fp_write(self, "\xF9\xFC\x00", 3) == -1)
                    return NULL;
            }
            break;
        default:
            u_single.f = u_double.f;
            if (u_single.f == u_double.f) {
                u_half.i = pack_float16(u_single.f);
                if (unpack_float16(u_half.i) == u_single.f) {
                    if (fp_write(self, "\xF9", 1) == -1)
                        return NULL;
                    if (fp_write(self, u_half.buf, sizeof(uint16_t)) == -1)
                        return NULL;
                } else {
                    if (fp_write(self, "\xFA", 1) == -1)
                        return NULL;
                    u_single.i = htobe32(u_single.i);
                    if (fp_write(self, u_single.buf, sizeof(float)) == -1)
                        return NULL;
                }
            } else {
                if (fp_write(self, "\xFB", 1) == -1)
                    return NULL;
                u_double.i = htobe64(u_double.i);
                if (fp_write(self, u_double.buf, sizeof(double)) == -1)
                    return NULL;
            }
            break;
    }
    Py_RETURN_NONE;
}


static PyObject *
encode_canonical_map_list(CBOREncoderObject *self, PyObject *list)
{
    PyObject *bytes, *ret;
    Py_ssize_t index;

    if (PyList_Sort(list) == -1)
        return NULL;
    if (encode_length(self, 5, PyList_GET_SIZE(list)) == -1)
        return NULL;
    for (index = 0; index < PyList_GET_SIZE(list); ++index) {
        // We already have the encoded form of the key so just write it out
        bytes = PyTuple_GET_ITEM(PyList_GET_ITEM(list, index), 1);
        if (fp_write(self, PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes)) == -1)
            return NULL;
        ret = CBOREncoder_encode(self,
                PyTuple_GET_ITEM(PyList_GET_ITEM(list, index), 3));
        if (ret)
            Py_DECREF(ret);
        else
            return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
dict_to_canonical_list(CBOREncoderObject *self, PyObject *value)
{
    PyObject *bytes, *length, *key, *val, *tuple, *ret, *list;
    Py_ssize_t index, pos;

    ret = list = PyList_New(PyDict_Size(value));
    if (list) {
        pos = 0;
        index = 0;
        while (ret && PyDict_Next(value, &pos, &key, &val)) {
            Py_INCREF(key);
            bytes = CBOREncoder_encode_to_bytes(self, key);
            Py_DECREF(key);
            if (bytes) {
                length = PyLong_FromSsize_t(PyBytes_GET_SIZE(bytes));
                if (length) {
                    tuple = PyTuple_Pack(4, length, bytes, key, val);
                    if (tuple)
                        PyList_SET_ITEM(list, index, tuple); // steals ref
                    else
                        ret = NULL;
                    index++;
                    Py_DECREF(length);
                } else
                    ret = NULL;
                Py_DECREF(bytes);
            } else
                ret = NULL;
        }
        if (!ret)
            Py_DECREF(list);
    }
    return ret;
}


static PyObject *
mapping_to_canonical_list(CBOREncoderObject *self, PyObject *value)
{
    PyObject **items, *map_items, *fast, *bytes, *length, *tuple, *ret, *list;
    Py_ssize_t fast_len, index;

    ret = list = PyList_New(PyMapping_Size(value));
    if (list) {
        map_items = PyMapping_Items(value);
        if (map_items) {
            fast = PySequence_Fast(map_items, "internal error");
            if (fast) {
                index = 0;
                fast_len = PySequence_Fast_GET_SIZE(fast);
                items = PySequence_Fast_ITEMS(fast);
                while (ret && fast_len) {
                    bytes = CBOREncoder_encode_to_bytes(self,
                        PyTuple_GET_ITEM(*items, 0));
                    if (bytes) {
                        length = PyLong_FromSsize_t(PyBytes_GET_SIZE(bytes));
                        if (length) {
                            tuple = PyTuple_Pack(4,
                                length, bytes,
                                PyTuple_GET_ITEM(*items, 0),
                                PyTuple_GET_ITEM(*items, 1));
                            if (tuple)
                                PyList_SET_ITEM(list, index, tuple); // steals ref
                            else
                                ret = NULL;
                            Py_DECREF(length);
                        } else
                            ret = NULL;
                        Py_DECREF(bytes);
                    } else
                        ret = NULL;
                    items++;
                    fast_len--;
                }
                Py_DECREF(fast);
            } else
                ret = NULL;
            Py_DECREF(map_items);
        } else
            ret = NULL;
        if (!ret)
            Py_DECREF(list);
    }
    return ret;
}


static PyObject *
encode_canonical_map(CBOREncoderObject *self, PyObject *value)
{
    PyObject *list, *ret = NULL;

    if (PyDict_Check(value))
        list = dict_to_canonical_list(self, value);
    else
        list = mapping_to_canonical_list(self, value);
    if (list) {
        ret = encode_canonical_map_list(self, list);
        Py_DECREF(list);
    }
    return ret;
}


static PyObject *
CBOREncoder_encode_canonical_map(CBOREncoderObject *self, PyObject *value)
{
    return encode_shared(self, &encode_canonical_map, value);
}


static PyObject *
encode_canonical_set_list(CBOREncoderObject *self, PyObject *list)
{
    PyObject *bytes;
    Py_ssize_t index;

    if (PyList_Sort(list) == -1)
        return NULL;
    if (encode_length(self, 6, 258) == -1)
        return NULL;
    if (encode_length(self, 4, PyList_GET_SIZE(list)) == -1)
        return NULL;
    for (index = 0; index < PyList_GET_SIZE(list); ++index) {
        // We already have the encoded form, so just write it out
        bytes = PyTuple_GET_ITEM(PyList_GET_ITEM(list, index), 1);
        if (fp_write(self, PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes)) == -1)
            return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
set_to_canonical_list(CBOREncoderObject *self, PyObject *value)
{
    PyObject *iter, *bytes, *length, *item, *tuple, *list, *ret;
    Py_ssize_t index;

    ret = list = PyList_New(PySet_GET_SIZE(value));
    if (list) {
        iter = PyObject_GetIter(value);
        if (iter) {
            index = 0;
            while (ret && (item = PyIter_Next(iter))) {
                bytes = CBOREncoder_encode_to_bytes(self, item);
                if (bytes) {
                    length = PyLong_FromSsize_t(PyBytes_GET_SIZE(bytes));
                    if (length) {
                        tuple = PyTuple_Pack(3, length, bytes, item);
                        if (tuple)
                            PyList_SET_ITEM(list, index, tuple); // steals ref
                        else
                            ret = NULL;
                        index++;
                        Py_DECREF(length);
                    } else
                        ret = NULL;
                    Py_DECREF(bytes);
                } else
                    ret = NULL;
                Py_DECREF(item);
            }
            Py_DECREF(iter);
        }
        if (!ret)
            Py_DECREF(list);
    }
    return ret;
}


static PyObject *
encode_canonical_set(CBOREncoderObject *self, PyObject *value)
{
    PyObject *list, *ret = NULL;

    list = set_to_canonical_list(self, value);
    if (list) {
        ret = encode_canonical_set_list(self, list);
        Py_DECREF(list);
    }
    return ret;
}


static PyObject *
CBOREncoder_encode_canonical_set(CBOREncoderObject *self, PyObject *value)
{
    return encode_shared(self, &encode_canonical_set, value);
}


// Main entry points /////////////////////////////////////////////////////////

static inline PyObject *
encode(CBOREncoderObject *self, PyObject *value)
{
    PyObject *encoder, *ret = NULL;

    switch (self->enc_style) {
        case 1:
            // canonical encoders
            if (PyFloat_CheckExact(value))
                return CBOREncoder_encode_minimal_float(self, value);
            else if (PyDict_CheckExact(value))
                return CBOREncoder_encode_canonical_map(self, value);
            else if (PyAnySet_CheckExact(value))
                return CBOREncoder_encode_canonical_set(self, value);
            // fall-thru
        case 0:
            // regular encoders
            if (PyBytes_CheckExact(value))
                return CBOREncoder_encode_bytes(self, value);
            else if (PyByteArray_CheckExact(value))
                return CBOREncoder_encode_bytearray(self, value);
            else if (PyUnicode_CheckExact(value))
                return CBOREncoder_encode_string(self, value);
            else if (PyLong_CheckExact(value))
                return CBOREncoder_encode_int(self, value);
            else if (PyFloat_CheckExact(value))
                return CBOREncoder_encode_float(self, value);
            else if (PyBool_Check(value))
                return CBOREncoder_encode_boolean(self, value);
            else if (value == Py_None)
                return CBOREncoder_encode_none(self, value);
            else if (PyTuple_CheckExact(value))
                return CBOREncoder_encode_array(self, value);
            else if (PyList_CheckExact(value))
                return CBOREncoder_encode_array(self, value);
            else if (PyDict_CheckExact(value))
                return CBOREncoder_encode_map(self, value);
            else if (PyDateTime_CheckExact(value))
                return CBOREncoder_encode_datetime(self, value);
            else if (PyDate_CheckExact(value))
                return CBOREncoder_encode_date(self, value);
            else if (PyAnySet_CheckExact(value))
                return CBOREncoder_encode_set(self, value);
            // fall-thru
        default:
            // lookup type (or subclass) in self->encoders
            encoder = CBOREncoder_find_encoder(self, (PyObject *)Py_TYPE(value));
            if (encoder) {
                if (encoder != Py_None)
                    ret = PyObject_CallFunctionObjArgs(
                            encoder, self, value, NULL);
                else if (self->default_handler != Py_None)
                    ret = PyObject_CallFunctionObjArgs(
                            self->default_handler, self, value, NULL);
                else
                    PyErr_Format(PyExc_ValueError, "cannot serialize type %R",
                            (PyObject *)Py_TYPE(value));
                Py_DECREF(encoder);
            }
    }
    return ret;
}


// CBOREncoder.encode(self, value)
static PyObject *
CBOREncoder_encode(CBOREncoderObject *self, PyObject *value)
{
    PyObject *ret;

    // TODO reset shared dict?
    if (Py_EnterRecursiveCall(" in CBOREncoder.encode"))
        return NULL;
    ret = encode(self, value);
    Py_LeaveRecursiveCall();
    return ret;
}


static PyObject *
CBOREncoder_encode_to_bytes(CBOREncoderObject *self, PyObject *value)
{
    PyObject *save_write, *buf, *ret = NULL;

    if (!_CBOAR_BytesIO && _CBOAR_init_BytesIO() == -1)
        return NULL;

    save_write = self->write;
    buf = PyObject_CallFunctionObjArgs(_CBOAR_BytesIO, NULL);
    if (buf) {
        self->write = PyObject_GetAttr(buf, _CBOAR_str_write);
        if (self->write) {
            ret = CBOREncoder_encode(self, value);
            if (ret) {
                assert(ret == Py_None);
                Py_DECREF(ret);
                ret = PyObject_CallMethodObjArgs(buf, _CBOAR_str_getvalue, NULL);
            }
            Py_DECREF(self->write);
        }
        Py_DECREF(buf);
    }
    self->write = save_write;
    return ret;
}


// Encoder class definition //////////////////////////////////////////////////

static PyMemberDef CBOREncoder_members[] = {
    {"encoders", T_OBJECT_EX, offsetof(CBOREncoderObject, encoders), READONLY,
        "the ordered dict mapping types to encoder functions"},
    {"enc_style", T_UBYTE, offsetof(CBOREncoderObject, enc_style), 0,
        "the optimized encoder lookup to use (0=regular, 1=canonical, "
        "anything else is custom)"},
    {"timestamp_format", T_BOOL, offsetof(CBOREncoderObject, timestamp_format), 0,
        "the sub-type to use when encoding datetime objects"},
    {"value_sharing", T_BOOL, offsetof(CBOREncoderObject, value_sharing), 0,
        "if True, then efficiently encode recursive structures"},
    {NULL}
};

static PyGetSetDef CBOREncoder_getsetters[] = {
    {"fp",
        (getter) _CBOREncoder_get_fp, (setter) _CBOREncoder_set_fp,
        "output file-like object", NULL},
    {"default_handler",
        (getter) _CBOREncoder_get_default, (setter) _CBOREncoder_set_default,
        "default handler called when encoding unknown objects", NULL},
    {"timezone",
        (getter) _CBOREncoder_get_timezone, (setter) _CBOREncoder_set_timezone,
        "the timezone to use when encoding naive datetime objects", NULL},
    {NULL}
};

static PyMethodDef CBOREncoder_methods[] = {
    {"_find_encoder", (PyCFunction) CBOREncoder_find_encoder, METH_O,
        "find an encoding function for the specified type"},
    // Standard encoding methods
    {"encode", (PyCFunction) CBOREncoder_encode, METH_O,
        "encode the specified *value* to the output"},
    {"encode_to_bytes", (PyCFunction) CBOREncoder_encode_to_bytes, METH_O,
        "encode the specified *value* to a bytestring"},
    {"encode_length", (PyCFunction) CBOREncoder_encode_length, METH_VARARGS,
        "encode the specified *major_tag* with the specified *length* to "
        "the output"},
    {"encode_int", (PyCFunction) CBOREncoder_encode_int, METH_O,
        "encode the specified integer *value* to the output"},
    {"encode_float", (PyCFunction) CBOREncoder_encode_float, METH_O,
        "encode the specified floating-point *value* to the output"},
    {"encode_boolean", (PyCFunction) CBOREncoder_encode_boolean, METH_O,
        "encode the specified boolean *value* to the output"},
    {"encode_none", (PyCFunction) CBOREncoder_encode_none, METH_O,
        "encode the None value to the output"},
    {"encode_undefined", (PyCFunction) CBOREncoder_encode_undefined, METH_O,
        "encode the undefined value to the output"},
    {"encode_datetime", (PyCFunction) CBOREncoder_encode_datetime, METH_O,
        "encode the datetime *value* to the output"},
    {"encode_date", (PyCFunction) CBOREncoder_encode_date, METH_O,
        "encode the date *value* to the output"},
    {"encode_bytes", (PyCFunction) CBOREncoder_encode_bytes, METH_O,
        "encode the specified bytes *value* to the output"},
    {"encode_bytearray", (PyCFunction) CBOREncoder_encode_bytearray, METH_O,
        "encode the specified bytearray *value* to the output"},
    {"encode_string", (PyCFunction) CBOREncoder_encode_string, METH_O,
        "encode the specified string *value* to the output"},
    {"encode_array", (PyCFunction) CBOREncoder_encode_array, METH_O,
        "encode the specified sequence *value* to the output"},
    {"encode_map", (PyCFunction) CBOREncoder_encode_map, METH_O,
        "encode the specified mapping *value* to the output"},
    {"encode_semantic", (PyCFunction) CBOREncoder_encode_semantic, METH_O,
        "encode the specified CBORTag to the output"},
    {"encode_simple", (PyCFunction) CBOREncoder_encode_simple, METH_O,
        "encode the specified CBORSimpleValue to the output"},
    {"encode_rational", (PyCFunction) CBOREncoder_encode_rational, METH_O,
        "encode the specified fraction to the output"},
    {"encode_decimal", (PyCFunction) CBOREncoder_encode_decimal, METH_O,
        "encode the specified Decimal to the output"},
    {"encode_regex", (PyCFunction) CBOREncoder_encode_regex, METH_O,
        "encode the specified regular expression object to the output"},
    {"encode_mime", (PyCFunction) CBOREncoder_encode_mime, METH_O,
        "encode the specified MIME message object to the output"},
    {"encode_uuid", (PyCFunction) CBOREncoder_encode_uuid, METH_O,
        "encode the specified UUID to the output"},
    {"encode_set", (PyCFunction) CBOREncoder_encode_set, METH_O,
        "encode the specified set to the output"},
    {"encode_shared", (PyCFunction) CBOREncoder_encode_shared, METH_VARARGS,
        "encode the specified CBORTag to the output"},
    // Canonical encoding methods
    {"encode_minimal_float",
        (PyCFunction) CBOREncoder_encode_minimal_float, METH_O,
        "encode the specified float to a minimal representation in the output"},
    {"encode_canonical_map",
        (PyCFunction) CBOREncoder_encode_canonical_map, METH_O,
        "encode the specified map to a canonical representation in the output"},
    {"encode_canonical_set",
        (PyCFunction) CBOREncoder_encode_canonical_set, METH_O,
        "encode the specified set to a canonical representation in the output"},
    {NULL}
};

PyTypeObject CBOREncoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.CBOREncoder",
    .tp_doc = "CBOR encoder objects",
    .tp_basicsize = sizeof(CBOREncoderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = CBOREncoder_new,
    .tp_init = (initproc) CBOREncoder_init,
    .tp_dealloc = (destructor) CBOREncoder_dealloc,
    .tp_traverse = (traverseproc) CBOREncoder_traverse,
    .tp_clear = (inquiry) CBOREncoder_clear,
    .tp_members = CBOREncoder_members,
    .tp_getset = CBOREncoder_getsetters,
    .tp_methods = CBOREncoder_methods,
};
