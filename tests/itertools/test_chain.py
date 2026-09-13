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
Tests for koskari.itertools chain.

The sync shapes live in data/chain_cases.ki and run at import time;
these tests import that module through the koskari import hook and
assert the collected results. The async-source shapes call chain from
Python and drive it with async iteration. chain is a native provider
reached through the koskari.itertools module, so the full stack
(reader, compiler, importer, bootstrap) must be available and these
run in the reproducers phase; the per-provider unit tests live in
tests/lib.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import importlib
import os
import sys
import unittest

import pytest

from koskari.importer import install


pytestmark = pytest.mark.phase_reproducers

_DATA_DIR = os.path.join(os.path.dirname(__file__), "data")


class TestChainSync(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        install()
        sys.path.insert(0, _DATA_DIR)
        sys.modules.pop("chain_cases", None)
        cls.cases = importlib.import_module("chain_cases")

    @classmethod
    def tearDownClass(cls):
        sys.modules.pop("chain_cases", None)
        sys.path.remove(_DATA_DIR)

    def case(self, name):
        return getattr(self.cases, name)

    def test_mixed_sync_sources_in_order(self):
        self.assertEqual(self.case("mixed"), [1, 2, 3, "a", "b"])

    def test_no_sources_is_empty(self):
        self.assertEqual(self.case("no-sources"), [])

    def test_one_source_passes_through(self):
        self.assertEqual(self.case("one-source"), [0, 1, 2])

    def test_empty_sources_contribute_nothing(self):
        self.assertEqual(self.case("empty-sources"), [1, 2])

    def test_chains_nest(self):
        self.assertEqual(self.case("nested"), [1, 2, 3])

    def test_koskari_generator_source(self):
        self.assertEqual(self.case("generator-source"), [10, 20, 30])

    def test_later_sources_start_only_when_reached(self):
        self.assertEqual(self.case("lazy-first"), 1)
        self.assertEqual(self.case("visits-after-first"), [1])
        self.assertEqual(self.case("lazy-rest"), [2, 3])
        self.assertEqual(list(self.case("visits")), [1, 2, 3])


async def async_source(*items):
    for item in items:
        yield item


class TestChainAsync(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        install()
        self.chain = importlib.import_module("koskari.itertools").chain

    async def drain(self, gen):
        return [x async for x in gen]

    async def test_async_sources(self):
        gen = self.chain(async_source(1, 2), async_source(3))
        self.assertEqual(await self.drain(gen), [1, 2, 3])

    async def test_async_and_sync_sources_mix(self):
        gen = self.chain([1], async_source(2), (3,))
        self.assertEqual(await self.drain(gen), [1, 2, 3])

    async def test_sync_sources_under_async_drive(self):
        gen = self.chain(range(2), "ab")
        self.assertEqual(await self.drain(gen), [0, 1, "a", "b"])


# The end.
