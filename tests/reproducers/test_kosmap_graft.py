# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_kosmap_graft

Pins for the KosMap in-eval invoke graft: a koskari function mapped
over a source and consumed by koskari for-each runs in the consumer's
evaluator -- the function frame grafts above the armed iterator
boundary and its return delivers as the element, with no coroutine at
any step. Covers the demand drive, the asyncio drive, awaits inside the
mapped function, error propagation, and the map-function StopIteration
exhaustion parity with builtins.map.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari.lib import KosMap
from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define inc [x]:
    (+ x 1)

define slow-inc [x]:
    (await (sleep 0.001))
    (+ x 1)

define boom [x]:
    raise (ValueError "boom")

define stopper [x]:
    if (== x 2):
        raise (StopIteration)
    x

define consume [it]:
    define total 0
    for-each [v it]:
        set! total (+ total v)
    total
"""


class TestKosMapInvokeGraft(unittest.IsolatedAsyncioTestCase):
    """
    koskari for-each over a KosMap of a koskari function: the invoke
    grafts in-eval and the frame return delivers as the element
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_graft_demand_drive(self):
        mod = await self.load("kmg_demand")
        v = vars(mod)
        result = v["consume"](KosMap(v["inc"], [1, 2, 3])).demand()
        self.assertEqual(result, 9)

    async def test_graft_async_drive(self):
        mod = await self.load("kmg_async")
        v = vars(mod)
        result = await v["consume"](KosMap(v["inc"], [10, 20]))
        self.assertEqual(result, 32)

    async def test_graft_await_inside_function(self):
        mod = await self.load("kmg_await")
        v = vars(mod)
        result = await v["consume"](KosMap(v["slow-inc"], [1, 2]))
        self.assertEqual(result, 5)

    async def test_graft_error_propagates(self):
        mod = await self.load("kmg_boom")
        v = vars(mod)
        with self.assertRaises(ValueError):
            await v["consume"](KosMap(v["boom"], [1]))
        # the map is reusable state-wise: not busy, not closed
        m = KosMap(v["inc"], [5])
        self.assertEqual(await v["consume"](m), 6)

    async def test_graft_stopiteration_exhausts(self):
        """
        a mapped function raising StopIteration ends the iteration,
        matching builtins.map truncation
        """
        mod = await self.load("kmg_stop")
        v = vars(mod)
        result = v["consume"](KosMap(v["stopper"], [1, 2, 3])).demand()
        self.assertEqual(result, 1)


# The end.
