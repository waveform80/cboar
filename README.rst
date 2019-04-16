=====
CBOAR
=====

A high performance, flexible CBOR serialization library. Basic usage is as
you'd expect::

    >>> import cboar as cbor
    >>> cbor.dumps(0)
    b'\x00'
    >>> cbor.dumps([1, 2, 3])
    b'\x83\x01\x02\x03'
    >>> cbor.loads(cbor.dumps('foo'))
    'foo'

Background
==========

On the piwheels project we recently switched to CBOR for all our serialization
needs. Partly this was down to security; we previously used pickle because it
was quick to get started with, but obviously there's *awful* security holes
there if you can't trust the nodes you're communicating with. Partly it was a
matter of flexibility; JSON was considered and quickly rejected for not
supporting half the data-types we wanted to transmit (timestamps, durations,
sets, etc. - half the fun of Python is its rich datatypes!).

Initially we settled on the `cbor2`_ library; it was extremely flexible and the
code looked well written and tested. Unfortunately, while cbor2 is easily fast
enough for the majority of purposes on a PC, we run piwheels on a pi and when
chucking around large structures (e.g. the search index) cbor2 took quite a
while to encode things. After a day of tweaking `cbor2`_ to try and improve the
performance, and trying pypy3 (nice idea, but it's not ready for primetime yet
with various external libraries causing issues), I decided to move to a C-based
implementation, specifically the popular `cbor`_ library.

Quickly, we ran into issues: it doesn't support some types out of the box (sets
and timestamps to name but two). No matter, it was flexible enough to provide a
mechanism to extend it with new types. Unfortunately, this mechanism isn't as
well designed as cbor2's. For instance, patching in set support breaks when
dealing with, say, sets of tuples (because unlike cbor2 it doesn't know it
should switch to immutable hashable collections when decoding within a set, or
for dict keys for that matter). Its decoding is also rather basic in several
areas (the long int decoding runs out of precision after a while, the dict
decoding doesn't handle complex keys). After digging into the code to see if
these issues could be quickly patched around, I came to the conclusion that its
internal design wasn't half as clean (or extensible) as cbor2's.

If only there was a C-based CBOR implementation that had a design as clean as
cbor2's, but written in (vaguely) comprehensible C!

Well, after a week mulling it over, here's my shot at it. It's basically a port
of cbor2 into C. Most of the internal architecture is exactly the same (an
OrderedDict to look up types, an identical default encoder mechanism, etc), so
I've licensed it the same as cbor2 because for all intents and purposes, it's a
derivative work (hell, I even nicked their test suite).
