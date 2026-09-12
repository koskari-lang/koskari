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
Tests for filterfalse, which is a filter carrying an inverted verdict
rather than a type of its own, and for filter keeping its own
behaviour beside it

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosFilter, filterfalse


pytestmark = pytest.mark.phase_lib


async def agen(items):
    """
    An async iterator that suspends before each element
    """

    for item in items:
        await asyncio.sleep(0)
        yield item


def odd(x):
    return x % 2


class TestFilterFalseSync(unittest.TestCase):
    """
    filterfalse keeps what filter drops
    """

    def test_keeps_the_rejected(self):
        self.assertEqual(list(filterfalse(odd, [1, 2, 3, 4, 5, 6])),
                         [2, 4, 6])

    def test_none_predicate_keeps_falsey(self):
        self.assertEqual(list(filterfalse(None, [0, 1, '', 2, None, 3])),
                         [0, '', None])

    def test_predicate_always_true_keeps_nothing(self):
        self.assertEqual(list(filterfalse(lambda x: True, [1, 2])), [])

    def test_predicate_always_false_keeps_everything(self):
        self.assertEqual(list(filterfalse(lambda x: False, [1, 2])), [1, 2])

    def test_empty_source(self):
        self.assertEqual(list(filterfalse(None, [])), [])

    def test_matches_itertools_filterfalse(self):
        cases = ((odd, [1, 2, 3, 4, 5, 6]), (None, [0, 1, '', 2, None, 3]),
                 (lambda x: x > 2, [1, 2, 3, 4]), (None, []))
        for pred, src in cases:
            self.assertEqual(list(filterfalse(pred, src)),
                             list(itertools.filterfalse(pred, src)), src)

    def test_is_a_filter_carrying_the_flag(self):
        self.assertIsInstance(filterfalse(None, [1]), KosFilter)

    def test_exhausted_stays_exhausted(self):
        f = filterfalse(odd, [1, 2])
        self.assertEqual(list(f), [2])
        self.assertEqual(list(f), [])


class TestFilterUnchanged(unittest.TestCase):
    """
    filter keeps its own verdict with the flag clear
    """

    def test_filter_still_keeps_the_accepted(self):
        self.assertEqual(list(KosFilter(odd, [1, 2, 3, 4, 5, 6])),
                         [1, 3, 5])

    def test_filter_none_predicate_still_keeps_truthy(self):
        self.assertEqual(list(KosFilter(None, [0, 1, '', 2, None, 3])),
                         [1, 2, 3])

    def test_the_two_partition_the_source(self):
        src = [0, 1, 2, 3, 4, 5]
        self.assertEqual(sorted(list(KosFilter(odd, src))
                                + list(filterfalse(odd, src))),
                         src)


class TestFilterFalseConstruction(unittest.TestCase):
    """
    filterfalse takes the same arguments filter does
    """

    def test_non_callable_predicate_refused(self):
        with self.assertRaises(TypeError):
            filterfalse(5, [1, 2])

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            filterfalse(None, 5)

    def test_missing_source_refused(self):
        with self.assertRaises(TypeError):
            filterfalse(None)


class TestFilterFalseDriven(unittest.IsolatedAsyncioTestCase):
    """
    The inverted verdict survives every suspension the filter path has
    """

    async def collect(self, f):
        return [item async for item in f]

    async def test_async_source(self):
        got = await self.collect(filterfalse(odd, agen([1, 2, 3, 4, 5, 6])))
        self.assertEqual(got, [2, 4, 6])

    async def test_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x % 2

        self.assertEqual(
            await self.collect(filterfalse(slow, [1, 2, 3, 4, 5, 6])),
            [2, 4, 6])

    async def test_async_source_and_suspending_predicate(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x % 2

        got = await self.collect(filterfalse(slow, agen([1, 2, 3, 4, 5, 6])))
        self.assertEqual(got, [2, 4, 6])

    async def test_filter_unchanged_on_the_same_driven_path(self):
        async def slow(x):
            await asyncio.sleep(0)
            return x % 2

        got = await self.collect(KosFilter(slow, agen([1, 2, 3, 4, 5, 6])))
        self.assertEqual(got, [1, 3, 5])

    async def test_long_rejected_run_across_suspensions(self):
        got = await self.collect(
            filterfalse(lambda x: x < 5, agen([1, 2, 3, 4, 5])))
        self.assertEqual(got, [5])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.filterfalse(odd, agen([1, 2])))


# The end.
