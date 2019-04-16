import io
import re
from collections import defaultdict, OrderedDict, namedtuple
from datetime import datetime, date

from _cboar import Encoder as BaseEncoder


class UndefinedType:
    __slots__ = ()
    def __repr__(self):
        return "undefined"

undefined = UndefinedType()


CBORTag = namedtuple('CBORTag', ('tag', 'value'))
CBORSimpleValue = namedtuple('CBORSimpleValue', ('value',))


default_encoders = [
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
    (defaultdict,                     BaseEncoder.encode_map),
    (OrderedDict,                     BaseEncoder.encode_map),
    (type(undefined),                 BaseEncoder.encode_undefined),
    (type(re.compile('')),            BaseEncoder.encode_regex),
    (('fractions', 'Fraction'),       BaseEncoder.encode_rational),
    (('email.message', 'Message'),    BaseEncoder.encode_mime),
    (('uuid', 'UUID'),                BaseEncoder.encode_uuid),
    (CBORTag,                         BaseEncoder.encode_semantic),
    (CBORSimpleValue,                 BaseEncoder.encode_simple),
    (set,                             BaseEncoder.encode_set),
    (frozenset,                       BaseEncoder.encode_set),
]


canonical_encoders = [
]


class Encoder(BaseEncoder):
    def __init__(self, fp, default_handler=None, timestamp_format=0,
                 value_sharing=False, canonical=False):
        super().__init__(fp, default_handler, timestamp_format, value_sharing)
        self.encoders.update(default_encoders)
        if canonical:
            self.encoders.update(canonical_encoders)


def dumps(obj, **kwargs):
    fp = io.BytesIO()
    dump(obj, fp, **kwargs)
    return fp.getvalue()


def dump(obj, fp, **kwargs):
    Encoder(fp, **kwargs).encode(obj)
