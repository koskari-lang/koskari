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
tests.lib.test_demand_continuation_token

A continuation captured in one call of a demand wrapper is dead in the
wrapper's next call: applying it from inside the next call, or from
Python during it, raises KoskariContinuationDead.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define box #[None])

(define [capture x]
  (call/cc (lambda [k] (set! (item box 0) k) x)))

(define [apply_saved x]
  (call (item box 0) x))

(define [apply_via f x] (f x))
"""


class TestDemandContinuationToken(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("demand_continuation_token")
        vars(module)["__file__"] = "<demand_continuation_token>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_continuation_from_a_finished_call_is_dead_in_the_next(self):
        mod = await self.load()
        capture = lib.demand(mod["capture"])
        apply_saved = lib.demand(mod["apply_saved"])
        self.assertEqual(capture(1), 1)
        self.assertIsNotNone(mod["box"][0])
        with self.assertRaises(lib.KoskariContinuationDead):
            apply_saved(2)
        # the same wrapper that captured, called again, is a new run too
        with self.assertRaises(lib.KoskariContinuationDead):
            lib.demand(mod["apply_via"])(mod["box"][0], 3)
        self.assertEqual(capture(4), 4)

    async def test_continuation_applied_from_python_during_the_next_call(self):
        mod = await self.load()
        capture = lib.demand(mod["capture"])
        self.assertEqual(capture(1), 1)
        k = mod["box"][0]
        seen = []

        def poke(x):
            try:
                k(x)
            except lib.KoskariContinuationDead:
                seen.append("dead")
            return x

        apply_via = lib.demand(mod["apply_via"])
        self.assertEqual(apply_via(poke, 5), 5)
        self.assertEqual(seen, ["dead"])


# The end.
