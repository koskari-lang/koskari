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
Tests for KosDropWhile: the latching predicate, the driven sources and
suspending predicates itertools.dropwhile cannot take, and parity with
the Python original

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosDropWhile


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


class TestKosDropWhileSync(unittest.TestCase):
    """
    A sync source with an immediate verdict drops or emits without the
    evaluator
    """

    def test_drops_the_leading_run(self):
        self.assertEqual(list(KosDropWhile(lambda x: x < 3, [1, 2, 3, 4])),
                         [3, 4])

    def test_predicate_retires_after_the_first_rejection(self):
        # the trailing 1 and 2 satisfy the predicate but are past the
        # break, so they are emitted untested
        self.assertEqual(
            list(KosDropWhile(lambda x: x < 3, [1, 2, 3, 4, 1, 2])),
            [3, 4, 1, 2])

    def test_predicate_never_rejects_yields_nothing(self):
        self.assertEqual(list(KosDropWhile(lambda x: True, [1, 2, 3])), [])

    def test_predicate_rejects_immediately_yields_everything(self):
        self.assertEqual(list(KosDropWhile(lambda x: False, [1, 2, 3])),
                         [1, 2, 3])

    def test_empty_source(self):
        self.assertEqual(list(KosDropWhile(lambda x: True, [])), [])

    def test_matches_itertools_dropwhile(self):
        for items in ([1, 2, 3, 4, 1, 2], [], [5], [1, 1, 1]):
            self.assertEqual(
                list(KosDropWhile(lambda x: x < 3, items)),
                list(itertools.dropwhile(lambda x: x < 3, items)),
                items)

    def test_exhausted_stays_exhausted(self):
        d = KosDropWhile(lambda x: x < 2, [1, 2])
        self.assertEqual(list(d), [2])
        self.assertEqual(list(d), [])


class TestKosDropWhileConstruction(unittest.TestCase):
    """
    The predicate is required; dropwhile has no truthiness fallback
    """

    def test_none_predicate_refused(self):
        with self.assertRaises(TypeError):
            KosDropWhile(None, [1, 2])

    def test_non_callable_predicate_refused(self):
        with self.assertRaises(TypeError):
            KosDropWhile(5, [1, 2])

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosDropWhile(lambda x: True, 5)


class TestKosDropWhileDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source or a suspending predicate is what
    itertools.dropwhile cannot take
    """

    async def collect(self, d):
        return [item async for item in d]

    async def test_async_source(self):
        got = await self.collect(
            KosDropWhile(lambda x: x < 3, agen([1, 2, 3, 4, 1])))
        self.assertEqual(got, [3, 4, 1])

    async def test_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x < 3

        self.assertEqual(await self.collect(KosDropWhile(slow, [1, 2, 3, 4])),
                         [3, 4])

    async def test_async_source_and_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x < 3

        got = await self.collect(KosDropWhile(slow, agen([1, 2, 3, 4, 1])))
        self.assertEqual(got, [3, 4, 1])

    async def test_predicate_retires_across_a_suspension(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x < 3

        # once retired the predicate is not consulted, so the trailing
        # values below 3 survive even though slow would accept them
        got = await self.collect(KosDropWhile(slow, agen([1, 3, 1, 2])))
        self.assertEqual(got, [3, 1, 2])

    async def test_itertools_dropwhile_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.dropwhile(lambda x: x < 3, agen([1, 2, 3])))

    async def test_predicate_error_propagates(self):
        def boom(x):
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosDropWhile(boom, [1, 2]))


class TestKosDropWhileTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source through the base's view
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource([1, 2, 3, 4])
        d = KosDropWhile(lambda x: x < 3, src)

        self.assertEqual(next(iter(d)), 3)
        await d.aclose()

        self.assertTrue(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(x):
            await release.wait()
            return False

        d = KosDropWhile(slow, [1, 2])
        first = asyncio.ensure_future(d.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await d.__anext__()

        release.set()
        self.assertEqual(await first, 1)


# The end.
