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
tests.reproducers.test_item_slot_rebind_getter

An item handler that borrows its container from a fast slot must hold
it once fetching a later operand can run user code. Here the key is a
bound attribute ref whose getter rebinds the container's slot, and the
handler must still index the list it loaded.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'item_slot_rebind_getter.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class Tracked(list):
    """
    A list that records every store and its own release
    """

    log = None

    def __setitem__(self, key, val):
        self.log.append(("set", key, val))
        list.__setitem__(self, key, val)

    def __del__(self):
        self.log.append(("del", list(self)))


class Rebinder:
    """
    Holds a ref to the container's slot. Reading k stores None through
    it, dropping the container's only reference, then answers 1
    """

    def __init__(self, ref):
        self.ref = ref

    @property
    def k(self):
        self.ref.store(None)
        return 1


class TestItemSlotRebindGetter(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.item_slot_rebind_getter')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    def setUp(self):
        self.log = []
        Tracked.log = self.log


    async def test_load_indexes_the_loaded_list(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST, opcodes_of(mod.load))
        self.assertEqual(await mod.load(Tracked, Rebinder), "b")
        self.assertEqual(self.log, [("del", ["a", "b", "c"])])


    async def test_store_lands_in_the_loaded_list(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST_STORE_FAST, opcodes_of(mod.store))
        self.assertIsNone(await mod.store(Tracked, Rebinder))
        self.assertEqual(self.log, [
            ("set", 1, "stored"),
            ("del", ["a", "stored", "c"]),
        ])


# The end.
