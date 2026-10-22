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
Tests for KosPairwise: the element held across steps, the two-fetch
prime, and the driven sources itertools.pairwise cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import itertools
import unittest
import weakref

import pytest

from koskari.lib import KosPairwise


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


class TestKosPairwiseSync(unittest.TestCase):
    """
    Consecutive overlapping pairs, built without the evaluator when
    the source is sync
    """

    def test_consecutive_pairs_overlap(self):
        self.assertEqual(list(KosPairwise("ABCD")),
                         [('A', 'B'), ('B', 'C'), ('C', 'D')])

    def test_two_element_source_yields_one_pair(self):
        self.assertEqual(list(KosPairwise([1, 2])), [(1, 2)])

    def test_single_element_source_yields_nothing(self):
        self.assertEqual(list(KosPairwise([1])), [])

    def test_empty_source_yields_nothing(self):
        self.assertEqual(list(KosPairwise([])), [])

    def test_row_is_a_two_tuple(self):
        row = next(iter(KosPairwise([1, 2])))
        self.assertIsInstance(row, tuple)
        self.assertEqual(len(row), 2)

    def test_exhausted_stays_exhausted(self):
        p = KosPairwise([1, 2])
        self.assertEqual(list(p), [(1, 2)])
        self.assertEqual(list(p), [])


class TestKosPairwiseConstruction(unittest.TestCase):
    """
    pairwise takes the source alone -- no callable
    """

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosPairwise(5)

    def test_second_argument_refused(self):
        with self.assertRaises(TypeError):
            KosPairwise([1, 2], [3, 4])

    def test_no_argument_refused(self):
        with self.assertRaises(TypeError):
            KosPairwise()

    def test_keyword_argument_refused(self):
        with self.assertRaises(TypeError):
            KosPairwise(iterable=[1, 2])


class TestKosPairwiseDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source is what itertools.pairwise cannot take
    """

    async def collect(self, p):
        return [row async for row in p]

    async def test_async_source(self):
        self.assertEqual(await self.collect(KosPairwise(agen("ABCD"))),
                         [('A', 'B'), ('B', 'C'), ('C', 'D')])

    async def test_async_single_element(self):
        self.assertEqual(await self.collect(KosPairwise(agen("A"))), [])

    async def test_async_empty(self):
        self.assertEqual(await self.collect(KosPairwise(agen(""))), [])

    async def test_held_element_survives_between_steps(self):
        p = KosPairwise(agen("ABC"))
        step = p.__aiter__()

        self.assertEqual(await step.__anext__(), ('A', 'B'))
        # B was held across the step boundary to open the next pair
        self.assertEqual(await step.__anext__(), ('B', 'C'))

    @unittest.skipIf(not hasattr(itertools, "pairwise"),
                     "itertools.pairwise arrived in 3.10")
    async def test_itertools_pairwise_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.pairwise(agen("AB")))


class TestKosPairwiseTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source, and an exhausted pairwise keeps
    nothing alive
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource([1, 2, 3])
        p = KosPairwise(src)

        self.assertEqual(next(iter(p)), (1, 2))
        await p.aclose()

        self.assertTrue(src.closed)

    async def test_abandoned_mid_stream_collects(self):
        p = KosPairwise(agen("ABCD"))
        step = p.__aiter__()

        self.assertEqual(await step.__anext__(), ('A', 'B'))

        del p, step
        gc.collect()

    async def test_exhausted_driven_source_releases_the_held_element(self):
        class Element:
            pass

        items = [Element(), Element(), Element()]
        last = weakref.ref(items[-1])
        p = KosPairwise(agen(items))

        rows = [row async for row in p]
        self.assertEqual(len(rows), 2)

        del rows, items
        gc.collect()

        # p is still alive; only its held element should be gone
        self.assertIsNone(last())
        self.assertEqual([row async for row in p], [])

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        p = KosPairwise(slow("ABC"))
        first = asyncio.ensure_future(p.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await p.__anext__()

        release.set()
        self.assertEqual(await first, ('A', 'B'))


class ReenteringSource:
    """
    A source whose second element steps the pairwise consuming it,
    and which is exhausted by that inner step
    """

    def __init__(self):
        self.n = 0
        self.pw = None
        self.inner = None

    def __iter__(self):
        return self

    def __next__(self):
        self.n += 1
        if self.n == 2:
            self.inner = next(self.pw)
            return "b"
        if self.n >= 3:
            raise StopIteration
        return self.n


class TestKosPairwiseReentry(unittest.TestCase):

    def test_source_that_reenters_the_pairwise_is_refused(self):
        src = ReenteringSource()
        pw = iter(KosPairwise(src))
        src.pw = pw
        with self.assertRaises(ValueError):
            next(pw)


# The end.
