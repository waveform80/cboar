import io
import re
import json
import cbor
import cbor2
import cboar
import timeit
from math import log2, ceil
from datetime import datetime, timezone
from fractions import Fraction
from decimal import Decimal
from collections import namedtuple, OrderedDict


UTC = timezone.utc

TEST_VALUES = [
    # label,            kwargs, value
    ('None',            {},     None),
    ('10e0',            {},     1),
    ('10e12',           {},     1000000000000),
    ('10e29',           {},     100000000000000000000000000000),
    ('-10e0',           {},     -1),
    ('-10e12',          {},     -1000000000000),
    ('-10e29',          {},     -100000000000000000000000000000),
    ('float1',          {},     1.0),
    ('float2',          {},     3.8),
    ('str',             {},     'foo'),
    ('bigstr',          {},     'foobarbaz ' * 1000),
    ('bytes',           {},     b'foo'),
    ('bigbytes',        {},     b'foobarbaz\x00' * 1000),
    ('datetime',        {'timezone': UTC}, datetime(2019, 5, 9, 22, 4, 5, 123456)),
    ('decimal',         {},     Decimal('1.1')),
    ('fraction',        {},     Fraction(1, 5)),
    ('intlist',         {},     [1, 2, 3]),
    ('bigintlist',      {},     [1, 2, 3] * 1000),
    ('strlist',         {},     ['foo', 'bar',  'baz']),
    ('bigstrlist',      {},     ['foo', 'bar',  'baz'] * 1000),
    ('dict',            {},     {'a': 1, 'b': 2, 'c': 3}),
    ('bigdict',         {},     {'a' * i: i for i in range(1000)}),
    ('set',             {},     {1, 2, 3}),
    ('bigset',          {},     set(range(1000))),
    ('bigdictlist',     {},     [{'a' * i: i for i in range(100)}] * 100),
    ('objectdict',      {'timezone': UTC},
     {'name': 'Foo', 'species': 'cat', 'dob': datetime(2013, 5, 20), 'weight': 4.1}),
    ('objectdictlist',  {'timezone': UTC},
     [{'name': 'Foo', 'species': 'cat', 'dob': datetime(2013, 5, 20), 'weight': 4.1}] * 100),
]


Codec = namedtuple('Codec', ('cbor', 'cbor2', 'cboar'))
Result = namedtuple('Result', ('encoding', 'decoding'))
Timing = namedtuple('Timing', ('time', 'repeat', 'count'))


def autorange(routine, *args, limit=0.2, **kwargs):
    # Adapted from the Python 3.7 version of timeit
    t = timeit.Timer(lambda: routine(*args, **kwargs))
    i = 1
    while True:
        for j in 1, 2, 5:
            number = i * j
            time_taken = t.timeit(number)
            if time_taken >= limit:
                return number
        i *= 10


def time(routine, *args, repeat=3, **kwargs):
    try:
        number = autorange(routine, *args, limit=0.02, **kwargs)
    except Exception as e:
        return e
    t = timeit.Timer(lambda: routine(*args, **kwargs))
    return Timing(min(t.repeat(repeat, number)) / number, repeat, number)


def format_time(t, suffixes=('s', 'ms', 'µs', 'ns'), zero='0s',
                template='{time:.1f}{suffix}'):
    if isinstance(t, Exception):
        return '-'
    else:
        try:
            index = min(len(suffixes) - 1, ceil(log2(1/t.time) / 10))
        except ValueError:
            return zero
        else:
            return template.format(time=t.time * 2 ** (index * 10),
                                   suffix=suffixes[index])


def print_len(s):
    return len(re.sub(r'\x1b\[.*?m', '', s))


RED = '\x1b[1;31m'
GREEN = '\x1b[1;32m'
RESET = '\x1b[0m'
def color_time(t, lim):
    time_str = format_time(t)
    if isinstance(t, Exception):
        return RED + time_str + RESET
    elif t.time <= lim.time * 0.8:
        return GREEN + time_str + RESET
    elif t.time > lim.time * 1.05:
        return RED + time_str + RESET
    else:
        return time_str


def main():
    results = OrderedDict()
    print("Testing", end="", flush=True)
    for name, kwargs, value in TEST_VALUES:
        encoded = cbor2.dumps(value, **kwargs)
        results[name] = Codec(**{
            mod.__name__: Result(
                encoding=time(mod.dumps, value, **kwargs),
                decoding=time(mod.loads, encoded)
            )
            for mod in (cbor, cbor2, cboar)
        })
        print(".", end="", flush=True)
    print()
    print()

    # Build table content
    head = ('Test',) + ('cbor', 'cboar', 'cbor2') * 2
    rows = [head] + [
        (
            value,
            format_time(result.cbor.encoding),
            color_time(result.cboar.encoding, result.cbor2.encoding),
            format_time(result.cbor2.encoding),
            format_time(result.cbor.decoding),
            color_time(result.cboar.decoding, result.cbor2.decoding),
            format_time(result.cbor2.decoding),
        )
        for value, result in results.items()
    ]

    # Format table output
    cols = zip(*rows)
    col_widths = [max(print_len(row) for row in col) for col in cols]
    sep = ''.join((
        '+-',
        '-+-'.join('-' * width for width in col_widths),
        '-+',
    ))
    print(''.join((
        '  ',
        ' ' * col_widths[0],
        ' +-',
        '-' * (sum(col_widths[1:4]) + 6),
        '-+-',
        '-' * (sum(col_widths[4:7]) + 6),
        '-+',
    )))
    print(''.join((
        '  ',
        ' ' * col_widths[0],
        ' | ',
        '{value:^{width}}'.format(value='Encoding', width=sum(col_widths[1:4]) + 6),
        ' | ',
        '{value:^{width}}'.format(value='Decoding', width=sum(col_widths[4:7]) + 6),
        ' |',
    )))
    print(sep)
    print(''.join((
        '| ',
        ' | '.join(
            '{value:<{width}}'.format(value=value, width=width)
            for value, width in zip(head, col_widths)
        ),
        ' |',
    )))
    print(sep)
    for row in rows[1:]:
        print(''.join((
            '| ',
            ' | '.join(
                '{value:<{width}}'.format(
                    value=value, width=width + len(value) - print_len(value))
                for value, width in zip(row, col_widths)
            ),
            ' |',
        )))
    print(sep)


if __name__ == '__main__':
    main()
