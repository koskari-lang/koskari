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
Tests for KosBatched: the batch fill, the short final batch on both
the sync and the driven path, and the driven sources
itertools.batched cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosBatched


pytestmark = pytest.mark.phase_lib


async def agen(items):
    """
    An async iterator that suspends before each element
    """

    for item in items:
        await asyncio.sleep(0)
        yield item


def expected(total, n):
    """
    The batches a source of range(total) should produce at size n
    """

    return [tuple(range(i, min(i + n, total)))
            for i in range(0, total, n)]


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


class TestKosBatchedSync(unittest.TestCase):
    """
    Batches build entirely in C while the source is sync
    """

    def test_even_division(self):
        self.assertEqual(list(KosBatched(range(6), 3)),
                         [(0, 1, 2), (3, 4, 5)])

    def test_short_final_batch(self):
        self.assertEqual(list(KosBatched(range(7), 3)),
                         [(0, 1, 2), (3, 4, 5), (6,)])

    def test_source_shorter_than_one_batch(self):
        self.assertEqual(list(KosBatched(range(2), 5)), [(0, 1)])

    def test_empty_source(self):
        self.assertEqual(list(KosBatched([], 3)), [])

    def test_batch_of_one(self):
        self.assertEqual(list(KosBatched(range(3), 1)),
                         [(0,), (1,), (2,)])

    def test_batches_are_tuples(self):
        self.assertIsInstance(next(iter(KosBatched(range(3), 2))), tuple)

    def test_covers_every_shape(self):
        for total, n in ((7, 3), (6, 3), (2, 5), (0, 3), (4, 1), (9, 4)):
            self.assertEqual(list(KosBatched(range(total), n)),
                             expected(total, n), (total, n))

    @unittest.skipIf(not hasattr(itertools, "batched"),
                     "itertools.batched arrived in 3.12")
    def test_matches_itertools_batched(self):
        for n in (1, 2, 3, 5, 9):
            self.assertEqual(list(KosBatched(range(7), n)),
                             list(itertools.batched(range(7), n)), n)

    def test_exhausted_stays_exhausted(self):
        b = KosBatched(range(3), 2)
        self.assertEqual(list(b), [(0, 1), (2,)])
        self.assertEqual(list(b), [])


class TestKosBatchedConstruction(unittest.TestCase):
    """
    The batch size is required and must be at least one
    """

    def test_zero_batch_size_refused(self):
        with self.assertRaises(ValueError):
            KosBatched(range(3), 0)

    def test_negative_batch_size_refused(self):
        with self.assertRaises(ValueError):
            KosBatched(range(3), -1)

    def test_non_integer_batch_size_refused(self):
        with self.assertRaises(TypeError):
            KosBatched(range(3), "x")

    def test_missing_batch_size_refused(self):
        with self.assertRaises(TypeError):
            KosBatched(range(3))

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosBatched(range(3), n=2)

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosBatched(5, 2)


class TestKosBatchedDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source is what itertools.batched cannot take. A source
    that runs out part way through a batch is the one place a step
    handles the pending StopIteration instead of propagating it
    """

    async def collect(self, b):
        return [batch async for batch in b]

    async def test_even_division(self):
        self.assertEqual(await self.collect(KosBatched(agen(range(6)), 3)),
                         [(0, 1, 2), (3, 4, 5)])

    async def test_short_final_batch(self):
        self.assertEqual(await self.collect(KosBatched(agen(range(7)), 3)),
                         [(0, 1, 2), (3, 4, 5), (6,)])

    async def test_source_shorter_than_one_batch(self):
        self.assertEqual(await self.collect(KosBatched(agen(range(2)), 5)),
                         [(0, 1)])

    async def test_empty_source(self):
        self.assertEqual(await self.collect(KosBatched(agen([]), 3)), [])

    async def test_covers_every_shape(self):
        for total, n in ((7, 3), (6, 3), (2, 5), (0, 3), (4, 1), (5, 2),
                         (1, 1), (9, 4)):
            got = await self.collect(KosBatched(agen(range(total)), n))
            self.assertEqual(got, expected(total, n), (total, n))

    @unittest.skipIf(not hasattr(itertools, "batched"),
                     "itertools.batched arrived in 3.12")
    async def test_itertools_batched_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.batched(agen(range(4)), 2))

    async def test_source_error_propagates(self):
        async def boom():
            yield 1
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosBatched(boom(), 3))


class TestKosBatchedTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source through the base's view
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource(range(10))
        b = KosBatched(src, 3)

        self.assertEqual(next(iter(b)), (0, 1, 2))
        await b.aclose()

        self.assertTrue(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        b = KosBatched(slow(range(4)), 2)
        first = asyncio.ensure_future(b.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await b.__anext__()

        release.set()
        self.assertEqual(await first, (0, 1))


# The end.
