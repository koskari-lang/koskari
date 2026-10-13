# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.reproducers.test_yield_from_delegate_teardown

A generator delegating with yield-from hands its delegate every
protocol event, as PEP 380 lays out: closing the delegating generator
(close, aclose, collection, a for-each that breaks) closes a Koskari
delegate so its finally runs, and closes a plain delegate that has a
close method; an exception thrown into the delegating generator is
thrown into a delegate that has throw, and raised at the yield-from
point for one that does not, where the delegating generator's own
handler takes it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest
import weakref

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def inner [col]:
    try:
        (yield 1)
        (yield 2)
    finally:
        (col.append "inner finally")

def outer [col]:
    try:
        (yield-from (inner col))
    finally:
        (col.append "outer finally")

def loop-outer [col]:
    for-each [x (outer col)]:
        (break)
    col

def relay [items col]:
    try:
        (yield-from items)
    except [ValueError]:
        (col.append "caught")
        (yield "handled")
"""


class Closeable:
    """
    A plain iterator with a close method and no throw
    """

    def __init__(self, col):
        self.col = col
        self.n = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.n += 1
        return self.n

    def close(self):
        self.col.append("delegate closed")


class Throwable(Closeable):
    """
    A plain iterator that also takes throw
    """

    def throw(self, exc):
        self.col.append(("thrown", type(exc).__name__))
        return "from delegate"


class TestYieldFromDelegateTeardown(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_close_runs_the_koskari_delegate_finally(self):
        m = await self.load("yf_close")
        col = []
        g = iter(m["outer"](col))
        next(g)
        g.close()
        self.assertEqual(["inner finally", "outer finally"], col)


    async def test_aclose_runs_the_koskari_delegate_finally(self):
        m = await self.load("yf_aclose")
        col = []
        ag = m["outer"](col)
        await ag.__anext__()
        await ag.aclose()
        self.assertEqual(["inner finally", "outer finally"], col)


    async def test_collection_runs_the_koskari_delegate_finally(self):
        m = await self.load("yf_gc")
        col = []
        g = iter(m["outer"](col))
        next(g)
        del g
        gc.collect()
        self.assertEqual(["inner finally", "outer finally"], col)


    async def test_cycle_through_the_delegate_is_collected(self):
        """
        a suspended delegating generator in a reference cycle through
        its delegate is collectable: the delegate the frame holds is
        visible to the cyclic collector
        """
        m = await self.load("yf_cycle")
        col = []

        class Box:
            pass

        def inner(box):
            yield box
            yield 2

        box = Box()
        g = iter(m["relay"](inner(box), col))
        box.g = g
        ref = weakref.ref(box)
        next(g)
        del g, box
        gc.collect()
        self.assertIsNone(ref())


    async def test_collection_releases_a_python_generator_delegate(self):
        mod = await self.load("yfdt_collect_plain")
        col = []

        def plain(col):
            try:
                yield 1
                yield 2
            finally:
                col.append("plain finally")

        delegate = plain(col)
        ref = weakref.ref(delegate)
        gen = mod["relay"](delegate, col)
        del delegate
        self.assertEqual(await gen.__anext__(), 1)
        del gen
        gc.collect()
        self.assertEqual(col, ["plain finally"])
        self.assertIsNone(ref())


    async def test_for_each_break_runs_the_koskari_delegate_finally(self):
        m = await self.load("yf_break")
        col = await m["loop-outer"]([])
        self.assertEqual(["inner finally", "outer finally"], col)


    async def test_close_closes_a_plain_delegate(self):
        m = await self.load("yf_plain_close")
        col = []
        g = iter(m["relay"](Closeable(col), col))
        next(g)
        g.close()
        self.assertEqual(["delegate closed"], col)


    async def test_throw_into_a_delegate_without_throw_raises_at_the_point(self):
        m = await self.load("yf_throw_plain")
        col = []
        g = iter(m["relay"]({"a": 1, "b": 2}.items(), col))
        self.assertEqual(("a", 1), next(g))
        self.assertEqual("handled", g.throw(ValueError("stop")))
        self.assertEqual(["caught"], col)


    async def test_throw_into_a_delegate_with_throw_reaches_it(self):
        m = await self.load("yf_throw_able")
        col = []
        g = iter(m["relay"](Throwable(col), col))
        self.assertEqual(1, next(g))
        self.assertEqual("from delegate", g.throw(ValueError("stop")))
        self.assertEqual([("thrown", "ValueError")], col)


# The end.
