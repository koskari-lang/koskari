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
Tests for koskari.itertools groupby.

The sync shapes live in data/groupby_cases.ki and run through the
koskari import hook; the async-source shapes call groupby from Python
and drive it with async iteration. groupby is written in Koskari, so
the full stack (reader, compiler, importer, bootstrap) must be
available and these run in the reproducers phase.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import importlib
import itertools
import os
import sys
import unittest

import pytest

from koskari.importer import install


pytestmark = pytest.mark.phase_reproducers

_DATA_DIR = os.path.join(os.path.dirname(__file__), "data")


async def async_source(*items):
    for item in items:
        await asyncio.sleep(0)
        yield item


def flatten(result):
    return [(k, list(g)) for k, g in result]


class TestGroupbySync(unittest.TestCase):
    """
    The sync shapes, compiled and run through the import hook
    """

    @classmethod
    def setUpClass(cls):
        install()
        sys.path.insert(0, _DATA_DIR)
        sys.modules.pop("groupby_cases", None)
        cls.cases = importlib.import_module("groupby_cases")

    @classmethod
    def tearDownClass(cls):
        sys.modules.pop("groupby_cases", None)
        sys.path.remove(_DATA_DIR)

    def case(self, name):
        return getattr(self.cases, name)

    def test_runs_of_letters(self):
        self.assertEqual(self.case("runs-of-letters"),
                         flatten(itertools.groupby("AAABBBCCD")))

    def test_runs_of_numbers(self):
        self.assertEqual(self.case("runs-of-numbers"),
                         flatten(itertools.groupby([1, 1, 2, 2, 3])))

    def test_empty_source(self):
        self.assertEqual(self.case("empty-source"), [])

    def test_single_element(self):
        self.assertEqual(self.case("single-element"), [(1, [1])])

    def test_keyed_by_length(self):
        self.assertEqual(
            self.case("keyed-by-length"),
            flatten(itertools.groupby(["a", "bb", "cc", "d"], len)))

    def test_repeated_key_is_two_runs(self):
        self.assertEqual(self.case("repeated-key-is-two-runs"),
                         flatten(itertools.groupby([1, 2, 1])))

    def test_group_survives_the_next_group(self):
        # Python's groups share a cursor, so its first group would
        # read back empty once the parent had moved on
        self.assertEqual(self.case("surviving-first"), ['A', 'A'])
        self.assertEqual(self.case("surviving-second"), ['B', 'B'])


class TestGroupbyAsync(unittest.IsolatedAsyncioTestCase):
    """
    A driven source, which itertools.groupby cannot take at all
    """

    async def asyncSetUp(self):
        install()
        self.groupby = importlib.import_module("koskari.itertools").groupby

    async def drain(self, result):
        return [(k, list(g)) async for k, g in result]

    async def test_async_source(self):
        got = await self.drain(self.groupby(async_source(*"AAABBBCCD")))
        self.assertEqual(got, flatten(itertools.groupby("AAABBBCCD")))

    async def test_async_source_with_key(self):
        got = await self.drain(
            self.groupby(async_source("a", "bb", "cc", "d"), len))
        self.assertEqual(got,
                         flatten(itertools.groupby(["a", "bb", "cc", "d"],
                                                   len)))

    async def test_async_empty_source(self):
        self.assertEqual(await self.drain(self.groupby(async_source())), [])

    async def test_itertools_cannot_take_the_same_source(self):
        with self.assertRaises(TypeError):
            list(itertools.groupby(async_source("A", "B")))

    async def test_source_error_propagates(self):
        async def boom():
            yield 'A'
            raise ValueError("nope")

        with self.assertRaises(ValueError):
            await self.drain(self.groupby(boom()))


# The end.
