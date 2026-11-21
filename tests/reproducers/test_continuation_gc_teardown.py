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
Reproducer: cyclic GC of abandoned delimited continuations must not
corrupt the dynchain node freelist.

The capture unlinks a continuation's template from the live chain: the
template's bottom node has no parent. Collecting abandoned reset/shift
cycles frees only the template's own nodes, never ones the evaluator
already released, so generators run afterwards find a sound freelist
and their yield walks terminate.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest

import pytest

from koskari.lib import demand_call
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SHIFT_SRC = """
(var leak-k None)

(def capture-and-abandon [x]
  (global var leak-k None)
  (reset
    (+ x (shift k (begin (setq leak-k k) 100)))))

(capture-and-abandon 1)
(capture-and-abandon 2)
(capture-and-abandon 3)
"""

GEN_SRC = """
(def pairs-via-yield [d]
  (for-each [pair (.items d)]
    (yield pair)))
"""


class TestContinuationGCTeardown(unittest.IsolatedAsyncioTestCase):

    async def load(self, name, source):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, source)
        return module

    async def test_collected_continuations_then_generators(self):
        """
        abandon captured continuations in collectable cycles, force
        collection, then drive generators hard: the yield walk must
        terminate
        """

        shift_mod = await self.load("shiftgc", SHIFT_SRC)
        ns = vars(shift_mod)
        self.assertIsNotNone(ns["leak-k"])

        # abandon the module and its captured continuations, then
        # collect the cycles; the freed templates must not touch nodes
        # the evaluator already released
        del shift_mod, ns
        gc.collect()

        gen_mod = await self.load("genafter", GEN_SRC)
        pairs_via_yield = vars(gen_mod)["pairs-via-yield"]

        for i in range(8):
            data = {f"k{j}": j for j in range(i + 1)}
            agen = demand_call(pairs_via_yield, data)
            result = list(agen)
            self.assertEqual(sorted(result), sorted(data.items()))
            gc.collect()


# The end.
