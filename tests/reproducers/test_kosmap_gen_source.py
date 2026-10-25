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
tests.reproducers.test_kosmap_gen_source

Tests for the flattened KosMap fetch over a koskari generator source:
the generator installs onto the driving evaluator and its yields,
suspensions, and completion arrive at the consumer frame's resume.
Covers the
python demand and asyncio drives, a koskari for-each consumer, awaits
inside the generator, nested map-over-map (including a generator at
the bottom), and source exhaustion cleanup.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define gen-plain []:
    yield 1
    yield 2
    yield 3

define gen-await []:
    (await (sleep 0.001))
    yield 10
    (await (sleep 0.001))
    yield 20

define inc [x]:
    (+ x 1)

define consume [it]:
    define total 0
    for-each [v it]:
        set! total (+ total v)
    total

define grab [x]:
    (shift k x)

define capture-attempt [m]:
    (reset (consume m))
"""


class TestKosMapGenSource(unittest.IsolatedAsyncioTestCase):
    """
    KosMap over a koskari generator source: the fetch flattens the
    generator into the driving evaluator
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_gen_source_demand_drive(self):
        mod = await self.load("kmgs_demand")
        v = vars(mod)
        result = list(KosMap(v["inc"], v["gen-plain"]()))
        self.assertEqual(result, [2, 3, 4])

    async def test_gen_source_async_drive(self):
        mod = await self.load("kmgs_async")
        v = vars(mod)
        result = [x async for x in KosMap(v["inc"], v["gen-plain"]())]
        self.assertEqual(result, [2, 3, 4])

    async def test_gen_source_python_function(self):
        mod = await self.load("kmgs_pyfn")
        v = vars(mod)
        result = list(KosMap(lambda x: x * 10, v["gen-plain"]()))
        self.assertEqual(result, [10, 20, 30])

    async def test_gen_source_kos_consumer(self):
        mod = await self.load("kmgs_consume")
        v = vars(mod)
        result = await v["consume"](KosMap(v["inc"], v["gen-plain"]()))
        self.assertEqual(result, 9)

    async def test_gen_source_await_inside(self):
        mod = await self.load("kmgs_await")
        v = vars(mod)
        result = [x async for x in KosMap(v["inc"], v["gen-await"]())]
        self.assertEqual(result, [11, 21])

    async def test_nested_map_over_gen(self):
        mod = await self.load("kmgs_nested")
        v = vars(mod)
        inner = KosMap(v["inc"], v["gen-plain"]())
        result = [x async for x in KosMap(v["inc"], inner)]
        self.assertEqual(result, [3, 4, 5])

    async def test_nested_map_over_python_source(self):
        mod = await self.load("kmgs_nested_py")
        v = vars(mod)
        inner = KosMap(v["inc"], [1, 2])
        result = [x async for x in KosMap(v["inc"], inner)]
        self.assertEqual(result, [3, 4])

    async def test_demand_drive_over_async_source_denied(self):
        """
        a synchronous drive over a genuinely suspending python async
        source raises KoskariSyncViolation
        """
        import asyncio

        from koskari.lib import KoskariSyncViolation

        async def pysrc():
            await asyncio.sleep(0)
            yield 1
            yield 2

        mod = await self.load("kmgs_demand_py_async")
        v = vars(mod)
        m = KosMap(v["inc"], pysrc())
        with self.assertRaises(KoskariSyncViolation):
            list(m)

    async def test_demand_drive_over_awaiting_gen_denied(self):
        """
        a synchronous drive over a koskari generator that awaits raises
        KoskariSyncViolation
        """
        from koskari.lib import KoskariSyncViolation

        mod = await self.load("kmgs_demand_await")
        v = vars(mod)
        m = KosMap(v["inc"], v["gen-await"]())
        with self.assertRaises(KoskariSyncViolation):
            list(m)

    async def test_capture_across_map_helper_frame_refused(self):
        """
        a continuation captured while the map's helper frame is live (a
        driven source keeps it on the chain) is refused with an error
        naming the native program; a sync-source step installs only the
        function's own frame and captures like any call
        """
        mod = await self.load("kmgs_capture")
        v = vars(mod)
        m = KosMap(v["grab"], v["gen-plain"]())
        with self.assertRaises(RuntimeError) as ctx:
            await v["capture-attempt"](m)
        self.assertIn("native frame 'map-step'", str(ctx.exception))

    async def test_gen_source_exhaustion_is_clean(self):
        """
        after exhaustion the map reports closed on every drive
        """
        mod = await self.load("kmgs_exhaust")
        v = vars(mod)
        m = KosMap(v["inc"], v["gen-plain"]())
        self.assertEqual(list(m), [2, 3, 4])
        self.assertEqual(list(m), [])
        self.assertEqual([x async for x in m], [])


# The end.
