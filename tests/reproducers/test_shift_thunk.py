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
tests.reproducers.test_shift_thunk

A shift's handler is a function of its continuation, closed over the
scope the shift was written in. The shift aborts to its prompt, then
calls the handler with the continuation while the prompt is still in
place, and the handler's value is the reset's. A return in the handler
returns that value; a shift in the handler finds the same prompt. The
continuation is the handler's parameter and nothing outside it binds
the name.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import SpecialError
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def callee_local []:
    var n 11
    (shift k (+ n 1))

def uses_callee_local []:
    reset:
        (+ 100 (callee_local))

def callee_returns []:
    (+ 1 (shift k (return 5)))

def return_in_callee []:
    reset:
        (+ 100 (callee_returns))

def return_in_own_function []:
    var r (reset (+ 1 (shift k (return 5))))
    (* 10 r)

def nested_shift []:
    (reset (+ 1 (shift k1 (* 2 (shift k2 5)))))

def nested_shift_applied []:
    (reset (+ 1 (shift k1 (* 2 (shift k2 (k2 5))))))

def nested_shift_at []:
    var t (object)
    (reset-at t (+ 1 (shift-at t k1 (* 2 (shift-at t k2 5)))))

def pending_outside []:
    (* 3 (reset (+ 1 (shift k 4))))

def shift_in_copy []:
    (reset (+ (shift k (k 1)) (shift j 10)))

def shift_in_copy_applied []:
    (reset (+ (shift k (k 1)) (shift j (j 20))))

def raise_caught_outside []:
    try:
        (reset (+ 1 (shift k (raise (ValueError "x")))))
    except [e ValueError]:
        "outside"

def raise_passes_region_try []:
    try:
        reset:
            try:
                (+ 1 (shift k (raise (ValueError "x"))))
            except [e ValueError]:
                "inside"
    except [e ValueError]:
        "outside"

def stored_k []:
    var saved #[None]
    var r (reset (+ 1 (shift k (begin (set! saved[0] k) 0))))
    #[r (saved[0] 5)]

def multi_shot []:
    (reset (* 2 (shift k (+ (k 1) (k 2)))))

def k_after_the_form []:
    (reset (shift k 1))
    k

def var_in_handler []:
    var x 1
    reset:
        shift k:
            var x 2
            x
    x

def break_outer_loop []:
    try:
        (while True (reset (shift k (break 9))))
    except [e RuntimeError]:
        (str e)

def wind_after_raises []:
    var log #[]
    var r (reset
        (+ 1 (try (dyn-wind (log.append "before")
                            (shift k (begin (log.append "handler") 0))
                            (begin (log.append "after")
                                   (raise (ValueError "x"))))
                  [[e ValueError] (begin (log.append "caught") 5)])))
    (log.append r)
    log
"""


YIELD_SRC = """
def yield_in_handler []:
    reset:
        + 100:
            shift k:
                yield (k 1)
"""


class TestShiftThunk(unittest.IsolatedAsyncioTestCase):

    async def load(self, src=SRC):
        module = new_module("shift_thunk")
        vars(module)["__file__"] = "<shift_thunk>"
        await load_module_source_async(module, src)
        return vars(module)


    async def test_handler_reads_the_callee_local(self):
        mod = await self.load()
        self.assertEqual(12, await mod["uses_callee_local"]())


    async def test_return_in_a_callee_handler(self):
        mod = await self.load()
        self.assertEqual(5, await mod["return_in_callee"]())


    async def test_return_in_a_handler_is_the_reset_value(self):
        mod = await self.load()
        self.assertEqual(50, await mod["return_in_own_function"]())


    async def test_yield_in_a_handler_is_refused(self):
        with self.assertRaises(SpecialError):
            await self.load(YIELD_SRC)


    async def test_shift_in_a_handler_finds_the_prompt(self):
        mod = await self.load()
        self.assertEqual(5, await mod["nested_shift"]())


    async def test_shift_in_a_handler_applies_its_k(self):
        mod = await self.load()
        self.assertEqual(10, await mod["nested_shift_applied"]())


    async def test_shift_at_in_a_handler_finds_the_prompt(self):
        mod = await self.load()
        self.assertEqual(5, await mod["nested_shift_at"]())


    async def test_operands_outside_the_reset(self):
        mod = await self.load()
        self.assertEqual(12, await mod["pending_outside"]())


    async def test_shift_in_a_resumed_copy(self):
        mod = await self.load()
        self.assertEqual(10, await mod["shift_in_copy"]())


    async def test_shift_in_a_resumed_copy_applies_its_k(self):
        mod = await self.load()
        self.assertEqual(21, await mod["shift_in_copy_applied"]())


    async def test_handler_raise_caught_outside(self):
        mod = await self.load()
        self.assertEqual("outside", await mod["raise_caught_outside"]())


    async def test_handler_raise_passes_a_region_try(self):
        mod = await self.load()
        self.assertEqual("outside", await mod["raise_passes_region_try"]())


    async def test_stored_k_applied_after_the_reset(self):
        mod = await self.load()
        self.assertEqual([0, 6], await mod["stored_k"]())


    async def test_multi_shot(self):
        mod = await self.load()
        self.assertEqual(6, await mod["multi_shot"]())


    async def test_k_is_not_bound_after_the_form(self):
        mod = await self.load()
        with self.assertRaises(NameError):
            await mod["k_after_the_form"]()


    async def test_var_in_a_handler_is_local_to_it(self):
        mod = await self.load()
        self.assertEqual(1, await mod["var_in_handler"]())


    async def test_break_does_not_reach_a_loop_around_the_reset(self):
        mod = await self.load()
        self.assertEqual("break outside loop", await mod["break_outer_loop"]())


    async def test_wind_after_raise_cancels_the_handler(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "after", "caught", 6],
            await mod["wind_after_raises"]())


# The end.
