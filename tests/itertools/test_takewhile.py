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
Tests for koskari.itertools takewhile.

The sync shapes live in data/takewhile_cases.ki and run at import time;
these tests import that module through the koskari import hook and
assert the collected results. The async-source shapes call takewhile
from Python and drive it with async iteration. takewhile is a native
provider reached through the koskari.itertools module, so the full
stack (reader, compiler, importer, bootstrap) must be available and
these run in the reproducers phase; the per-provider unit tests live
in tests/lib.

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


class TestTakewhileSync(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        install()
        sys.path.insert(0, _DATA_DIR)
        sys.modules.pop("takewhile_cases", None)
        cls.cases = importlib.import_module("takewhile_cases")

    @classmethod
    def tearDownClass(cls):
        sys.modules.pop("takewhile_cases", None)
        sys.path.remove(_DATA_DIR)

    def case(self, name):
        return getattr(self.cases, name)

    def test_yields_the_passing_prefix(self):
        self.assertEqual(self.case("prefix"), [0, 1, 2])

    def test_empty_source_is_empty(self):
        self.assertEqual(self.case("empty-source"), [])

    def test_all_passing_yields_everything(self):
        self.assertEqual(self.case("all-pass"), [0, 1, 2])

    def test_first_failing_yields_nothing(self):
        self.assertEqual(self.case("none-pass"), [])

    def test_python_callable_predicate(self):
        self.assertEqual(self.case("python-predicate"), ["1", "2"])

    def test_stops_at_the_first_failure_and_leaves_the_rest(self):
        self.assertEqual(self.case("stops-early"), [0, 1, 2])
        self.assertEqual(self.case("visited"), [0, 1, 2, 3])


async def async_source(*items):
    for item in items:
        yield item


class TestTakewhileAsync(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        install()
        sys.path.insert(0, _DATA_DIR)
        sys.modules.pop("takewhile_cases", None)
        self.cases = importlib.import_module("takewhile_cases")
        self.takewhile = importlib.import_module("koskari.itertools").takewhile

    async def asyncTearDown(self):
        sys.modules.pop("takewhile_cases", None)
        sys.path.remove(_DATA_DIR)

    async def drain(self, gen):
        return [x async for x in gen]

    async def test_async_source(self):
        pred = getattr(self.cases, "small?")
        gen = self.takewhile(pred, async_source(1, 2, 5, 1))
        self.assertEqual(await self.drain(gen), [1, 2])

    async def test_awaiting_predicate_over_async_source(self):
        pred = getattr(self.cases, "slow-small?")
        gen = self.takewhile(pred, async_source(0, 1, 7, 0))
        self.assertEqual(await self.drain(gen), [0, 1])

    async def test_python_predicate_under_async_drive(self):
        gen = self.takewhile(lambda x: x < 2, range(5))
        self.assertEqual(await self.drain(gen), [0, 1])


# The end.
