import io
import re
from collections import defaultdict, OrderedDict, namedtuple
from datetime import datetime, date

from _cboar import (
    Encoder,
    Decoder,
    Tag as CBORTag,
    CBORSimpleValue,
    undefined,
)


default_encoders = [
    # The following encoders are effectively hard-coded in the C-based class;
    # you can't override them by adjusting these definitions (or by adjusting
    # the Encoder.encoders OrderedDict).
    (bytes,                           Encoder.encode_bytes),
    (bytearray,                       Encoder.encode_bytearray),
    (str,                             Encoder.encode_string),
    (int,                             Encoder.encode_int),
    (float,                           Encoder.encode_float),
    (bool,                            Encoder.encode_boolean),
    (type(None),                      Encoder.encode_none),
    (tuple,                           Encoder.encode_array),
    (list,                            Encoder.encode_array),
    (dict,                            Encoder.encode_map),
    (datetime,                        Encoder.encode_datetime),
    (date,                            Encoder.encode_date),
    # Everything from here is looked up from Encoder.encoders, and is
    # therefore customizable
    (('decimal', 'Decimal'),          Encoder.encode_decimal),
    (('fractions', 'Fraction'),       Encoder.encode_rational),
    (defaultdict,                     Encoder.encode_map),
    (OrderedDict,                     Encoder.encode_map),
    (type(undefined),                 Encoder.encode_undefined),
    (type(re.compile('')),            Encoder.encode_regex),
    (('email.message', 'Message'),    Encoder.encode_mime),
    (('uuid', 'UUID'),                Encoder.encode_uuid),
    (CBORTag,                         Encoder.encode_semantic),
    (CBORSimpleValue,                 Encoder.encode_simple),
    (set,                             Encoder.encode_set),
    (frozenset,                       Encoder.encode_set),
]


canonical_encoders = [
    # TODO
]


class CBOREncoder(Encoder):
    def __init__(self, fp, datetime_as_timestamp=False, timezone=None,
                 value_sharing=False, default=None, canonical=False):
        super().__init__(fp, datetime_as_timestamp, timezone, value_sharing,
                         default)
        self.encoders.update(default_encoders)
        if canonical:
            self.encoders.update(canonical_encoders)

    def encode_to_bytes(self, obj):
        old_fp = self.fp
        self.fp = fp = io.BytesIO()
        try:
            self.encode(obj)
        finally:
            self.fp = old_fp
        return fp.getvalue()


class CBORDecoder(Decoder):
    def __init__(self, fp, tag_hook=None, object_hook=None, str_errors='strict'):
        super().__init__(fp, tag_hook, object_hook, str_errors)

    def decode_from_bytes(self, buf):
        old_fp = self.fp
        self.fp = io.BytesIO(buf)
        try:
            retval = self.decode()
        finally:
            self.fp = old_fp
        return retval


def dumps(obj, **kwargs):
    fp = io.BytesIO()
    dump(obj, fp, **kwargs)
    return fp.getvalue()


def dump(obj, fp, **kwargs):
    return CBOREncoder(fp, **kwargs).encode(obj)


def loads(buf, **kwargs):
    fp = io.BytesIO(buf)
    return load(fp, **kwargs)


def load(fp, **kwargs):
    return CBORDecoder(fp, **kwargs).decode()
