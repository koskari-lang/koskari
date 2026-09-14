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
Tests for KosCycle: the first pass buffering as it yields, the switch
to replaying when the source runs out, and the driven sources
itertools.cycle cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosCycle


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


def take(iterable, n):
    """
    The first n elements, for a source that never ends on its own
    """

    return list(itertools.islice(iterable, n))


class TestKosCycleSync(unittest.TestCase):
    """
    The first pass keeps what it yields, and later laps read it back
    """

    def test_repeats_the_source(self):
        self.assertEqual(take(KosCycle("ABC"), 8),
                         ['A', 'B', 'C', 'A', 'B', 'C', 'A', 'B'])

    def test_single_element_source(self):
        self.assertEqual(take(KosCycle([7]), 3), [7, 7, 7])

    def test_wraps_at_the_buffer_end(self):
        self.assertEqual(take(KosCycle([1, 2]), 5), [1, 2, 1, 2, 1])

    def test_empty_source_is_exhausted_not_endless(self):
        self.assertEqual(list(KosCycle("")), [])

    def test_empty_source_stays_exhausted(self):
        c = KosCycle([])
        self.assertEqual(list(c), [])
        self.assertEqual(list(c), [])

    def test_matches_itertools_cycle(self):
        for src, n in (("ABC", 8), ([1, 2], 5), ([7], 3), ("", 3)):
            self.assertEqual(take(KosCycle(src), n),
                             take(itertools.cycle(src), n), (src, n))

    def test_source_consumed_once(self):
        src = ClosingSource("AB")
        c = KosCycle(src)

        self.assertEqual(take(c, 6), ['A', 'B', 'A', 'B', 'A', 'B'])
        # the underlying iterator was walked a single time
        self.assertEqual(list(src), [])


class TestKosCycleConstruction(unittest.TestCase):
    """
    cycle takes the source alone
    """

    def test_no_argument_refused(self):
        with self.assertRaises(TypeError):
            KosCycle()

    def test_second_argument_refused(self):
        with self.assertRaises(TypeError):
            KosCycle("AB", "CD")

    def test_non_iterable_refused(self):
        with self.assertRaises(TypeError):
            KosCycle(5)

    def test_keyword_argument_refused(self):
        with self.assertRaises(TypeError):
            KosCycle(iterable="AB")


class TestKosCycleDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source is what itertools.cycle cannot take; once buffered
    it replays without the evaluator
    """

    async def take(self, c, n):
        out = []
        async for item in c:
            out.append(item)
            if len(out) >= n:
                break
        return out

    async def test_async_source_repeats(self):
        self.assertEqual(await self.take(KosCycle(agen("ABC")), 8),
                         ['A', 'B', 'C', 'A', 'B', 'C', 'A', 'B'])

    async def test_async_single_element(self):
        self.assertEqual(await self.take(KosCycle(agen([7])), 3), [7, 7, 7])

    async def test_async_empty_source_is_exhausted(self):
        self.assertEqual([r async for r in KosCycle(agen(""))], [])

    async def test_async_empty_source_stays_exhausted(self):
        c = KosCycle(agen(""))
        self.assertEqual([r async for r in c], [])
        self.assertEqual([r async for r in c], [])

    async def test_replay_crosses_the_switch(self):
        c = KosCycle(agen([1, 2]))
        step = c.__aiter__()

        self.assertEqual(await step.__anext__(), 1)
        self.assertEqual(await step.__anext__(), 2)
        # the source is spent here; this one comes from the buffer
        self.assertEqual(await step.__anext__(), 1)

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            take(itertools.cycle(agen("AB")), 2)

    async def test_source_error_propagates(self):
        async def boom():
            yield 'A'
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.take(KosCycle(boom()), 5)


class TestKosCycleTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source while it is still the source
    """

    async def test_close_before_the_switch_reaches_the_source(self):
        src = ClosingSource("ABCD")
        c = KosCycle(src)

        self.assertEqual(next(iter(c)), 'A')
        await c.aclose()

        self.assertTrue(src.closed)

    async def test_close_after_the_switch(self):
        src = ClosingSource("AB")
        c = KosCycle(src)

        self.assertEqual(take(c, 4), ['A', 'B', 'A', 'B'])
        await c.aclose()

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        c = KosCycle(slow("AB"))
        first = asyncio.ensure_future(c.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await c.__anext__()

        release.set()
        self.assertEqual(await first, 'A')


# The end.
