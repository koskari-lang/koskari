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
Tests for KosZipLongest: the fill value standing in for a spent
source, a driven source running out while a sibling is still live, and
the driven sources itertools.zip_longest cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import itertools
import unittest

import pytest

from koskari.lib import KosZipLongest


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


class TestKosZipLongestSync(unittest.TestCase):
    """
    Rows continue past a spent source, which contributes the fill
    value, until every source is spent
    """

    def test_equal_lengths(self):
        self.assertEqual(list(KosZipLongest([1, 2, 3], "abc")),
                         [(1, 'a'), (2, 'b'), (3, 'c')])

    def test_shorter_second_source_fills(self):
        self.assertEqual(list(KosZipLongest([1, 2, 3], "ab")),
                         [(1, 'a'), (2, 'b'), (3, None)])

    def test_shorter_first_source_fills(self):
        self.assertEqual(list(KosZipLongest([1], "abc")),
                         [(1, 'a'), (None, 'b'), (None, 'c')])

    def test_fillvalue_replaces_none(self):
        self.assertEqual(list(KosZipLongest([1, 2, 3], "ab", fillvalue=0)),
                         [(1, 'a'), (2, 'b'), (3, 0)])

    def test_no_sources_is_empty(self):
        self.assertEqual(list(KosZipLongest()), [])

    def test_single_source(self):
        self.assertEqual(list(KosZipLongest([1, 2])), [(1,), (2,)])

    def test_every_source_empty(self):
        self.assertEqual(list(KosZipLongest([], [])), [])

    def test_more_sources_than_the_sync_mask_holds(self):
        rows = [[i, i + 100] for i in range(40)]
        self.assertEqual(list(KosZipLongest(*rows)),
                         list(itertools.zip_longest(*rows)))

    def test_three_sources_of_different_lengths(self):
        self.assertEqual(
            list(KosZipLongest([1, 2], [3], [4, 5, 6], fillvalue=-1)),
            [(1, 3, 4), (2, -1, 5), (-1, -1, 6)])

    def test_matches_itertools_zip_longest(self):
        cases = (((([1, 2, 3], "abc")), {}),
                 ((([1, 2, 3], "ab")), {}),
                 ((([1], "abc")), {}),
                 ((([1, 2, 3], "ab")), {'fillvalue': 0}),
                 ((([], [])), {}),
                 ((([1, 2], [3], [4, 5, 6])), {'fillvalue': -1}))
        for args, kwargs in cases:
            self.assertEqual(list(KosZipLongest(*args, **kwargs)),
                             list(itertools.zip_longest(*args, **kwargs)),
                             (args, kwargs))

    def test_exhausted_stays_exhausted(self):
        z = KosZipLongest([1], "ab")
        self.assertEqual(list(z), [(1, 'a'), (None, 'b')])
        self.assertEqual(list(z), [])


class TestKosZipLongestConstruction(unittest.TestCase):
    """
    fillvalue is the only keyword accepted
    """

    def test_non_iterable_raises_at_construction(self):
        with self.assertRaises(TypeError):
            KosZipLongest([1, 2], 5)

    def test_unknown_keyword_refused(self):
        with self.assertRaises(TypeError):
            KosZipLongest([1, 2], bogus=3)

    def test_extra_keyword_alongside_fillvalue_refused(self):
        with self.assertRaises(TypeError):
            KosZipLongest([1, 2], fillvalue=0, bogus=3)


class TestKosZipLongestDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source is what itertools.zip_longest cannot take, and one
    running out while a sibling is still live is the case that must
    carry the row on rather than end it
    """

    async def collect(self, z):
        return [row async for row in z]

    async def test_driven_source_runs_out_first(self):
        got = await self.collect(KosZipLongest(agen([1]), agen("abc")))
        self.assertEqual(got, [(1, 'a'), (None, 'b'), (None, 'c')])

    async def test_driven_source_runs_out_second(self):
        got = await self.collect(KosZipLongest(agen([1, 2, 3]), agen("ab")))
        self.assertEqual(got, [(1, 'a'), (2, 'b'), (3, None)])

    async def test_driven_sources_run_out_together(self):
        got = await self.collect(KosZipLongest(agen([1, 2]), agen("ab")))
        self.assertEqual(got, [(1, 'a'), (2, 'b')])

    async def test_mixed_sync_and_driven(self):
        got = await self.collect(KosZipLongest(agen([1, 2, 3]), "ab"))
        self.assertEqual(got, [(1, 'a'), (2, 'b'), (3, None)])

    async def test_fillvalue_across_a_suspension(self):
        got = await self.collect(
            KosZipLongest(agen([1, 2, 3]), agen("ab"), fillvalue=0))
        self.assertEqual(got, [(1, 'a'), (2, 'b'), (3, 0)])

    async def test_three_driven_sources(self):
        got = await self.collect(
            KosZipLongest(agen([1, 2]), agen([3]), agen([4, 5, 6])))
        self.assertEqual(got, [(1, 3, 4), (2, None, 5), (None, None, 6)])

    async def test_every_driven_source_empty(self):
        self.assertEqual(
            await self.collect(KosZipLongest(agen([]), agen([]))), [])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.zip_longest([1, 2], agen("ab")))

    async def test_driven_source_retires_past_the_sync_mask(self):
        # the per-source sync flags are cached in 32 bits; sources
        # beyond that are checked live. This one is both driven and
        # short, so it retires from index 35 while the rest run on
        rows = [[i, i + 100] for i in range(40)]
        sources = list(rows)
        sources[35] = agen([35])

        expected_rows = list(rows)
        expected_rows[35] = [35]

        self.assertEqual(
            await self.collect(KosZipLongest(*sources, fillvalue=-1)),
            list(itertools.zip_longest(*expected_rows, fillvalue=-1)))

    async def test_source_error_propagates(self):
        async def boom():
            yield 1
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosZipLongest(boom(), agen("abc")))


class TestKosZipLongestTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches every source the view still names, and a spent
    source is released as it retires
    """

    async def test_close_reaches_every_live_source(self):
        first = ClosingSource([1, 2, 3])
        second = ClosingSource([4, 5, 6])
        z = KosZipLongest(first, second)

        self.assertEqual(next(iter(z)), (1, 4))
        await z.aclose()

        self.assertTrue(first.closed)
        self.assertTrue(second.closed)

    async def test_close_after_a_source_retired(self):
        short = ClosingSource([1])
        long = ClosingSource([4, 5, 6])
        z = KosZipLongest(short, long)
        step = iter(z)

        self.assertEqual(next(step), (1, 4))
        # the second row retires the short source
        self.assertEqual(next(step), (None, 5))
        await z.aclose()

        self.assertTrue(long.closed)

    async def test_abandoned_mid_row_collects(self):
        z = KosZipLongest(agen([1, 2, 3]), agen("ab"))
        step = z.__aiter__()

        self.assertEqual(await step.__anext__(), (1, 'a'))

        del z, step
        gc.collect()

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        z = KosZipLongest(slow([1, 2]), [3, 4])
        first = asyncio.ensure_future(z.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await z.__anext__()

        release.set()
        self.assertEqual(await first, (1, 3))


class ReenteringSource:
    """
    A source whose first element steps the zip-longest consuming it
    """

    def __init__(self):
        self.n = 0
        self.z = None
        self.inner = None

    def __iter__(self):
        return self

    def __next__(self):
        self.n += 1
        if self.n == 1:
            self.inner = next(self.z)
        return self.n


class TestKosZipLongestReentry(unittest.TestCase):

    def test_source_that_reenters_the_zip_longest_is_refused(self):
        src = ReenteringSource()
        z = iter(KosZipLongest(src, [10, 20]))
        src.z = z
        with self.assertRaises(ValueError):
            next(z)


# The end.
