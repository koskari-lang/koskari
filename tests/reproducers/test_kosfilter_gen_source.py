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
tests.reproducers.test_kosfilter_gen_source

KosFilter with koskari predicates and koskari generator sources: the
predicate runs flattened in the driving evaluator and the generator
installs onto it. Covers the demand and asyncio drives, awaits inside
the predicate and the source, nested composition with KosMap, capture
refusal across the filter's frame program, and the namespace
retargeting regression where a rejected first element followed by a
refetch republished the popped predicate frame's namespace

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import KosFilter, KosMap
from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define gen-plain []:
    yield 1
    yield 2
    yield 3
    yield 4

define gen-await []:
    (await (sleep 0.001))
    yield 10
    (await (sleep 0.001))
    yield 3
    yield 20

define odd? [x]:
    (== 1 (% x 2))

define big? [x]:
    (await (sleep 0))
    (> x 5)

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


class TestKosFilterGenSource(unittest.IsolatedAsyncioTestCase):
    """
    KosFilter over koskari predicates and generator sources
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_kos_predicate_demand_drive(self):
        mod = await self.load("kfgs_demand")
        v = vars(mod)
        self.assertEqual(list(KosFilter(v["odd?"], range(10))),
                         [1, 3, 5, 7, 9])

    async def test_kos_predicate_async_drive(self):
        mod = await self.load("kfgs_async")
        v = vars(mod)
        result = [x async for x in KosFilter(v["odd?"], range(10))]
        self.assertEqual(result, [1, 3, 5, 7, 9])

    async def test_awaiting_kos_predicate(self):
        mod = await self.load("kfgs_await_pred")
        v = vars(mod)
        result = [x async for x in KosFilter(v["big?"], range(10))]
        self.assertEqual(result, [6, 7, 8, 9])

    async def test_gen_source_kos_predicate(self):
        mod = await self.load("kfgs_gen")
        v = vars(mod)
        result = [x async for x in KosFilter(v["odd?"], v["gen-plain"]())]
        self.assertEqual(result, [1, 3])

    async def test_awaiting_gen_source_none_predicate(self):
        mod = await self.load("kfgs_gen_await")
        v = vars(mod)
        result = [x async for x in KosFilter(None, v["gen-await"]())]
        self.assertEqual(result, [10, 3, 20])

    async def test_kos_consumer_over_filter(self):
        mod = await self.load("kfgs_consume")
        v = vars(mod)
        result = await v["consume"](KosFilter(v["odd?"], v["gen-plain"]()))
        self.assertEqual(result, 4)

    async def test_filter_over_map(self):
        mod = await self.load("kfgs_over_map")
        v = vars(mod)
        m = KosMap(v["odd?"], range(6))
        inner = KosMap(lambda x: x + 1, range(6))
        result = [x async for x in KosFilter(v["odd?"], inner)]
        self.assertEqual(result, [1, 3, 5])
        del m

    async def test_demand_drive_over_awaiting_gen_denied(self):
        """
        a synchronous drive over a koskari generator that awaits raises
        KoskariSyncViolation
        """
        from koskari.lib import KoskariSyncViolation

        mod = await self.load("kfgs_demand_await")
        v = vars(mod)
        f = KosFilter(v["odd?"], v["gen-await"]())
        with self.assertRaises(KoskariSyncViolation):
            list(f)

    async def test_capture_across_filter_frame_refused(self):
        """
        a continuation captured while the filter's frame program is
        live is refused with an error naming the native program
        """
        mod = await self.load("kfgs_capture")
        v = vars(mod)
        f = KosFilter(v["grab"], v["gen-plain"]())
        with self.assertRaises(RuntimeError) as ctx:
            await v["capture-attempt"](f)
        self.assertIn("native frame 'filter-step'", str(ctx.exception))

    async def test_rejected_first_element_then_reuse(self):
        """
        regression: a predicate whose first verdict is false forces a
        refetch right after the predicate frame pops; the namespace
        retarget must not republish the popped frame's namespace, or a
        later drive of the same function corrupts unrelated namespaces
        """
        mod = await self.load("kfgs_retarget")
        v = vars(mod)

        # first verdict false, second true: pop-then-refetch shape
        result = [x async for x in KosFilter(v["odd?"], [2, 3])]
        self.assertEqual(result, [3])

        # the same predicate over a generator source must be unharmed
        result = [x async for x in KosFilter(v["odd?"], v["gen-plain"]())]
        self.assertEqual(result, [1, 3])

        # and the module's other definitions still resolve and run
        self.assertEqual(await v["consume"]([5, 6]), 11)

    async def test_exhaustion_is_clean(self):
        mod = await self.load("kfgs_exhaust")
        v = vars(mod)
        f = KosFilter(v["odd?"], v["gen-plain"]())
        self.assertEqual(list(f), [1, 3])
        self.assertEqual(list(f), [])
        self.assertEqual([x async for x in f], [])


# The end.
