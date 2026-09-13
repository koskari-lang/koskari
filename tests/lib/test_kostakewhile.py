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
Tests for KosTakeWhile: the run ending at the first rejection, the
rejecting element consumed with the source left open, and the driven
sources itertools.takewhile cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosTakeWhile


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


class TestKosTakeWhileSync(unittest.TestCase):
    """
    The run ends at the first rejection and does not resume
    """

    def test_takes_the_leading_run(self):
        self.assertEqual(list(KosTakeWhile(lambda x: x < 3, [1, 2, 3, 4])),
                         [1, 2])

    def test_does_not_resume_after_a_rejection(self):
        # the trailing 1 and 2 satisfy the predicate but come after
        self.assertEqual(
            list(KosTakeWhile(lambda x: x < 3, [1, 2, 3, 4, 1, 2])),
            [1, 2])

    def test_predicate_never_rejects_takes_everything(self):
        self.assertEqual(list(KosTakeWhile(lambda x: True, [1, 2, 3])),
                         [1, 2, 3])

    def test_predicate_rejects_immediately_takes_nothing(self):
        self.assertEqual(list(KosTakeWhile(lambda x: False, [1, 2, 3])), [])

    def test_empty_source(self):
        self.assertEqual(list(KosTakeWhile(lambda x: True, [])), [])

    def test_matches_itertools_takewhile(self):
        cases = ((lambda x: x < 3, [1, 2, 3, 4, 1, 2]),
                 (lambda x: x < 10, [1, 2, 3]),
                 (lambda x: False, [1, 2, 3]),
                 (lambda x: True, [1, 2, 3]),
                 (lambda x: True, []))
        for pred, src in cases:
            self.assertEqual(list(KosTakeWhile(pred, src)),
                             list(itertools.takewhile(pred, src)), src)

    def test_rejecting_element_is_consumed_and_the_rest_left(self):
        src = iter([1, 2, 3, 4, 5])
        self.assertEqual(list(KosTakeWhile(lambda x: x < 3, src)), [1, 2])
        # 3 was taken from the source to be tested, and dropped
        self.assertEqual(list(src), [4, 5])

    def test_exhausted_stays_exhausted(self):
        t = KosTakeWhile(lambda x: x < 3, [1, 2, 3])
        self.assertEqual(list(t), [1, 2])
        self.assertEqual(list(t), [])


class TestKosTakeWhileConstruction(unittest.TestCase):
    """
    The predicate is required; takewhile has no truthiness fallback
    """

    def test_none_predicate_refused(self):
        with self.assertRaises(TypeError):
            KosTakeWhile(None, [1, 2])

    def test_non_callable_predicate_refused(self):
        with self.assertRaises(TypeError):
            KosTakeWhile(5, [1, 2])

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosTakeWhile(lambda x: True, 5)

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosTakeWhile(lambda x: True, iterable=[1])


class TestKosTakeWhileDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source or a suspending predicate is what
    itertools.takewhile cannot take
    """

    async def collect(self, t):
        return [item async for item in t]

    async def test_async_source(self):
        got = await self.collect(
            KosTakeWhile(lambda x: x < 3, agen([1, 2, 3, 4, 1])))
        self.assertEqual(got, [1, 2])

    async def test_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x < 3

        self.assertEqual(
            await self.collect(KosTakeWhile(slow, [1, 2, 3, 4, 1])), [1, 2])

    async def test_async_source_and_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x < 3

        got = await self.collect(KosTakeWhile(slow, agen([1, 2, 3, 4, 1])))
        self.assertEqual(got, [1, 2])

    async def test_rejection_across_a_suspension_ends_the_run(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x != 2

        got = await self.collect(KosTakeWhile(slow, agen([1, 2, 3, 1])))
        self.assertEqual(got, [1])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.takewhile(lambda x: True, agen([1, 2])))

    async def test_predicate_error_propagates(self):
        def boom(x):
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosTakeWhile(boom, [1, 2]))


class TestKosTakeWhileTeardown(unittest.IsolatedAsyncioTestCase):
    """
    A run ended by a rejection leaves the source open; an explicit
    close still reaches it
    """

    async def test_close_before_the_run_ends_reaches_the_source(self):
        src = ClosingSource([1, 2, 3, 4])
        t = KosTakeWhile(lambda x: x < 3, src)

        self.assertEqual(next(iter(t)), 1)
        await t.aclose()

        self.assertTrue(src.closed)

    async def test_rejection_does_not_close_the_source(self):
        src = ClosingSource([1, 2, 3, 4])

        self.assertEqual(list(KosTakeWhile(lambda x: x < 3, src)), [1, 2])
        self.assertFalse(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(x):
            await release.wait()
            return True

        t = KosTakeWhile(slow, [1, 2])
        first = asyncio.ensure_future(t.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await t.__anext__()

        release.set()
        self.assertEqual(await first, 1)


# The end.
