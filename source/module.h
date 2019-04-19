#include <Python.h>

// break_marker singleton
extern PyObject _break_marker_obj;
#define break_marker (&_break_marker_obj)

// CBORTag namedtuple type
extern PyTypeObject CBORTagType;

// Various interned strings
extern PyObject *_CBOAR_empty_bytes;
extern PyObject *_CBOAR_empty_str;
extern PyObject *_CBOAR_str_as_string;
extern PyObject *_CBOAR_str_as_tuple;
extern PyObject *_CBOAR_str_bit_length;
extern PyObject *_CBOAR_str_bytes;
extern PyObject *_CBOAR_str_denominator;
extern PyObject *_CBOAR_str_is_infinite;
extern PyObject *_CBOAR_str_is_nan;
extern PyObject *_CBOAR_str_isoformat;
extern PyObject *_CBOAR_str_join;
extern PyObject *_CBOAR_str_numerator;
extern PyObject *_CBOAR_str_OrderedDict;
extern PyObject *_CBOAR_str_pattern;
extern PyObject *_CBOAR_str_read;
extern PyObject *_CBOAR_str_timestamp;
extern PyObject *_CBOAR_str_utc_suffix;
extern PyObject *_CBOAR_str_write;