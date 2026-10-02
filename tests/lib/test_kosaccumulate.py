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
Tests for KosAccumulate: the running total across steps, the seed
emitted before the source is touched, and the driven sources and
suspending folds itertools.accumulate cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import operator
import unittest

import pytest

from koskari.lib import KosAccumulate


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


class TestKosAccumulateSync(unittest.TestCase):
    """
    A sync source with an immediate fold runs without the evaluator
    """

    def test_running_sum_is_the_default(self):
        self.assertEqual(list(KosAccumulate([1, 2, 3, 4])), [1, 3, 6, 10])

    def test_explicit_function(self):
        self.assertEqual(list(KosAccumulate([1, 2, 3, 4], operator.mul)),
                         [1, 2, 6, 24])

    def test_none_function_is_the_default_fold(self):
        self.assertEqual(list(KosAccumulate([1, 2, 3], None)), [1, 3, 6])

    def test_first_element_is_emitted_unfolded(self):
        self.assertEqual(list(KosAccumulate([5])), [5])

    def test_folds_whatever_add_accepts(self):
        self.assertEqual(list(KosAccumulate(['a', 'b', 'c'])),
                         ['a', 'ab', 'abc'])

    def test_empty_source(self):
        self.assertEqual(list(KosAccumulate([])), [])

    def test_seed_precedes_the_source(self):
        self.assertEqual(list(KosAccumulate([1, 2, 3], initial=10)),
                         [10, 11, 13, 16])

    def test_seed_over_an_empty_source_still_yields(self):
        self.assertEqual(list(KosAccumulate([], initial=10)), [10])

    def test_seed_with_a_function(self):
        self.assertEqual(
            list(KosAccumulate([1, 2, 3], operator.mul, initial=2)),
            [2, 2, 4, 12])

    def test_none_seed_is_no_seed(self):
        self.assertEqual(list(KosAccumulate([1, 2, 3], initial=None)),
                         [1, 3, 6])

    def test_function_by_keyword(self):
        self.assertEqual(
            list(KosAccumulate([1, 2, 3], func=operator.mul)),
            [1, 2, 6])

    def test_none_function_by_keyword_with_a_seed(self):
        self.assertEqual(
            list(KosAccumulate([1, 2, 3], func=None, initial=5)),
            [5, 6, 8, 11])

    def test_matches_itertools_accumulate(self):
        cases = ((([1, 2, 3, 4],), {}),
                 (([1, 2, 3, 4], operator.mul), {}),
                 (([1, 2, 3], None), {}),
                 (([1, 2, 3],), {'initial': 10}),
                 (([],), {'initial': 10}),
                 (([],), {}),
                 (([5],), {}),
                 ((['a', 'b', 'c'],), {}))
        for args, kwargs in cases:
            self.assertEqual(list(KosAccumulate(*args, **kwargs)),
                             list(itertools.accumulate(*args, **kwargs)),
                             (args, kwargs))

    def test_exhausted_stays_exhausted(self):
        a = KosAccumulate([1, 2])
        self.assertEqual(list(a), [1, 3])
        self.assertEqual(list(a), [])


class TestKosAccumulateConstruction(unittest.TestCase):
    """
    The function is optional, by position or keyword; initial is
    keyword-only
    """

    def test_non_callable_function_refused(self):
        with self.assertRaises(TypeError):
            KosAccumulate([1, 2], 5)

    def test_unknown_keyword_refused(self):
        with self.assertRaises(TypeError):
            KosAccumulate([1, 2], bogus=3)

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosAccumulate(5)

    def test_missing_source_refused(self):
        with self.assertRaises(TypeError):
            KosAccumulate()

    def test_third_positional_refused(self):
        with self.assertRaises(TypeError):
            KosAccumulate([1, 2], operator.add, 3)

    def test_unfoldable_elements_raise_at_the_step(self):
        with self.assertRaises(TypeError):
            list(KosAccumulate([1, 'a']))


class TestKosAccumulateDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source or a suspending fold is what itertools.accumulate
    cannot take
    """

    async def collect(self, a):
        return [item async for item in a]

    async def test_async_source(self):
        self.assertEqual(await self.collect(KosAccumulate(agen([1, 2, 3, 4]))),
                         [1, 3, 6, 10])

    async def test_suspending_fold(self):
        async def slow(x, y):
            await asyncio.sleep(0)
            return x + y

        self.assertEqual(
            await self.collect(KosAccumulate([1, 2, 3, 4], slow)),
            [1, 3, 6, 10])

    async def test_async_source_and_suspending_fold(self):
        async def slow(x, y):
            await asyncio.sleep(0)
            return x + y

        self.assertEqual(
            await self.collect(KosAccumulate(agen([1, 2, 3, 4]), slow)),
            [1, 3, 6, 10])

    async def test_seed_with_a_driven_source(self):
        got = await self.collect(KosAccumulate(agen([1, 2, 3]), initial=10))
        self.assertEqual(got, [10, 11, 13, 16])

    async def test_total_survives_between_steps(self):
        a = KosAccumulate(agen([1, 2, 3]))
        step = a.__aiter__()

        self.assertEqual(await step.__anext__(), 1)
        # the running total carried across the step boundary
        self.assertEqual(await step.__anext__(), 3)

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.accumulate(agen([1, 2])))

    async def test_fold_error_propagates(self):
        def boom(x, y):
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosAccumulate([1, 2], boom))


class TestKosAccumulateTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches the source through the base's view
    """

    async def test_close_reaches_the_source(self):
        src = ClosingSource([1, 2, 3])
        a = KosAccumulate(src)

        self.assertEqual(next(iter(a)), 1)
        await a.aclose()

        self.assertTrue(src.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(x, y):
            await release.wait()
            return x + y

        a = KosAccumulate([1, 2, 3], slow)

        # the first element primes the total and is emitted with no
        # fold at all, so it is the second step that can park
        self.assertEqual(await a.__anext__(), 1)

        second = asyncio.ensure_future(a.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await a.__anext__()

        release.set()
        self.assertEqual(await second, 3)


# The end.
