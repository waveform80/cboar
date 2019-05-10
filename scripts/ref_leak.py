#!/usr/bin/env python
import objgraph
import cbor2
import cboar
from datetime import datetime, timezone, timedelta
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

Leaks = namedtuple('Leaks', ('count', 'leaks'))
Result = namedtuple('Result', ('encoding', 'decoding'))


peak = {}
def growth():
    return objgraph.growth(limit=None, peak_stats=peak)


def test(op):
    count = 0
    start = datetime.now()
    growth()
    while True:
        count += 1
        op()
        if datetime.now() - start > timedelta(seconds=0.2):
            break
    return count, growth()


def format_leaks(result):
    if result.leaks:
        return '%d (%d)' % (
            sum(leak[-1] for leak in result.leaks),
            result.count)
    else:
        return '-'


def main():
    results = OrderedDict()
    print('Testing', end='', flush=True)
    for name, kwargs, value in TEST_VALUES:
        encoded = cbor2.dumps(value, **kwargs)
        results[name] = Result(
            encoding=Leaks(*test(lambda: cboar.dumps(value, **kwargs))),
            decoding=Leaks(*test(lambda: cboar.loads(encoded)))
        )
        print('.', end='', flush=True)
    print()
    print()

    # Build table content
    head = ('Test', 'Encoding', 'Decoding')
    rows = [head] + [
        (
            label,
            format_leaks(result.encoding),
            format_leaks(result.decoding),
        )
        for label, result in results.items()
    ]

    # Format table output
    cols = zip(*rows)
    col_widths = [max(len(row) for row in col) for col in cols]
    sep = ''.join((
        '+-',
        '-+-'.join('-' * width for width in col_widths),
        '-+',
    ))
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
                '{value:<{width}}'.format(value=value, width=width)
                for value, width in zip(row, col_widths)
            ),
            ' |',
        )))
    print(sep)


if __name__ == '__main__':
    main()
