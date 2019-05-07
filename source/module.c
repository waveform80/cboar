#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>
#include "module.h"
#include "tags.h"
#include "encoder.h"
#include "decoder.h"


// Some notes on conventions in this code. All methods conform to a couple of
// return styles:
//
// * PyObject* (mostly for methods accessible from Python) in which case a
//   return value of NULL indicates an error, or
//
// * int (mostly for internal methods) in which case 0 indicates success and -1
//   an error. This is in keeping with most of Python's C-API.
//
// In an attempt to avoid leaks a particular coding style is used where
// possible:
//
// 1. As soon as a new reference to an object is generated / returned, a
//    block like this follows: if (ref) { ... Py_DECREF(ref); }
//
// 2. The result is calculated in the "ret" local and returned only at the
//    end of the function, once we're sure all references have been accounted
//    for.
//
// 3. No "return" is permitted before the end of the function, and "break" or
//    "goto" should be used over a minimal distance to ensure Py_DECREFs aren't
//    jumped over.
//
// 4. Wherever possible, functions that return a PyObject pointer return a
//    *new* reference (like the majority of the CPython API) as opposed to
//    a borrowed reference.
//
// 5. The above rules are broken occasionally where necessary for clarity :)
//
// While this style helps ensure fewer leaks, it's worth noting it results in
// rather "nested" code which looks a bit unusual / ugly for C. Furthermore,
// it's not fool-proof; there's probably some leaks left. Please file bugs for
// any leaks you detect!


// break_marker singleton ////////////////////////////////////////////////////

static PyObject *
break_marker_repr(PyObject *op)
{
    return PyUnicode_FromString("break_marker");
}

static void
break_marker_dealloc(PyObject *ignore)
{
    Py_FatalError("deallocating break_marker");
}

static PyObject *
break_marker_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    if (PyTuple_GET_SIZE(args) || (kwargs && PyDict_Size(kwargs))) {
        PyErr_SetString(PyExc_TypeError, "break_marker_type takes no arguments");
        return NULL;
    }
    Py_INCREF(break_marker);
    return break_marker;
}

static int
break_marker_bool(PyObject *v)
{
    return 1;
}

static PyNumberMethods break_marker_as_number = {
    .nb_bool = (inquiry) break_marker_bool,
};

PyTypeObject break_marker_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "break_marker_type",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = break_marker_new,
    .tp_dealloc = break_marker_dealloc,
    .tp_repr = break_marker_repr,
    .tp_as_number = &break_marker_as_number,
};

PyObject _break_marker_obj = {
    _PyObject_EXTRA_INIT
    1, &break_marker_type
};


// undefined singleton ///////////////////////////////////////////////////////

static PyObject *
undefined_repr(PyObject *op)
{
    return PyUnicode_FromString("undefined");
}

static void
undefined_dealloc(PyObject *ignore)
{
    Py_FatalError("deallocating undefined");
}

static PyObject *
undefined_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    if (PyTuple_GET_SIZE(args) || (kwargs && PyDict_Size(kwargs))) {
        PyErr_SetString(PyExc_TypeError, "undefined_type takes no arguments");
        return NULL;
    }
    Py_INCREF(undefined);
    return undefined;
}

static int
undefined_bool(PyObject *v)
{
    return 0;
}

static PyNumberMethods undefined_as_number = {
    .nb_bool = (inquiry) undefined_bool,
};

PyTypeObject undefined_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "undefined_type",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = undefined_new,
    .tp_dealloc = undefined_dealloc,
    .tp_repr = undefined_repr,
    .tp_as_number = &undefined_as_number,
};

PyObject _undefined_obj = {
    _PyObject_EXTRA_INIT
    1, &undefined_type
};


// CBORSimpleValue namedtuple ////////////////////////////////////////////////

PyTypeObject CBORSimpleValueType;

static PyStructSequence_Field CBORSimpleValueFields[] = {
    {.name = "value"},
    {NULL},
};

static PyStructSequence_Desc CBORSimpleValueDesc = {
    .name = "CBORSimpleValue",
    .doc = NULL,  // TODO
    .fields = CBORSimpleValueFields,
    .n_in_sequence = 1,
};

static PyObject *
CBORSimpleValue_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {"value", NULL};
    PyObject *value = NULL, *ret;
    uint8_t val;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "B", keywords, &val))
        return NULL;

    ret = PyStructSequence_New(type);
    if (ret) {
        value = PyLong_FromLong(val);
        if (value)
            PyStructSequence_SET_ITEM(ret, 0, value);
    }
    return ret;
}


// dump/load functions ///////////////////////////////////////////////////////

static PyObject *
CBOAR_dump(PyObject *module, PyObject *args, PyObject *kwargs)
{
    PyObject *obj, *ret = NULL;
    CBOREncoderObject *self;
    bool decref_args = false;

    if (PyTuple_GET_SIZE(args) == 0) {
        obj = PyDict_GetItem(kwargs, _CBOAR_str_obj);
        if (!obj) {
            PyErr_SetString(PyExc_TypeError,
                    "dump missing 1 required argument: 'obj'");
            return NULL;
        }
        Py_INCREF(obj);
        if (PyDict_DelItem(kwargs, _CBOAR_str_obj) == -1) {
            Py_DECREF(obj);
            return NULL;
        }
    } else {
        obj = PyTuple_GET_ITEM(args, 0);
        args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
        if (!args)
            return NULL;
        Py_INCREF(obj);
        decref_args = true;
    }

    self = (CBOREncoderObject *)CBOREncoder_new(&CBOREncoderType, NULL, NULL);
    if (self) {
        if (CBOREncoder_init(self, args, kwargs) == 0) {
            ret = CBOREncoder_encode(self, obj);
        }
        Py_DECREF(self);
    }
    Py_DECREF(obj);
    if (decref_args)
        Py_DECREF(args);
    return ret;
}


static PyObject *
CBOAR_load(PyObject *module, PyObject *args, PyObject *kwargs)
{
    PyObject *ret = NULL;
    CBORDecoderObject *self;

    self = (CBORDecoderObject *)CBORDecoder_new(&CBORDecoderType, NULL, NULL);
    if (self) {
        if (CBORDecoder_init(self, args, kwargs) == 0) {
            ret = CBORDecoder_decode(self);
        }
        Py_DECREF(self);
    }
    return ret;
}


static PyObject *
CBOAR_loads(PyObject *module, PyObject *args, PyObject *kwargs)
{
    PyObject *new_args, *buf, *stream, *ret = NULL;
    Py_ssize_t i;

    if (!_CBOAR_BytesIO && _CBOAR_init_BytesIO() == -1)
        return NULL;

    if (PyTuple_GET_SIZE(args) == 0) {
        buf = PyDict_GetItem(kwargs, _CBOAR_str_buf);
        if (!buf) {
            PyErr_SetString(PyExc_TypeError,
                    "dump missing 1 required argument: 'buf'");
            return NULL;
        }
        Py_INCREF(buf);
        if (PyDict_DelItem(kwargs, _CBOAR_str_buf) == -1)
            goto error;
        new_args = PyTuple_New(PyTuple_GET_SIZE(args) + 1);
        if (!new_args)
            goto error;
        for (i = 0; i < PyTuple_GET_SIZE(args); ++i) {
            // inc. ref because PyTuple_SET_ITEM steals a ref
            Py_INCREF(PyTuple_GET_ITEM(args, i));
            PyTuple_SET_ITEM(new_args, i + 1, PyTuple_GET_ITEM(args, i));
        }
    } else {
        buf = PyTuple_GET_ITEM(args, 0);
        Py_INCREF(buf);
        new_args = PyTuple_New(PyTuple_GET_SIZE(args));
        if (!new_args)
            goto error;
        for (i = 1; i < PyTuple_GET_SIZE(args); ++i) {
            // inc. ref because PyTuple_SET_ITEM steals a ref
            Py_INCREF(PyTuple_GET_ITEM(args, i));
            PyTuple_SET_ITEM(new_args, i, PyTuple_GET_ITEM(args, i));
        }
    }

    stream = PyObject_CallFunctionObjArgs(_CBOAR_BytesIO, buf, NULL);
    if (stream) {
        PyTuple_SET_ITEM(new_args, 0, stream);
        ret = CBOAR_load(module, new_args, kwargs);
        // no need to dec. ref stream here because SET_ITEM above stole the ref
    }
    Py_DECREF(new_args);
    return ret;
error:
    Py_DECREF(buf);
    return NULL;
}


// Cache-init functions //////////////////////////////////////////////////////

int
_CBOAR_init_BytesIO(void)
{
    PyObject *io;

    // from io import BytesIO
    io = PyImport_ImportModule("io");
    if (!io)
        goto error;
    _CBOAR_BytesIO = PyObject_GetAttr(io, _CBOAR_str_BytesIO);
    Py_DECREF(io);
    if (!_CBOAR_BytesIO)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError,
            "unable to import BytesIO from io");
    return -1;
}

int
_CBOAR_init_OrderedDict(void)
{
    PyObject *collections;

    // from collections import OrderedDict
    collections = PyImport_ImportModule("collections");
    if (!collections)
        goto error;
    _CBOAR_OrderedDict = PyObject_GetAttr(collections, _CBOAR_str_OrderedDict);
    Py_DECREF(collections);
    if (!_CBOAR_OrderedDict)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError,
            "unable to import OrderedDict from collections");
    return -1;
}


int
_CBOAR_init_Decimal(void)
{
    PyObject *decimal;

    // from decimal import Decimal
    decimal = PyImport_ImportModule("decimal");
    if (!decimal)
        goto error;
    _CBOAR_Decimal = PyObject_GetAttr(decimal, _CBOAR_str_Decimal);
    Py_DECREF(decimal);
    if (!_CBOAR_Decimal)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import Decimal from decimal");
    return -1;
}


int
_CBOAR_init_Fraction(void)
{
    PyObject *fractions;

    // from fractions import Fraction
    fractions = PyImport_ImportModule("fractions");
    if (!fractions)
        goto error;
    _CBOAR_Fraction = PyObject_GetAttr(fractions, _CBOAR_str_Fraction);
    Py_DECREF(fractions);
    if (!_CBOAR_Fraction)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import Fraction from fractions");
    return -1;
}


int
_CBOAR_init_UUID(void)
{
    PyObject *uuid;

    // from uuid import UUID
    uuid = PyImport_ImportModule("uuid");
    if (!uuid)
        goto error;
    _CBOAR_UUID = PyObject_GetAttr(uuid, _CBOAR_str_UUID);
    Py_DECREF(uuid);
    if (!_CBOAR_UUID)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import UUID from uuid");
    return -1;
}


int
_CBOAR_init_re_compile(void)
{
    PyObject *re;

    // import re
    // datestr_re = re.compile("long-date-time-regex...")
    re = PyImport_ImportModule("re");
    if (!re)
        goto error;
    _CBOAR_re_compile = PyObject_GetAttr(re, _CBOAR_str_compile);
    Py_DECREF(re);
    if (!_CBOAR_re_compile)
        goto error;
    _CBOAR_datestr_re = PyObject_CallFunctionObjArgs(
            _CBOAR_re_compile, _CBOAR_str_datestr_re, NULL);
    if (!_CBOAR_datestr_re)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import compile from re");
    return -1;
}


int
_CBOAR_init_timezone_utc(void)
{
    PyObject *datetime;

#if PY_VERSION_HEX >= 0x03070000
    Py_INCREF(PyDateTime_TimeZone_UTC);
    _CBOAR_timezone_utc = PyDateTime_TimeZone_UTC;
    _CBOAR_timezone = NULL;
#else
    // from datetime import timezone
    // utc = timezone.utc
    datetime = PyImport_ImportModule("datetime");
    if (!datetime)
        goto error;
    _CBOAR_timezone = PyObject_GetAttr(datetime, _CBOAR_str_timezone);
    Py_DECREF(datetime);
    if (!_CBOAR_timezone)
        goto error;
    _CBOAR_timezone_utc = PyObject_GetAttr(_CBOAR_timezone, _CBOAR_str_utc);
    if (!_CBOAR_timezone_utc)
        goto error;
#endif
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import timezone from datetime");
    return -1;
}


int
_CBOAR_init_Parser(void)
{
    PyObject *parser;

    // from email.parser import Parser
    parser = PyImport_ImportModule("email.parser");
    if (!parser)
        goto error;
    _CBOAR_Parser = PyObject_GetAttr(parser, _CBOAR_str_Parser);
    Py_DECREF(parser);
    if (!_CBOAR_Parser)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import Parser from email.parser");
    return -1;
}


int
_CBOAR_init_ip_address(void)
{
    PyObject *ipaddress;

    // from ipaddress import ip_address
    ipaddress = PyImport_ImportModule("ipaddress");
    if (!ipaddress)
        goto error;
    _CBOAR_ip_address = PyObject_GetAttr(ipaddress, _CBOAR_str_ip_address);
    Py_DECREF(ipaddress);
    if (!_CBOAR_ip_address)
        goto error;
    return 0;
error:
    PyErr_SetString(PyExc_ImportError, "unable to import ip_address from ipaddress");
    return -1;
}


// Module definition /////////////////////////////////////////////////////////

PyObject *_CBOAR_empty_bytes = NULL;
PyObject *_CBOAR_empty_str = NULL;
PyObject *_CBOAR_str_as_string = NULL;
PyObject *_CBOAR_str_as_tuple = NULL;
PyObject *_CBOAR_str_bit_length = NULL;
PyObject *_CBOAR_str_buf = NULL;
PyObject *_CBOAR_str_bytes = NULL;
PyObject *_CBOAR_str_BytesIO = NULL;
PyObject *_CBOAR_str_compile = NULL;
PyObject *_CBOAR_str_datestr_re = NULL;
PyObject *_CBOAR_str_Decimal = NULL;
PyObject *_CBOAR_str_denominator = NULL;
PyObject *_CBOAR_str_Fraction = NULL;
PyObject *_CBOAR_str_fromtimestamp = NULL;
PyObject *_CBOAR_str_getvalue = NULL;
PyObject *_CBOAR_str_groups = NULL;
PyObject *_CBOAR_str_ip_address = NULL;
PyObject *_CBOAR_str_is_infinite = NULL;
PyObject *_CBOAR_str_is_nan = NULL;
PyObject *_CBOAR_str_isoformat = NULL;
PyObject *_CBOAR_str_join = NULL;
PyObject *_CBOAR_str_match = NULL;
PyObject *_CBOAR_str_numerator = NULL;
PyObject *_CBOAR_str_obj = NULL;
PyObject *_CBOAR_str_OrderedDict = NULL;
PyObject *_CBOAR_str_packed = NULL;
PyObject *_CBOAR_str_Parser = NULL;
PyObject *_CBOAR_str_parsestr = NULL;
PyObject *_CBOAR_str_pattern = NULL;
PyObject *_CBOAR_str_read = NULL;
PyObject *_CBOAR_str_timestamp = NULL;
PyObject *_CBOAR_str_timezone = NULL;
PyObject *_CBOAR_str_utc = NULL;
PyObject *_CBOAR_str_utc_suffix = NULL;
PyObject *_CBOAR_str_UUID = NULL;
PyObject *_CBOAR_str_write = NULL;

PyObject *_CBOAR_timezone = NULL;
PyObject *_CBOAR_timezone_utc = NULL;
PyObject *_CBOAR_BytesIO = NULL;
PyObject *_CBOAR_OrderedDict = NULL;
PyObject *_CBOAR_Decimal = NULL;
PyObject *_CBOAR_Fraction = NULL;
PyObject *_CBOAR_UUID = NULL;
PyObject *_CBOAR_Parser = NULL;
PyObject *_CBOAR_re_compile = NULL;
PyObject *_CBOAR_datestr_re = NULL;
PyObject *_CBOAR_ip_address = NULL;

static void
cboar_free(PyObject *m)
{
    Py_CLEAR(_CBOAR_timezone_utc);
    Py_CLEAR(_CBOAR_timezone);
    Py_CLEAR(_CBOAR_BytesIO);
    Py_CLEAR(_CBOAR_OrderedDict);
    Py_CLEAR(_CBOAR_Decimal);
    Py_CLEAR(_CBOAR_Fraction);
    Py_CLEAR(_CBOAR_UUID);
    Py_CLEAR(_CBOAR_Parser);
    Py_CLEAR(_CBOAR_re_compile);
    Py_CLEAR(_CBOAR_datestr_re);
    Py_CLEAR(_CBOAR_ip_address);
}

static PyMethodDef _cboarmethods[] = {
    {"dump", (PyCFunction) CBOAR_dump, METH_VARARGS | METH_KEYWORDS,
        "encode a value to the stream"},
    {"load", (PyCFunction) CBOAR_load, METH_VARARGS | METH_KEYWORDS,
        "decode a value from the stream"},
    {"loads", (PyCFunction) CBOAR_loads, METH_VARARGS | METH_KEYWORDS,
        "decode a value from a byte-string"},
    {NULL}
};

PyDoc_STRVAR(_cboar__doc__,
"The _cboar module is the C-extension backing the cboar Python module. It\n"
"defines the base CBOREncoder, CBORDecoder, CBORTag, and undefined types\n"
"which are operational in and of themselves, but lacks the standard load\n"
"and dump routines which form the usual public interface. These are added\n"
"by the cboar module which imports the _cboar module.\n"
);

static struct PyModuleDef _cboarmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_cboar",
    .m_doc = _cboar__doc__,
    .m_size = -1,
    .m_free = (freefunc) cboar_free,
    .m_methods = _cboarmethods,
};

PyMODINIT_FUNC
PyInit__cboar(void)
{
    PyObject *module;

    PyDateTime_IMPORT;
    if (!PyDateTimeAPI)
        return NULL;

    if (PyType_Ready(&CBORTagType) < 0)
        return NULL;
    if (PyType_Ready(&CBOREncoderType) < 0)
        return NULL;
    if (PyType_Ready(&CBORDecoderType) < 0)
        return NULL;

    module = PyModule_Create(&_cboarmodule);
    if (!module)
        return NULL;

#if PY_VERSION_HEX >= 0x03040000
    // Use PyStructSequence_InitType2 to workaround #34784 (dup of #28709)
    if (PyStructSequence_InitType2(&CBORSimpleValueType, &CBORSimpleValueDesc) == -1)
        goto error;
#else
    // PyStructSequence_InitType2 was added in 3.4
    PyStructSequence_InitType(&CBORSimpleValueType, &CBORSimpleValueDesc);
#endif
    Py_INCREF((PyObject *) &CBORSimpleValueType);
    CBORSimpleValueType.tp_new = CBORSimpleValue_new;
    if (PyModule_AddObject(
            module, "CBORSimpleValue", (PyObject *) &CBORSimpleValueType) == -1)
        goto error;

    Py_INCREF(&CBORTagType);
    if (PyModule_AddObject(module, "CBORTag", (PyObject *) &CBORTagType) == -1)
        goto error;

    Py_INCREF(&CBOREncoderType);
    if (PyModule_AddObject(module, "CBOREncoder", (PyObject *) &CBOREncoderType) == -1)
        goto error;

    Py_INCREF(&CBORDecoderType);
    if (PyModule_AddObject(module, "CBORDecoder", (PyObject *) &CBORDecoderType) == -1)
        goto error;

    Py_INCREF(break_marker);
    if (PyModule_AddObject(module, "break_marker", break_marker) == -1)
        goto error;

    Py_INCREF(undefined);
    if (PyModule_AddObject(module, "undefined", undefined) == -1)
        goto error;

#define INTERN_STRING(name)                                           \
    if (!_CBOAR_str_##name &&                                         \
            !(_CBOAR_str_##name = PyUnicode_InternFromString(#name))) \
        goto error;

    INTERN_STRING(as_string);
    INTERN_STRING(as_tuple);
    INTERN_STRING(bit_length);
    INTERN_STRING(buf);
    INTERN_STRING(bytes);
    INTERN_STRING(BytesIO);
    INTERN_STRING(compile);
    INTERN_STRING(Decimal);
    INTERN_STRING(denominator);
    INTERN_STRING(Fraction);
    INTERN_STRING(fromtimestamp);
    INTERN_STRING(getvalue);
    INTERN_STRING(groups);
    INTERN_STRING(ip_address);
    INTERN_STRING(is_infinite);
    INTERN_STRING(is_nan);
    INTERN_STRING(isoformat);
    INTERN_STRING(join);
    INTERN_STRING(match);
    INTERN_STRING(numerator);
    INTERN_STRING(obj);
    INTERN_STRING(OrderedDict);
    INTERN_STRING(packed);
    INTERN_STRING(Parser);
    INTERN_STRING(parsestr);
    INTERN_STRING(pattern);
    INTERN_STRING(read);
    INTERN_STRING(timestamp);
    INTERN_STRING(timezone);
    INTERN_STRING(utc);
    INTERN_STRING(UUID);
    INTERN_STRING(write);

#undef INTERN_STRING

    if (!_CBOAR_str_utc_suffix &&
            !(_CBOAR_str_utc_suffix = PyUnicode_InternFromString("+00:00")))
        goto error;
    if (!_CBOAR_str_datestr_re &&
            !(_CBOAR_str_datestr_re = PyUnicode_InternFromString(
                    "^(\\d{4})-(\\d\\d)-(\\d\\d)T"     // Y-m-d
                    "(\\d\\d):(\\d\\d):(\\d\\d)"       // H:M:S
                    "(?:\\.(\\d+))?"                   // .uS
                    "(?:Z|([+-]\\d\\d):(\\d\\d))$")))  // +-TZ
        goto error;
    if (!_CBOAR_empty_bytes &&
            !(_CBOAR_empty_bytes = PyBytes_FromStringAndSize(NULL, 0)))
        goto error;
    if (!_CBOAR_empty_str &&
            !(_CBOAR_empty_str = PyUnicode_FromStringAndSize(NULL, 0)))
        goto error;

    return module;
error:
    Py_DECREF(module);
    return NULL;
}
