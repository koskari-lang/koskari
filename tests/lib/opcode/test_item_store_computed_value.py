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
tests.lib.opcode.test_item_store_computed_value

A constant-key item store whose value comes from a local bound to a
computing ref: the deref builds a fresh object each time, and the store
must hold that only reference until the container has its own.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define store_into_list (lambda [obj lst]
  (bind! v (attr obj fresh))
  (set! (item lst 0) v)
  lst))

(define store_into_dict (lambda [obj d]
  (bind! v (attr obj fresh))
  (set! (item d "k") v)
  d))
"""


class Source:
    def __init__(self):
        self.reads = 0

    @property
    def fresh(self):
        self.reads += 1
        return [1, 2, 3]


class TestItemStoreComputedValue(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("item_store_computed_value")
        vars(module)["__file__"] = "<item_store_computed_value>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_list_store_keeps_the_fresh_value(self):
        mod = await self.load()
        src = Source()
        for _ in range(30):
            lst = await mod["store_into_list"](src, [None])
            self.assertEqual(lst, [[1, 2, 3]])
            self.assertEqual(sys.getrefcount(lst[0]), 2)
        self.assertEqual(src.reads, 30)

    async def test_dict_store_keeps_the_fresh_value(self):
        mod = await self.load()
        src = Source()
        for _ in range(30):
            d = await mod["store_into_dict"](src, {})
            self.assertEqual(d, {"k": [1, 2, 3]})
            self.assertEqual(sys.getrefcount(d["k"]), 2)
        self.assertEqual(src.reads, 30)


# The end.
