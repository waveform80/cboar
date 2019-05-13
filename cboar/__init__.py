from functools import wraps

from _cboar import (
    default_encoders,
    canonical_encoders,
    CBORError,
    CBOREncodeError,
    CBORDecodeError,
    CBOREncoder,
    CBORDecoder,
    CBORTag,
    CBORSimpleValue,
    undefined,
    break_marker,
    dump,
    dumps,
    load,
    loads,
)

def shareable_encoder(func):
    @wraps(func)
    def wrapper(encoder, value):
        encoder.encode_shared(func, value)
    return wrapper
