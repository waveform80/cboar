import re
from collections import defaultdict, OrderedDict
from datetime import datetime, date

from _cboar import Encoder as BaseEncoder

default_encoders = [
    (bytes,     BaseEncoder.encode_bytes),
    (bytearray, BaseEncoder.encode_bytearray),
    (str,       BaseEncoder.encode_string),
    (int,       BaseEncoder.encode_int),
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
