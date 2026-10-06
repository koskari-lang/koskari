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
tests.reproducers.test_demand_gen_await

The demand bit is drive-state on the generator's frame, assigned at
every entry: flattened iteration inherits the consumer's demand state,
a KosPyIter drive from Python forces it on, an async boundary drive
forces it off. An await under a demand drive therefore denies in-eval as
KoskariSyncViolation -- catchable by the generator body for sync
fallback, exactly as in plain demand functions; uncaught, it unwinds
the generator (finally runs) and propagates to the caller as the
violation, never dressed as StopIteration.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KoskariSyncViolation, demand_call
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import-from asyncio sleep

define gen-fallback []:
    try:
        (await (sleep 0.01))
        (yield "awaited")
    except [KoskariSyncViolation]:
        (yield "fell-back-sync")

define gen-bare []:
    (await (sleep 0.01))
    (yield "awaited")

define gen-two []:
    (yield "first")
    (await (sleep 0.01))
    (yield "awaited")

;; demand koskari for-eaching a plain async gen: flattened iteration
;; inherits the demand bit from the consumer's frame
define collect-fallback []:
    define out #[]
    (for-each [x (gen-fallback)]
        (out.append x))
    out

define collect-bare []:
    define out #[]
    (for-each [x (gen-bare)]
        (out.append x))
    out
"""


class TestDemandGenAwait(unittest.IsolatedAsyncioTestCase):
    """
    Await under a demand drive denies in-eval and is catchable; async
    drives are untouched
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_async_iteration_awaits(self):
        """
        plain koskari iteration of an async gen whose body awaits: the
        await suspends and completes normally (async-allowed row)
        """

        mod = await self.load("dga_async")
        result = await vars(mod)["collect-bare"]()
        self.assertEqual(result, ["awaited"])

    async def test_demand_call_mode1_fallback(self):
        """
        demand_call on the yielding fn returns its AsyncGenerator;
        iterating it from Python drives through a KosPyIter, and the
        body catches the violation and yields the sync fallback
        """

        mod = await self.load("dga_mode1")
        dgen = demand_call(vars(mod)["gen-fallback"])
        self.assertEqual([x for x in dgen], ["fell-back-sync"])

    async def test_wrapped_agen_fallback(self):
        """
        iter() of an existing async gen (a KosPyIter): same catchable
        fallback
        """

        mod = await self.load("dga_wrapped")
        agen = vars(mod)["gen-fallback"]()
        self.assertEqual([x for x in iter(agen)], ["fell-back-sync"])

    async def test_demand_koskari_flattened_fallback(self):
        """
        demand koskari for-eaching a plain async gen: the flattened
        install inherits the consumer's demand bit, and the generator
        body catches and falls back
        """

        mod = await self.load("dga_flatten")
        result = demand_call(vars(mod)["collect-fallback"])
        self.assertEqual(result, ["fell-back-sync"])

    async def test_uncaught_violation_raises_out(self):
        """
        no try/except in the body: the violation propagates to the
        caller as KoskariSyncViolation (never StopIteration-shaped),
        and the generator is closed afterward
        """

        mod = await self.load("dga_uncaught")
        dgen = demand_call(vars(mod)["gen-bare"])
        it = iter(dgen)
        with self.assertRaises(KoskariSyncViolation):
            next(it)
        with self.assertRaises(StopIteration):
            next(it)

    async def test_uncaught_flattened_raises_out(self):
        """
        demand koskari for-eaching a bare-await gen: the violation
        propagates out of the demand call
        """

        mod = await self.load("dga_uncaught_flat")
        with self.assertRaises(KoskariSyncViolation):
            demand_call(vars(mod)["collect-bare"])

    async def test_resumed_demand_step_denies(self):
        """
        the bit is forced per entry, not just on the first drive: a
        second demand step resuming past the first yield hits the await
        and the violation raises out
        """

        # Direct demand->async alternation on one agen (checking the
        # forced-off polarity) lives in
        # test_asyncgen_anext_await.py::test_demand_then_async_alternation.
        mod = await self.load("dga_resumed")
        agen = vars(mod)["gen-two"]()
        it = iter(agen)
        self.assertEqual(next(it), "first")
        with self.assertRaises(KoskariSyncViolation):
            next(it)


# The end.
