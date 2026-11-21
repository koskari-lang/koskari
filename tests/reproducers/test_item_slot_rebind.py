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
tests.reproducers.test_item_slot_rebind

Full-stack reproducer for an item site whose container is a captured
slot and whose key rebinds that slot from its hash: the dict body
fetches the container borrowed, the hash runs the closure, and the
site must finish the access on the container it fetched, answering
KeyError for a load and storing into the old container for a store,
with the rebound slot holding the new one afterward.

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
    os.path.dirname(__file__), 'scripts', 'item_slot_rebind.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestItemSlotRebind(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.item_slot_rebind')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_load_under_a_rebinding_key(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST, opcodes_of(mod.load))
        seen = []
        for _ in range(3):
            with self.assertRaises(KeyError):
                await mod.load()
            seen.append((await mod.current())[0])
        self.assertEqual(len({id(d) for d in seen}), 3)


    async def test_inplace_under_a_rebinding_key(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST_INPLACE_CONST, opcodes_of(mod.incr))
        seen = []
        for _ in range(3):
            with self.assertRaises(KeyError):
                await mod.incr()
            seen.append((await mod.current())[1])
        self.assertEqual(len({id(d) for d in seen}), 3)


    async def test_inplace_guard_miss_under_a_rebinding_key(self):
        # an int cell elects the int body; a str cell then misses that
        # body's guard after the hash ran, and the protocol raises for
        # the container the row fetched
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST_INPLACE_CONST, opcodes_of(mod.miss))
        await mod.prime(1)
        primed = (await mod.current())[3]
        after = await mod.miss()
        self.assertEqual(primed, {mod.km: 2})
        self.assertIsNot(after, primed)
        seen = []
        for _ in range(3):
            await mod.prime("text")
            with self.assertRaises(TypeError):
                await mod.miss()
            seen.append((await mod.current())[3])
        self.assertEqual(len({id(d) for d in seen}), 3)


    async def test_store_under_a_rebinding_key(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_ITEM_FAST_STORE_FAST, opcodes_of(mod.store))
        first = mod.first
        for i in range(3):
            # the store goes into the container the row fetched, and the
            # slot then holds the fresh one the hash bound
            after = await mod.store(i)
            self.assertEqual(after, {})
            self.assertIsNot(after, first)
        self.assertEqual(list(first.values()), [0])


# The end.
