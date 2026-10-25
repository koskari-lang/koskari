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

Tests for the KosMap in-eval invoke graft: a koskari function mapped
over a source and consumed by koskari for-each runs in the consumer's
evaluator -- the function frame grafts above the iterator boundary
with its request set and its return delivers as the element, with no
coroutine at any step. Covers the demand drive, the asyncio drive,
awaits inside the mapped function, error propagation, and the
map-function StopIteration
exhaustion parity with builtins.map.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.lib import KosFilter, KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep
import-from operator neg

define inc [x]:
    (+ x 1)

define slow-inc [x]:
    (await (sleep 0.001))
    (+ x 1)

define boom [x]:
    raise (ValueError "boom")

define boom-at-one [x]:
    when (== x 1):
        raise (ValueError "boom")
    x

define raising-gen [n]:
    for-each [i (range n)]:
        when (== i 1):
            raise (ValueError "gen")
        yield i

define stopper [x]:
    if (== x 2):
        raise (StopIteration)
    x

define py-name [x]:
    (str x)

define call-in-try [x]:
    try:
        (py-name x)
    except [e Exception]:
        "no"

define py-neg [x]:
    (neg x)

define map-lambda [xs]:
    (collect (map (lambda [x] (neg x)) xs))

define consume [it]:
    define total 0
    for-each [v it]:
        set! total (+ total v)
    total

define collect [it]:
    define out #[]
    for-each [v it]:
        out.append v
    out

define consume-caught [it]:
    try:
        (consume it)
    except [e ValueError]:
        (str e)
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

    async def test_map_is_usable_after_its_function_raised(self):
        """
        a map whose mapped function raised mid-iteration is finished
        when the loop frame declines the failure, so a later loop over
        the same map carries on from the next element
        """
        mod = await self.load("kmg_reuse_map")
        v = vars(mod)
        m = KosMap(v["boom-at-one"], [1, 2, 3])
        self.assertEqual(await v["consume-caught"](m), "boom")
        self.assertEqual(await v["consume"](m), 5)

    async def test_filter_is_usable_after_its_predicate_raised(self):
        mod = await self.load("kmg_reuse_filter")
        v = vars(mod)
        f = KosFilter(v["boom-at-one"], [1, 2, 3])
        self.assertEqual(await v["consume-caught"](f), "boom")
        self.assertEqual(await v["consume"](f), 5)

    async def test_map_is_usable_after_its_source_raised(self):
        mod = await self.load("kmg_reuse_source")
        v = vars(mod)
        m = KosMap(v["inc"], v["raising-gen"](3))
        self.assertEqual(await v["consume-caught"](m), "gen")
        self.assertEqual(await v["consume"](m), 0)

    async def test_mapped_function_tail_calling_python(self):
        """
        a mapped Koskari function whose body is a tail call to a Python
        callable delivers that call's value as the element
        """
        mod = await self.load("kmg_py_tail")
        v = vars(mod)
        self.assertEqual(await v["collect"](KosMap(v["py-name"], [1, 2, 3])),
                         ["1", "2", "3"])
        self.assertEqual(await v["collect"](KosMap(v["py-neg"], [1, 2, 3])),
                         [-1, -2, -3])
        self.assertEqual(await v["map-lambda"]([1, 2, 3]), [-1, -2, -3])
        self.assertEqual(await v["call-in-try"](7), "7")

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
