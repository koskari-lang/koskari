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
tests.reproducers.test_transform_async_source_end

A transform whose source is a python async iterator that suspends
before ending, consumed inside the evaluator by a koskari for-each:
the source's end is the transform's end, for a map, a chain of two
such sources, and a zip of one with a list.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KosChain, KosMap, KosZip
from koskari.module import load_module_source_async, new_module
from tests import SlowAIter


pytestmark = pytest.mark.phase_reproducers


SRC = """
define collect [it]:
    define out #[]
    for-each [x it]:
        out.append x
    out
"""


class TestTransformAsyncSourceEnd(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return vars(module)["collect"]

    async def test_map_over_a_suspending_async_source_ends(self):
        collect = await self.load("tase_map")
        self.assertEqual(
            await collect(KosMap(lambda x: x, SlowAIter([0, 1]))), [0, 1])

    async def test_chain_moves_on_when_a_suspending_source_ends(self):
        collect = await self.load("tase_chain")
        self.assertEqual(
            await collect(KosChain(SlowAIter([1]), SlowAIter([2]))), [1, 2])

    async def test_zip_ends_with_its_suspending_source(self):
        collect = await self.load("tase_zip")
        self.assertEqual(
            await collect(KosZip(SlowAIter([1, 2]), [10, 20, 30])),
            [(1, 10), (2, 20)])


# The end.
