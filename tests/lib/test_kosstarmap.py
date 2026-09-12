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
Tests for KosStarMap: the element unpacked as the argument vector, the
driven sources and suspending functions itertools.starmap cannot take,
and parity with the Python original

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import operator
import unittest

import pytest

from koskari.lib import KosStarMap


pytestmark = pytest.mark.phase_lib


async def agen(items):
    """
    An async iterator that suspends before each element
    """

    for item in items:
        await asyncio.sleep(0)
        yield item


class ClosingSource:
    """
    A sync iterator recording whether its close was reached
    """

    def __init__(self, items):
        self.it = iter(items)
        self.closed = False

    def __iter__(self):
        return self

    def __next__(self):
        return next(self.it)

    def close(self):
        self.closed = True


class TestKosStarMapSync(unittest.TestCase):
    """
    A sync source and a plain python function complete without the
    evaluator
    """

    def test_element_supplies_the_argument_vector(self):
        self.assertEqual(list(KosStarMap(operator.add, [(1, 2), (3, 4)])),
                         [3, 7])

    def test_arity_follows_each_element(self):
        self.assertEqual(
            list(KosStarMap(lambda *a: a, [[1, 2, 3], (4,), ()])),
            [(1, 2, 3), (4,), ()])

    def test_non_tuple_sequences_unpack(self):
        self.assertEqual(list(KosStarMap(operator.add, [[1, 2], "ab"])),
                         [3, 'ab'])

    def test_empty_source(self):
        self.assertEqual(list(KosStarMap(operator.add, [])), [])

    def test_matches_itertools_starmap(self):
        rows = [(1, 2), (3, 4), (5, 6)]
        self.assertEqual(list(KosStarMap(operator.add, rows)),
                         list(itertools.starmap(operator.add, rows)))

    def test_exhausted_stays_exhausted(self):
        s = KosStarMap(operator.add, [(1, 2)])
        self.assertEqual(list(s), [3])
        self.assertEqual(list(s), [])


class TestKosStarMapConstruction(unittest.TestCase):
    """
    The function is required, and each element must be iterable
    """

    def test_none_func_refused(self):
        with self.assertRaises(TypeError):
            KosStarMap(None, [(1, 2)])

    def test_non_callable_func_refused(self):
        with self.assertRaises(TypeError):
            KosStarMap(5, [(1, 2)])

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosStarMap(operator.add, 5)

    def test_non_iterable_element_raises_at_the_step(self):
        with self.assertRaises(TypeError):
            list(KosStarMap(operator.add, [5]))

    def test_wrong_arity_element_raises_from_the_call(self):
        with self.assertRaises(TypeError):
            list(KosStarMap(operator.add, [(1, 2, 3)]))


class TestKosStarMapDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source or a suspending function is what
    itertools.starmap cannot take
    """

    async def collect(self, s):
        return [item async for item in s]

    async def test_async_source(self):
        got = await self.collect(
            KosStarMap(operator.add, agen([(1, 2), (3, 4)])))
        self.assertEqual(got, [3, 7])

    async def test_suspending_function(self):
        async def slow(a, b):
            await asyncio.sleep(0)
            return a + b

        self.assertEqual(
            await self.collect(KosStarMap(slow, [(1, 2), (3, 4)])), [3, 7])

    async def test_async_source_and_suspending_function(self):
        async def slow(a, b):
            await asyncio.sleep(0)
            return a + b

        got = await self.collect(KosStarMap(slow, agen([(1, 2), (3, 4)])))
        self.assertEqual(got, [3, 7])

    async def test_itertools_starmap_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.starmap(operator.add, agen([(1, 2)])))

    async def test_function_error_propagates(self):
        def boom(a, b):
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosStarMap(boom, [(1, 2)]))


class TestKosStarMapTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source through the base's view
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource([(1, 2), (3, 4)])
        s = KosStarMap(operator.add, src)

        self.assertEqual(next(iter(s)), 3)
        await s.aclose()

        self.assertTrue(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(a, b):
            await release.wait()
            return a + b

        s = KosStarMap(slow, [(1, 2), (3, 4)])
        first = asyncio.ensure_future(s.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await s.__anext__()

        release.set()
        self.assertEqual(await first, 3)


# The end.
