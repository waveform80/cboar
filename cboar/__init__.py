import io
import re
from functools import wraps
from collections import defaultdict, OrderedDict, namedtuple
from datetime import datetime, date

import _cboar
from _cboar import (
    CBORDecoder,
    CBORTag,
    CBORSimpleValue,
    undefined,
    break_marker,
)


default_encoders = [
    # The following encoders are effectively hard-coded in the C-based class;
    # you can't override them by adjusting these definitions (or by adjusting
    # the CBOREncoder.encoders OrderedDict). If you wish to override them, pass
    # 2 as the value of the "canonical" parameter in the constructor. This
    # disables the hard-coded lookup (with some speed cost) but allows full
    # customization of the encoders
    (bytes,                           _cboar.CBOREncoder.encode_bytes),
    (bytearray,                       _cboar.CBOREncoder.encode_bytearray),
    (str,                             _cboar.CBOREncoder.encode_string),
    (int,                             _cboar.CBOREncoder.encode_int),
    (float,                           _cboar.CBOREncoder.encode_float),
    (bool,                            _cboar.CBOREncoder.encode_boolean),
    (type(None),                      _cboar.CBOREncoder.encode_none),
    (tuple,                           _cboar.CBOREncoder.encode_array),
    (list,                            _cboar.CBOREncoder.encode_array),
    (dict,                            _cboar.CBOREncoder.encode_map),
    (datetime,                        _cboar.CBOREncoder.encode_datetime),
    (date,                            _cboar.CBOREncoder.encode_date),
    (set,                             _cboar.CBOREncoder.encode_set),
    (frozenset,                       _cboar.CBOREncoder.encode_set),
    # Everything from here is always looked up from CBOREncoder.encoders, and
    # is therefore customizable by default
    (('decimal', 'Decimal'),          _cboar.CBOREncoder.encode_decimal),
    (('fractions', 'Fraction'),       _cboar.CBOREncoder.encode_rational),
    (defaultdict,                     _cboar.CBOREncoder.encode_map),
    (OrderedDict,                     _cboar.CBOREncoder.encode_map),
    (type(undefined),                 _cboar.CBOREncoder.encode_undefined),
    (type(re.compile('')),            _cboar.CBOREncoder.encode_regex),
    (('email.message', 'Message'),    _cboar.CBOREncoder.encode_mime),
    (('uuid', 'UUID'),                _cboar.CBOREncoder.encode_uuid),
    (('ipaddress', 'IPv4Address'),    _cboar.CBOREncoder.encode_ipaddress),
    (('ipaddress', 'IPv6Address'),    _cboar.CBOREncoder.encode_ipaddress),
    (CBORTag,                         _cboar.CBOREncoder.encode_semantic),
    (CBORSimpleValue,                 _cboar.CBOREncoder.encode_simple),
]


canonical_encoders = [
    # The same warning applies to the canonical encoders; these are hard-coded
    # in the C-class unless 2 is passed as the value of the "canonical"
    # parameter in which case they can be customized
    (float,                           _cboar.CBOREncoder.encode_minimal_float),
    (dict,                            _cboar.CBOREncoder.encode_canonical_map),
    (set,                             _cboar.CBOREncoder.encode_canonical_set),
    (frozenset,                       _cboar.CBOREncoder.encode_canonical_set),
    (OrderedDict,                     _cboar.CBOREncoder.encode_canonical_map),
]


def shareable_encoder(func):
    @wraps(func)
    def wrapper(encoder, value):
        encoder.encode_shared(func, value)
    return wrapper


class CBOREncoder(_cboar.CBOREncoder):
    def __init__(self, fp, datetime_as_timestamp=False, timezone=None,
                 value_sharing=False, default=None, canonical=False):
        super().__init__(fp, datetime_as_timestamp, timezone, value_sharing,
                         default, canonical)
        self.encoders.update(default_encoders)
        if canonical:
            self.encoders.update(canonical_encoders)


def dumps(obj, **kwargs):
    with io.BytesIO() as fp:
        dump(obj, fp, **kwargs)
        return fp.getvalue()


def dump(obj, fp, **kwargs):
    return CBOREncoder(fp, **kwargs).encode(obj)


def loads(buf, **kwargs):
    with io.BytesIO(buf) as fp:
        return load(fp, **kwargs)


def load(fp, **kwargs):
    return CBORDecoder(fp, **kwargs).decode()
