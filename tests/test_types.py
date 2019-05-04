import pytest

from cboar import *


def test_undefined_bool():
    assert not undefined


def test_undefined_repr():
    assert repr(undefined) == 'undefined'


def test_undefined_singleton():
    assert type(undefined)() is undefined


def test_undefined_init():
    with pytest.raises(TypeError):
        type(undefined)('foo')


def test_break_bool():
    assert break_marker


def test_break_repr():
    assert repr(break_marker) == 'break_marker'


def test_break_singleton():
    assert type(break_marker)() is break_marker


def test_break_init():
    with pytest.raises(TypeError):
        type(break_marker)('foo')


def test_tag_init():
    with pytest.raises(TypeError):
        CBORTag('foo', 'bar')


def test_tag_attr():
    tag = CBORTag(1, 'foo')
    assert tag.tag == 1
    assert tag.value == 'foo'


def test_tag_compare():
    tag1 = CBORTag(1, 'foo')
    tag2 = CBORTag(1, 'foo')
    tag3 = CBORTag(2, 'bar')
    tag4 = CBORTag(2, 'baz')
    assert tag1 is not tag2
    assert tag1 == tag2
    assert not (tag1 == tag3)
    assert tag1 != tag3
    assert tag3 >= tag2
    assert tag3 > tag2
    assert tag2 < tag3
    assert tag2 <= tag3
    assert tag4 >= tag3
    assert tag4 > tag3
    assert tag3 < tag4
    assert tag3 <= tag4
    assert not tag1 == (1, 'foo')


def test_tag_recursive():
    tag = CBORTag(1, None)
    tag.value = tag
    assert repr(tag) == 'CBORTag(1, CBORTag(...))'
    assert tag is tag.value
    assert tag == tag.value
    assert not (tag != tag.value)
