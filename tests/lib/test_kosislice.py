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
Tests for KosISlice: the discard run, the bounds, the source left
unconsumed past a spent slice, and the driven sources
itertools.islice cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosISlice


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


class TestKosISliceSync(unittest.TestCase):
    """
    The slice runs entirely in C while the source is sync
    """

    def test_stop_only(self):
        self.assertEqual(list(KosISlice(range(10), 3)), [0, 1, 2])

    def test_start_and_stop(self):
        self.assertEqual(list(KosISlice(range(10), 2, 5)), [2, 3, 4])

    def test_step(self):
        self.assertEqual(list(KosISlice(range(10), 1, 7, 2)), [1, 3, 5])

    def test_open_stop(self):
        self.assertEqual(list(KosISlice(range(5), 2, None)), [2, 3, 4])

    def test_none_start(self):
        self.assertEqual(list(KosISlice(range(5), None, 3)), [0, 1, 2])

    def test_none_step(self):
        self.assertEqual(list(KosISlice(range(5), 0, 3, None)), [0, 1, 2])

    def test_zero_stop_is_empty(self):
        self.assertEqual(list(KosISlice(range(5), 0)), [])

    def test_empty_when_start_meets_stop(self):
        self.assertEqual(list(KosISlice(range(10), 3, 3)), [])

    def test_stop_past_the_source(self):
        self.assertEqual(list(KosISlice(range(3), 10)), [0, 1, 2])

    def test_matches_itertools_islice(self):
        for bounds in ((10,), (2,), (0,), (2, 5), (2, None), (1, 7, 2),
                       (0, 10, 3), (None, 4), (3, 3)):
            self.assertEqual(list(KosISlice(range(10), *bounds)),
                             list(itertools.islice(range(10), *bounds)),
                             bounds)

    def test_spent_slice_leaves_the_rest_unconsumed(self):
        src = iter(range(10))
        self.assertEqual(list(KosISlice(src, 3)), [0, 1, 2])
        self.assertEqual(list(src), [3, 4, 5, 6, 7, 8, 9])

    def test_exhausted_stays_exhausted(self):
        s = KosISlice(range(10), 2)
        self.assertEqual(list(s), [0, 1])
        self.assertEqual(list(s), [])


class TestKosISliceConstruction(unittest.TestCase):
    """
    Bounds are rejected the way Python's islice rejects them, message
    included
    """

    def assertSameError(self, *bounds):
        expected = None
        try:
            itertools.islice(range(5), *bounds)
        except Exception as exc:
            expected = (type(exc), str(exc))

        self.assertIsNotNone(expected, bounds)
        with self.assertRaises(expected[0]) as caught:
            KosISlice(range(5), *bounds)
        self.assertEqual(str(caught.exception), expected[1], bounds)

    def test_negative_stop(self):
        self.assertSameError(-1)

    def test_negative_start(self):
        self.assertSameError(-1, 5)

    def test_negative_stop_in_the_long_form(self):
        self.assertSameError(1, -1)

    def test_zero_step(self):
        self.assertSameError(1, 5, 0)

    def test_negative_step(self):
        self.assertSameError(1, 5, -2)

    def test_non_integer_start(self):
        self.assertSameError("x", 5)

    def test_non_integer_stop(self):
        self.assertSameError(1, "x")

    def test_too_few_arguments(self):
        with self.assertRaises(TypeError):
            KosISlice(range(5))

    def test_too_many_arguments(self):
        with self.assertRaises(TypeError):
            KosISlice(range(5), 1, 2, 3, 4)

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosISlice(range(5), stop=2)

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosISlice(5, 2)


class TestKosISliceDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source is what itertools.islice cannot take, and its
    skipped elements are discarded through the evaluator
    """

    async def collect(self, s):
        return [item async for item in s]

    async def test_async_stop_only(self):
        self.assertEqual(await self.collect(KosISlice(agen(range(10)), 3)),
                         [0, 1, 2])

    async def test_async_start_stop_step(self):
        got = await self.collect(KosISlice(agen(range(10)), 2, 7, 2))
        self.assertEqual(got, [2, 4, 6])

    async def test_async_open_stop(self):
        self.assertEqual(
            await self.collect(KosISlice(agen(range(8)), 5, None)),
            [5, 6, 7])

    async def test_async_start_past_the_source(self):
        self.assertEqual(await self.collect(KosISlice(agen(range(3)), 9, 12)),
                         [])

    async def test_itertools_islice_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.islice(agen(range(5)), 2))


class TestKosISliceTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source through the base's view
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource(range(10))
        s = KosISlice(src, 2, 6)

        self.assertEqual(next(iter(s)), 2)
        await s.aclose()

        self.assertTrue(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        s = KosISlice(slow(range(4)), 2)
        first = asyncio.ensure_future(s.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await s.__anext__()

        release.set()
        self.assertEqual(await first, 0)


# The end.
