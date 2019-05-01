#include <Python.h>
#if PY_MAJOR_VERSION < 3
#error "cboar doesn't support the Python 2.x API"
#elif PY_MAJOR_VERSION == 3 && PY_MAJOR_VERSION < 3
#error "cboar requires Python 3.3 or newer"
#endif

// structure of the lead-byte for all CBOR records
typedef
    union {
        struct {
            unsigned int subtype: 5;
            unsigned int major: 3;
        };
        char byte;
    } LeadByte;

// break_marker singleton
extern PyObject _break_marker_obj;
#define break_marker (&_break_marker_obj)
#define CBOAR_RETURN_BREAK return Py_INCREF(break_marker), break_marker

// undefined singleton
extern PyObject _undefined_obj;
#define undefined (&_undefined_obj)
#define CBOAR_RETURN_UNDEFINED return Py_INCREF(undefined), undefined

// CBORSimpleValue namedtuple type
extern PyTypeObject CBORSimpleValueType;

// Various interned strings
extern PyObject *_CBOAR_empty_bytes;
extern PyObject *_CBOAR_empty_str;
extern PyObject *_CBOAR_str_as_string;
extern PyObject *_CBOAR_str_as_tuple;
extern PyObject *_CBOAR_str_bit_length;
extern PyObject *_CBOAR_str_bytes;
extern PyObject *_CBOAR_str_BytesIO;
extern PyObject *_CBOAR_str_compile;
extern PyObject *_CBOAR_str_datestr_re;
extern PyObject *_CBOAR_str_Decimal;
extern PyObject *_CBOAR_str_denominator;
extern PyObject *_CBOAR_str_Fraction;
extern PyObject *_CBOAR_str_fromtimestamp;
extern PyObject *_CBOAR_str_getvalue;
extern PyObject *_CBOAR_str_groups;
extern PyObject *_CBOAR_str_is_infinite;
extern PyObject *_CBOAR_str_is_nan;
extern PyObject *_CBOAR_str_isoformat;
extern PyObject *_CBOAR_str_join;
extern PyObject *_CBOAR_str_match;
extern PyObject *_CBOAR_str_numerator;
extern PyObject *_CBOAR_str_OrderedDict;
extern PyObject *_CBOAR_str_Parser;
extern PyObject *_CBOAR_str_parsestr;
extern PyObject *_CBOAR_str_pattern;
extern PyObject *_CBOAR_str_read;
extern PyObject *_CBOAR_str_timestamp;
extern PyObject *_CBOAR_str_timezone;
extern PyObject *_CBOAR_str_utc;
extern PyObject *_CBOAR_str_utc_suffix;
extern PyObject *_CBOAR_str_UUID;
extern PyObject *_CBOAR_str_write;

// Global references (initialized by functions declared below)
extern PyObject *_CBOAR_timezone;
extern PyObject *_CBOAR_timezone_utc;
extern PyObject *_CBOAR_BytesIO;
extern PyObject *_CBOAR_OrderedDict;
extern PyObject *_CBOAR_Decimal;
extern PyObject *_CBOAR_Fraction;
extern PyObject *_CBOAR_UUID;
extern PyObject *_CBOAR_Parser;
extern PyObject *_CBOAR_re_compile;
extern PyObject *_CBOAR_datestr_re;

// Initializers for the cached references above
int _CBOAR_init_timezone_utc(void); // also handles timezone
int _CBOAR_init_BytesIO(void);
int _CBOAR_init_OrderedDict(void);
int _CBOAR_init_Decimal(void);
int _CBOAR_init_Fraction(void);
int _CBOAR_init_UUID(void);
int _CBOAR_init_Parser(void);
int _CBOAR_init_re_compile(void); // also handles datestr_re
