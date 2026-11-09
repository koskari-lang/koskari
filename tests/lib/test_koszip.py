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
Tests for KosZip: the row walk over N sources, the driven sources
builtins.zip cannot take at all, and the teardown of a view the
provider owns

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest

import pytest

from koskari.lib import KosZip


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


class CountingSource:
    """
    A sync iterator counting every call to its __next__
    """

    def __init__(self, items):
        self.it = iter(items)
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        return next(self.it)


class RaisingBool:
    """
    A value whose truth test raises
    """

    def __bool__(self):
        raise RuntimeError("no truth here")


class TestKosZipSync(unittest.TestCase):
    """
    Every source stepping through tp_iternext: the whole row is built
    without the evaluator
    """

    def test_rows_pair_positionally(self):
        self.assertEqual(list(KosZip([1, 2, 3], "abc")),
                         [(1, 'a'), (2, 'b'), (3, 'c')])

    def test_shortest_source_ends_the_zip(self):
        self.assertEqual(list(KosZip([1, 2, 3], "ab")),
                         [(1, 'a'), (2, 'b')])

    def test_no_sources_is_empty(self):
        self.assertEqual(list(KosZip()), [])

    def test_single_source_yields_one_tuples(self):
        self.assertEqual(list(KosZip([1, 2])), [(1,), (2,)])

    def test_many_sources(self):
        self.assertEqual(list(KosZip([1, 2], [3, 4], [5, 6], [7, 8])),
                         [(1, 3, 5, 7), (2, 4, 6, 8)])

    def test_more_sources_than_the_sync_mask_holds(self):
        rows = [[i, i + 100] for i in range(40)]
        self.assertEqual(list(KosZip(*rows)), list(zip(*rows)))

    def test_rows_kept_by_the_consumer_stay_distinct(self):
        rows = list(KosZip([1, 2, 3], "abc"))
        self.assertEqual(rows, [(1, "a"), (2, "b"), (3, "c")])
        self.assertEqual(3, len({id(r) for r in rows}))

    def test_rows_read_one_at_a_time_hold_their_own_items(self):
        z = iter(KosZip([1, 2, 3], "abc"))
        first = next(z)
        second = next(z)
        self.assertEqual((1, "a"), first)
        self.assertEqual((2, "b"), second)
        del first
        self.assertEqual((3, "c"), next(z))
        self.assertEqual((2, "b"), second)

    def test_row_is_a_tuple(self):
        self.assertIsInstance(next(iter(KosZip([1], [2]))), tuple)

    def test_exhausted_zip_stays_exhausted(self):
        z = KosZip([1], [2])
        self.assertEqual(list(z), [(1, 2)])
        self.assertEqual(list(z), [])


class TestKosZipStrict(unittest.TestCase):
    """
    strict=True raises ValueError when the sources do not all end
    together, worded as builtins.zip words it
    """

    def assertStrictError(self, z, message):
        with self.assertRaises(ValueError) as cm:
            list(z)
        self.assertEqual(str(cm.exception), message)

    def test_equal_lengths_give_the_same_rows(self):
        self.assertEqual(list(KosZip([1, 2, 3], "abc", strict=True)),
                         list(KosZip([1, 2, 3], "abc")))

    def test_argument_two_shorter(self):
        self.assertStrictError(
            KosZip([1, 2], [1], strict=True),
            "zip() argument 2 is shorter than argument 1")

    def test_argument_two_longer(self):
        self.assertStrictError(
            KosZip([1], [1, 2], strict=True),
            "zip() argument 2 is longer than argument 1")

    def test_argument_three_shorter(self):
        self.assertStrictError(
            KosZip([1, 2], [1, 2], [1], strict=True),
            "zip() argument 3 is shorter than arguments 1-2")

    def test_argument_three_longer_after_argument_two_ended(self):
        self.assertStrictError(
            KosZip([1], [1], [1, 2], strict=True),
            "zip() argument 3 is longer than arguments 1-2")

    def test_longer_source_pulled_once_past_the_end(self):
        longer = CountingSource([1, 2, 3])
        self.assertStrictError(
            KosZip([1], longer, strict=True),
            "zip() argument 2 is longer than argument 1")
        self.assertEqual(longer.calls, 2)

    def test_strict_false_is_the_default(self):
        self.assertEqual(list(KosZip([1, 2], [1], strict=False)),
                         list(KosZip([1, 2], [1])))
        self.assertEqual(list(KosZip([1, 2], [1], strict=False)), [(1, 1)])

    def test_no_sources_is_empty(self):
        self.assertEqual(list(KosZip(strict=True)), [])

    def test_strict_error_does_not_latch(self):
        first = CountingSource([1, 2, 3])
        z = iter(KosZip(first, [1], strict=True))

        self.assertEqual(next(z), (1, 1))
        with self.assertRaises(ValueError):
            next(z)
        self.assertEqual(first.calls, 2)

        with self.assertRaises(ValueError):
            next(z)
        self.assertEqual(first.calls, 3)


class TestKosZipConstruction(unittest.TestCase):
    """
    Sources are acquired eagerly, so a bad argument raises where the
    zip is written rather than inside whatever drives it
    """

    def test_non_iterable_raises_at_construction(self):
        with self.assertRaises(TypeError):
            KosZip([1, 2], 5)

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosZip([1, 2], other=[3, 4])

    def test_strict_with_another_keyword_refused(self):
        with self.assertRaises(TypeError):
            KosZip([1, 2], strict=True, other=[1])

    def test_strict_truth_test_error_propagates(self):
        with self.assertRaises(RuntimeError):
            KosZip([1, 2], [3, 4], strict=RaisingBool())


class TestKosZipDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source -- an async iterator or a native provider -- is
    what builtins.zip cannot consume at all
    """

    async def collect(self, z):
        return [row async for row in z]

    async def test_sync_with_async_source(self):
        self.assertEqual(await self.collect(KosZip([1, 2, 3], agen("abc"))),
                         [(1, 'a'), (2, 'b'), (3, 'c')])

    async def test_every_source_async(self):
        got = await self.collect(KosZip(agen([1, 2, 3]), agen("abc")))
        self.assertEqual(got, [(1, 'a'), (2, 'b'), (3, 'c')])

    async def test_async_source_ends_the_zip(self):
        self.assertEqual(await self.collect(KosZip(agen([1, 2]), "abc")),
                         [(1, 'a'), (2, 'b')])

    async def test_mixed_sources_across_a_row(self):
        got = await self.collect(
            KosZip(agen([1, 2]), "abc", agen([9, 8, 7])))
        self.assertEqual(got, [(1, 'a', 9), (2, 'b', 8)])

    async def test_builtin_zip_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(zip([1, 2, 3], agen("abc")))

    async def test_driven_source_past_the_sync_mask(self):
        # the per-source sync flags are cached in 32 bits; sources
        # beyond that are checked live, so a driven one at index 35
        # is what proves the fallback
        rows = [[i, i + 100] for i in range(40)]
        sources = list(rows)
        sources[35] = agen(rows[35])

        self.assertEqual(await self.collect(KosZip(*sources)),
                         list(zip(*rows)))

    async def assertStrictError(self, z, message):
        with self.assertRaises(ValueError) as cm:
            await self.collect(z)
        self.assertEqual(str(cm.exception), message)

    async def test_strict_shorter_at_a_driven_source(self):
        await self.assertStrictError(
            KosZip([1, 2], agen([1]), strict=True),
            "zip() argument 2 is shorter than argument 1")

    async def test_strict_driven_first_source_ends_into_the_tail(self):
        await self.assertStrictError(
            KosZip(agen([1]), [1, 2], strict=True),
            "zip() argument 2 is longer than argument 1")

    async def test_strict_longer_at_a_driven_source_in_the_tail(self):
        await self.assertStrictError(
            KosZip([1], agen([1, 2]), strict=True),
            "zip() argument 2 is longer than argument 1")

    async def test_strict_tail_across_a_driven_source_that_ends(self):
        got = await self.collect(KosZip([1], agen("a"), [9], strict=True))
        self.assertEqual(got, [(1, 'a', 9)])

    async def test_strict_tail_past_a_driven_source_finds_a_longer(self):
        await self.assertStrictError(
            KosZip([1], agen("a"), [9, 8], strict=True),
            "zip() argument 3 is longer than arguments 1-2")


class TestKosZipTeardown(unittest.IsolatedAsyncioTestCase):
    """
    The provider owns the source storage and publishes it to the base,
    which closes every source it names
    """

    async def test_close_reaches_every_source(self):
        first = ClosingSource([1, 2, 3])
        second = ClosingSource([4, 5, 6])
        z = KosZip(first, second)

        self.assertEqual(next(iter(z)), (1, 4))
        await z.aclose()

        self.assertTrue(first.closed)
        self.assertTrue(second.closed)

    async def test_close_before_any_step_reaches_every_source(self):
        first = ClosingSource([1])
        second = ClosingSource([2])

        await KosZip(first, second).aclose()

        self.assertTrue(first.closed)
        self.assertTrue(second.closed)

    async def test_abandoned_mid_row_collects(self):
        z = KosZip(agen([1, 2, 3]), agen("abc"))
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

        z = KosZip(slow([1, 2]), [3, 4])
        first = asyncio.ensure_future(z.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await z.__anext__()

        release.set()
        self.assertEqual(await first, (1, 3))


# The end.
