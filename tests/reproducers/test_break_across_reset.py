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
tests.reproducers.test_break_across_reset

A reset's body is a function the reset calls, so only a loop inside
the body is the body's: a break or continue there with no loop of its
own is a break outside a loop, whatever loop surrounds the reset, and a
return gives the reset its value while a raise crosses it. A
shift handler runs after the shift has aborted to its reset, so only a
loop inside the handler is the handler's: a break or continue there
with no loop of its own is a break outside a loop, whatever loop
surrounds the reset. Inside a resumed delimited slice the slice is the
continuation's body: a break or continue there with no loop of its own
is a break outside a loop too, not an exit from the loop surrounding
the reset and not an exit from a loop in the shift handler.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def break_in_body []:
    while True:
        reset:
            break 5

def continue_in_body []:
    var seen #[]
    for-each [x #[1 2 3]]:
        reset:
            (seen.append x)
            continue x
        (seen.append "unreached")
    seen

def break_in_handler []:
    while True:
        reset:
            (+ 1 (shift k (k (break 9))))

def continue_in_handler []:
    var seen #[]
    for-each [x #[1 2 3]]:
        reset:
            (+ 1 (shift k (begin (seen.append x) (continue x))))
        (seen.append "unreached")
    seen

def return_in_body []:
    var seen #[]
    while True:
        (seen.append (reset (return 7)))
        break
    seen

def raise_in_body []:
    try:
        while True:
            reset:
                raise (ValueError "boo")
    except [ValueError as: e]:
        (str e)

def break_in_slice []:
    while True:
        reset:
            (+ 1 (shift k (k 1)) (break 11))

def break_in_slice_handler_loop []:
    reset:
        (+ 1 (shift k (while True (k 1))) (break 11))

def continue_in_slice []:
    for-each [x #[1 2 3]]:
        reset:
            (+ 1 (shift k (k 1)) (continue x))

def break_in_slice_own_loop []:
    reset:
        (+ 1 (shift k (k 1)) (while True (break 13)))
"""


class TestBreakAcrossReset(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("break_across_reset")
        vars(module)["__file__"] = "<break_across_reset>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_break_in_reset_body_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["break_in_body"]()
        self.assertEqual("break outside loop", str(caught.exception))


    async def test_continue_in_reset_body_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["continue_in_body"]()
        self.assertEqual("continue outside loop", str(caught.exception))


    async def test_break_in_shift_handler_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["break_in_handler"]()
        self.assertEqual("break outside loop", str(caught.exception))


    async def test_continue_in_shift_handler_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["continue_in_handler"]()
        self.assertEqual("continue outside loop", str(caught.exception))


    async def test_return_is_the_reset_value_and_raise_crosses(self):
        mod = await self.load()
        self.assertEqual([7], await mod["return_in_body"]())
        self.assertEqual("boo", await mod["raise_in_body"]())


    async def test_break_in_resumed_slice_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["break_in_slice"]()
        self.assertEqual("break outside loop", str(caught.exception))


    async def test_break_in_slice_ignores_handler_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["break_in_slice_handler_loop"]()
        self.assertEqual("break outside loop", str(caught.exception))


    async def test_continue_in_resumed_slice_is_outside_loop(self):
        mod = await self.load()
        with self.assertRaises(RuntimeError) as caught:
            await mod["continue_in_slice"]()
        self.assertEqual("continue outside loop", str(caught.exception))


    async def test_break_in_slice_reaches_its_own_loop(self):
        mod = await self.load()
        self.assertEqual(15, await mod["break_in_slice_own_loop"]())


# The end.
