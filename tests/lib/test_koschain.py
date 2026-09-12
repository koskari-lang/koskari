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
Tests for KosChain: one source live at a time, each acquired only when
the one before it runs out, and the driven sources itertools.chain
cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosChain


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


class Tracked:
    """
    An iterable recording when its iterator was acquired
    """

    def __init__(self, items, tag, log):
        self.items = items
        self.tag = tag
        self.log = log

    def __iter__(self):
        self.log.append(self.tag)
        return iter(self.items)


class TestKosChainSync(unittest.TestCase):
    """
    The series runs end to end while its sources are sync
    """

    def test_runs_each_source_in_turn(self):
        self.assertEqual(list(KosChain("AB", [1, 2])), ['A', 'B', 1, 2])

    def test_single_source(self):
        self.assertEqual(list(KosChain("ABC")), ['A', 'B', 'C'])

    def test_no_sources_is_empty(self):
        self.assertEqual(list(KosChain()), [])

    def test_leading_empties_are_skipped(self):
        self.assertEqual(list(KosChain([], [], [1])), [1])

    def test_trailing_empties_are_skipped(self):
        self.assertEqual(list(KosChain([1], [], [])), [1])

    def test_empty_in_the_middle(self):
        self.assertEqual(list(KosChain("AB", "", "CD")),
                         ['A', 'B', 'C', 'D'])

    def test_matches_itertools_chain(self):
        cases = (("AB", [1, 2]), ("ABC",), (), ([], [], [1]),
                 ([1], [], []), ("AB", "", "CD"))
        for args in cases:
            self.assertEqual(list(KosChain(*args)),
                             list(itertools.chain(*args)), args)

    def test_exhausted_stays_exhausted(self):
        c = KosChain([1], [2])
        self.assertEqual(list(c), [1, 2])
        self.assertEqual(list(c), [])


class TestKosChainLaziness(unittest.TestCase):
    """
    A source is acquired when it is reached and not before
    """

    def test_later_sources_untouched_until_reached(self):
        log = []
        c = KosChain(Tracked([1, 2], 'a', log), Tracked([3], 'b', log))
        step = iter(c)

        self.assertEqual(next(step), 1)
        self.assertEqual(log, ['a'])

        self.assertEqual(list(step), [2, 3])
        self.assertEqual(log, ['a', 'b'])

    def test_unreached_source_never_acquired(self):
        log = []
        c = KosChain(Tracked([1], 'a', log), Tracked([2], 'b', log))

        self.assertEqual(next(iter(c)), 1)
        self.assertEqual(log, ['a'])

    def test_bad_source_raises_when_reached_not_at_construction(self):
        c = KosChain([1], 5)

        self.assertEqual(next(iter(c)), 1)
        with self.assertRaises(TypeError):
            list(c)


class TestKosChainConstruction(unittest.TestCase):
    """
    chain takes its series positionally and no keywords
    """

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosChain([1], bogus=2)


class TestKosChainDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source anywhere in the series is what itertools.chain
    cannot take, and the sync check is redone at every acquisition
    """

    async def collect(self, c):
        return [item async for item in c]

    async def test_driven_source_in_the_middle(self):
        got = await self.collect(KosChain([1, 2], agen([3, 4]), [5]))
        self.assertEqual(got, [1, 2, 3, 4, 5])

    async def test_driven_source_first(self):
        self.assertEqual(await self.collect(KosChain(agen([1, 2]), [3, 4])),
                         [1, 2, 3, 4])

    async def test_every_source_driven(self):
        got = await self.collect(KosChain(agen([1, 2]), agen([3, 4])))
        self.assertEqual(got, [1, 2, 3, 4])

    async def test_driven_empties_are_skipped(self):
        got = await self.collect(KosChain(agen([]), agen([]), [9]))
        self.assertEqual(got, [9])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.chain([1], agen([2])))

    async def test_source_error_propagates(self):
        async def boom():
            yield 1
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosChain(boom(), [2]))


class TestKosChainTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Close reaches whichever source is live, and only that one
    """

    async def test_close_reaches_the_live_source(self):
        first = ClosingSource([1, 2, 3])
        second = ClosingSource([4, 5])
        c = KosChain(first, second)

        self.assertEqual(next(iter(c)), 1)
        await c.aclose()

        self.assertTrue(first.closed)
        # the second was never reached, so never acquired to be closed
        self.assertFalse(second.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        c = KosChain(slow([1, 2]), [3])
        first = asyncio.ensure_future(c.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await c.__anext__()

        release.set()
        self.assertEqual(await first, 1)


class TestKosChainFromIterable(unittest.TestCase):
    """
    chain-from-iterable takes the series from one iterable
    """

    def test_series_from_one_iterable(self):
        from koskari.lib import KosChain as _c  # noqa: F401
        import koskari.lib as kl

        from_iterable = getattr(kl, 'chain-from-iterable')
        self.assertEqual(list(from_iterable(["AB", [1, 2]])),
                         ['A', 'B', 1, 2])

    def test_matches_itertools_from_iterable(self):
        import koskari.lib as kl

        from_iterable = getattr(kl, 'chain-from-iterable')
        for series in ((["AB", [1, 2]]), ([], [[1]]), ([["a"], [], ["b"]])):
            self.assertEqual(list(from_iterable(series)),
                             list(itertools.chain.from_iterable(series)),
                             series)


# The end.
