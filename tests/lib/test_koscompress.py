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
Tests for KosCompress: the selector deciding each data element,
either source ending the compress, and the driven sources
itertools.compress cannot take

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import itertools
import unittest

import pytest

from koskari.lib import KosCompress


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


class TestKosCompressSync(unittest.TestCase):
    """
    Rows run entirely in C while both sources are sync
    """

    def test_selector_keeps_and_drops(self):
        self.assertEqual(list(KosCompress("ABCDEF", [1, 0, 1, 0, 1, 1])),
                         ['A', 'C', 'E', 'F'])

    def test_every_selector_true(self):
        self.assertEqual(list(KosCompress("ABC", [1, 1, 1])),
                         ['A', 'B', 'C'])

    def test_every_selector_false(self):
        self.assertEqual(list(KosCompress("ABC", [0, 0, 0])), [])

    def test_short_selector_ends_it(self):
        self.assertEqual(list(KosCompress("ABCDEF", [1, 1])), ['A', 'B'])

    def test_short_data_ends_it(self):
        self.assertEqual(list(KosCompress("AB", [1, 1, 1, 1])), ['A', 'B'])

    def test_empty_data(self):
        self.assertEqual(list(KosCompress("", [1, 2, 3])), [])

    def test_empty_selectors(self):
        self.assertEqual(list(KosCompress("ABC", [])), [])

    def test_truthiness_not_equality(self):
        self.assertEqual(
            list(KosCompress("ABCDE", [1, 0, '', None, 'x'])), ['A', 'E'])

    def test_matches_itertools_compress(self):
        cases = (("ABCDEF", [1, 0, 1, 0, 1, 1]), ("ABC", [1, 1, 1]),
                 ("ABC", [0, 0, 0]), ("ABCDEF", [1, 1]),
                 ("AB", [1, 1, 1, 1]), ("", [1, 2, 3]), ("ABC", []))
        for data, selectors in cases:
            self.assertEqual(list(KosCompress(data, selectors)),
                             list(itertools.compress(data, selectors)),
                             (data, selectors))

    def test_exhausted_stays_exhausted(self):
        c = KosCompress("AB", [1, 1])
        self.assertEqual(list(c), ['A', 'B'])
        self.assertEqual(list(c), [])


class TestKosCompressConstruction(unittest.TestCase):
    """
    compress takes both sources and no callable
    """

    def test_missing_selectors_refused(self):
        with self.assertRaises(TypeError):
            KosCompress("ABC")

    def test_third_argument_refused(self):
        with self.assertRaises(TypeError):
            KosCompress("ABC", [1], [2])

    def test_non_iterable_data_refused(self):
        with self.assertRaises(TypeError):
            KosCompress(5, [1])

    def test_non_iterable_selectors_refused(self):
        with self.assertRaises(TypeError):
            KosCompress("ABC", 5)

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosCompress("ABC", selectors=[1])


class TestKosCompressDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source on either side is what itertools.compress cannot
    take
    """

    async def collect(self, c):
        return [item async for item in c]

    async def test_driven_data(self):
        got = await self.collect(
            KosCompress(agen("ABCDEF"), [1, 0, 1, 0, 1, 1]))
        self.assertEqual(got, ['A', 'C', 'E', 'F'])

    async def test_driven_selectors(self):
        got = await self.collect(
            KosCompress("ABCDEF", agen([1, 0, 1, 0, 1, 1])))
        self.assertEqual(got, ['A', 'C', 'E', 'F'])

    async def test_both_driven(self):
        got = await self.collect(
            KosCompress(agen("ABCDEF"), agen([1, 0, 1, 0, 1, 1])))
        self.assertEqual(got, ['A', 'C', 'E', 'F'])

    async def test_long_rejected_run_across_suspensions(self):
        got = await self.collect(
            KosCompress(agen("ABCDEF"), agen([0, 0, 0, 0, 0, 1])))
        self.assertEqual(got, ['F'])

    async def test_driven_selectors_run_out_first(self):
        got = await self.collect(KosCompress(agen("ABCDEF"), agen([1, 1])))
        self.assertEqual(got, ['A', 'B'])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.compress(agen("AB"), [1, 1]))

    async def test_source_error_propagates(self):
        async def boom():
            yield 'A'
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.collect(KosCompress(boom(), [1, 1, 1]))


class TestKosCompressTeardown(unittest.IsolatedAsyncioTestCase):
    """
    Teardown reaches both sources through the base's view
    """

    async def test_close_reaches_both_sources(self):
        data = ClosingSource("ABCD")
        selectors = ClosingSource([1, 1, 1, 1])
        c = KosCompress(data, selectors)

        self.assertEqual(next(iter(c)), 'A')
        await c.aclose()

        self.assertTrue(data.closed)
        self.assertTrue(selectors.closed)

    async def test_concurrent_step_refused(self):
        release = asyncio.Event()

        async def slow(items):
            for item in items:
                await release.wait()
                yield item

        c = KosCompress(slow("AB"), [1, 1])
        first = asyncio.ensure_future(c.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await c.__anext__()

        release.set()
        self.assertEqual(await first, 'A')


# The end.
