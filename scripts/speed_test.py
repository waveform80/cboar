import io
import re
import json
import cbor
import cbor2
import cboar
import timeit
from math import log2, ceil
from collections import namedtuple, OrderedDict


TEST_VALUES = OrderedDict([
    ('None',        None),
    ('False',       False),
    ('True',        True),
    ('10e0',        1),
    ('10e2',        100),
    ('10e3',        1000),
    ('10e5',        100000),
    ('10e12',       1000000000000),
    ('10e29',       100000000000000000000000000000),
    ('-10e0',       -1),
    ('-10e2',       -100),
    ('-10e3',       -1000),
    ('-10e5',       -100000),
    ('-10e12',      -1000000000000),
    ('-10e29',      -100000000000000000000000000000),
    ('float1',      1.0),
    ('float2',      3.8),
    ('str',         'foo'),
    ('bigstr',      'foobarbaz ' * 1000),
    ('bytes',       b'foo'),
    ('bigbytes',    b'foobarbaz\x00' * 1000),
    ('intlist',     [1, 2, 3]),
    ('bigintlist',  [1, 2, 3] * 1000),
    ('strlist',     ['foo', 'bar',  'baz']),
    ('bigstrlist',  ['foo', 'bar',  'baz'] * 1000),
    ('dict',        {'a': 1, 'b': 2, 'c': 3}),
    ('bigdict',     {'a' * i: i for i in range(1000)}),
    ('bigdictlist', {'a' * i: ['foo', 'bar',  'baz'] * i for i in range(100)}),
])


Codec = namedtuple('Codec', ('cbor', 'cbor2', 'cboar'))
Result = namedtuple('Result', ('encoding', 'decoding'))
Timing = namedtuple('Timing', ('time', 'repeat', 'count'))


def autorange(routine, value, limit=0.2):
    # Adapted from the Python 3.7 version of timeit
    t = timeit.Timer(lambda: routine(value))
    i = 1
    while True:
        for j in 1, 2, 5:
            number = i * j
            time_taken = t.timeit(number)
            if time_taken >= limit:
                return number
        i *= 10


def time(routine, value, repeat=3):
    num = autorange(routine, value, limit=0.02)
    t = timeit.Timer(lambda: routine(value))
    return Timing(min(t.repeat(repeat, num)) / num, repeat, num)


def format_time(t, suffixes=('s', 'ms', 'µs', 'ns'), zero='0s',
                template='{time:.1f}{suffix}'):
    try:
        index = min(len(suffixes) - 1, ceil(log2(1/t) / 10))
    except ValueError:
        return zero
    else:
        return template.format(time=t * 2 ** (index * 10),
                               suffix=suffixes[index])


def print_len(s):
    return len(re.sub(r'\x1b\[.*?m', '', s))


RED = '\x1b[1;31m'
GREEN = '\x1b[1;32m'
RESET = '\x1b[0m'
def color_time(t, lim):
    if t <= lim * 0.5:
        return GREEN + format_time(t) + RESET
    elif t > lim:
        return RED + format_time(t) + RESET
    else:
        return format_time(t)


def main():
    results = OrderedDict()
    print("Testing", end="", flush=True)
    for name, value in TEST_VALUES.items():
        encoded = cbor2.dumps(value)
        results[name] = Codec(**{
            mod.__name__: Result(
                encoding=time(mod.dumps, value),
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
            format_time(result.cbor.encoding.time),
            color_time(result.cboar.encoding.time, result.cbor2.encoding.time),
            format_time(result.cbor2.encoding.time),
            format_time(result.cbor.decoding.time),
            color_time(result.cboar.decoding.time, result.cbor2.decoding.time),
            format_time(result.cbor2.decoding.time),
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
