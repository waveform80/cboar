import io
import re
from collections import defaultdict, OrderedDict, namedtuple
from datetime import datetime, date

from _cboar import (
    Encoder as BaseEncoder,
    Decoder as BaseDecoder,
    CBORTag,
    CBORSimpleValue,
    undefined,
)


default_encoders = [
    # The following encoders are effectively hard-coded in the C-based class;
    # you can't override them by adjusting these definitions (or by adjusting
    # the BaseEncoder.encoders OrderedDict).
    (bytes,                           BaseEncoder.encode_bytes),
    (bytearray,                       BaseEncoder.encode_bytearray),
    (str,                             BaseEncoder.encode_string),
    (int,                             BaseEncoder.encode_int),
    (float,                           BaseEncoder.encode_float),
    (bool,                            BaseEncoder.encode_boolean),
    (type(None),                      BaseEncoder.encode_none),
    (tuple,                           BaseEncoder.encode_array),
    (list,                            BaseEncoder.encode_array),
    (dict,                            BaseEncoder.encode_map),
    (datetime,                        BaseEncoder.encode_datetime),
    (date,                            BaseEncoder.encode_date),
    # Everything from here is looked up from BaseEncoder.encoders, and is
    # therefore customizable
    (('decimal', 'Decimal'),          BaseEncoder.encode_decimal),
    (('fractions', 'Fraction'),       BaseEncoder.encode_rational),
    (defaultdict,                     BaseEncoder.encode_map),
    (OrderedDict,                     BaseEncoder.encode_map),
    (type(undefined),                 BaseEncoder.encode_undefined),
    (type(re.compile('')),            BaseEncoder.encode_regex),
    (('email.message', 'Message'),    BaseEncoder.encode_mime),
    (('uuid', 'UUID'),                BaseEncoder.encode_uuid),
    (CBORTag,                         BaseEncoder.encode_semantic),
    (CBORSimpleValue,                 BaseEncoder.encode_simple),
    (set,                             BaseEncoder.encode_set),
    (frozenset,                       BaseEncoder.encode_set),
]


canonical_encoders = [
    # TODO
]


class Encoder(BaseEncoder):
    def __init__(self, fp, datetime_as_timestamp=False, timezone=None,
                 value_sharing=False, default=None, canonical=False):
        super().__init__(fp, datetime_as_timestamp, timezone, value_sharing,
                         default)
        self.encoders.update(default_encoders)
        if canonical:
            self.encoders.update(canonical_encoders)


class Decoder(BaseDecoder):
    def __init__(self, fp, tag_hook=None, object_hook=None, str_errors='strict'):
        super().__init__(fp, tag_hook, object_hook, str_errors)


def dumps(obj, **kwargs):
    fp = io.BytesIO()
    dump(obj, fp, **kwargs)
    return fp.getvalue()


def dump(obj, fp, **kwargs):
    Encoder(fp, **kwargs).encode(obj)


def loads(buf, **kwargs):
    fp = io.BytesIO(buf)
    return load(fp)


def load(fp, **kwargs):
    return Decoder(fp, **kwargs).decode()
