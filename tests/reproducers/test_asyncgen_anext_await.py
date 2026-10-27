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
tests.reproducers.test_asyncgen_anext_await

Regression tests for the anext await-suspension underrun: a
Python-side __anext__ drive of a koskari async generator must handle a
step that suspends on an await. Previously any anext step containing
an awaitfailed with "stack pop underrun" because the resume path re-entered the
evaluator as if no await were pending; flattened and demand consumption
were unaffected.

The fix records the await on the asend (the generator stays in-use
across the suspension) and resumes by driving the in-flight awaitable,
delivering its result to the pending AWAIT. See
design/archive/asyncgen-anext-await-underrun.md for the drive-path
anatomy.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

def g-await-first []:
    (await (sleep 0.001))
    (yield "one")

def g-yield-then-await []:
    (yield "one")
    (await (sleep 0.001))
    (yield "two")
"""


class TestAsyncGenAnextAwait(unittest.IsolatedAsyncioTestCase):
    """
    __anext__ drives a step containing an await: the awaitable is
    suspended to the loop and the following yield arrives
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_anext_step_with_await(self):
        """
        the very first anext, whose step awaits before yielding
        """

        mod = await self.load("agaw_first")
        agen = vars(mod)["g-await-first"]()
        self.assertEqual(await agen.__anext__(), "one")

    async def test_anext_resumed_step_with_await(self):
        """
        a resumed step (past the first yield) that awaits before its
        yield
        """

        mod = await self.load("agaw_resumed")
        agen = vars(mod)["g-yield-then-await"]()
        self.assertEqual(await agen.__anext__(), "one")
        self.assertEqual(await agen.__anext__(), "two")

    async def test_async_for_over_awaiting_gen(self):
        """
        the consumer-facing shape: async-for collects all values from a
        generator that awaits between yields
        """

        mod = await self.load("agaw_asyncfor")
        agen = vars(mod)["g-yield-then-await"]()
        collected = [v async for v in agen]
        self.assertEqual(collected, ["one", "two"])

    async def test_demand_then_async_alternation(self):
        """
        a KosPyIter step, then anext on the same agen: the async
        drive's step awaits and completes -- the demand bit was forced
        off at the async install (the alternation test previously
        blocked by this defect)
        """

        mod = await self.load("agaw_alternation")
        agen = vars(mod)["g-yield-then-await"]()
        self.assertEqual(next(iter(agen)), "one")
        self.assertEqual(await agen.__anext__(), "two")

    async def test_mid_await_reentry_guarded(self):
        """
        while a step is suspended on its await, the generator is in use:
        a second anext is refused instead of corrupting the suspended
        step
        """

        mod = await self.load("agaw_guard")
        agen = vars(mod)["g-yield-then-await"]()
        self.assertEqual(await agen.__anext__(), "one")

        # suspend the second step on its await
        task = asyncio.ensure_future(agen.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await agen.__anext__()

        # the suspended step is undisturbed and completes
        self.assertEqual(await task, "two")


# The end.
