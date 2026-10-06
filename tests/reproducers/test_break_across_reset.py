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

A break or continue reaches the innermost loop of its own function
through any reset between them, whether it is written in the reset
body or in a shift handler, as a return or a raise does. Inside a
resumed delimited slice the slice is the continuation's body: a break
or continue there with no loop of its own is a break outside a loop,
not an exit from the loop surrounding the reset and not an exit from a
loop in the shift handler.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define break_in_body []:
    while True:
        reset:
            break 5

define continue_in_body []:
    define seen #[]
    for-each [x #[1 2 3]]:
        reset:
            (seen.append x)
            continue x
        (seen.append "unreached")
    seen

define break_in_handler []:
    while True:
        reset:
            (+ 1 (shift k (k (break 9))))

define continue_in_handler []:
    define seen #[]
    for-each [x #[1 2 3]]:
        reset:
            (+ 1 (shift k (begin (seen.append x) (continue x))))
        (seen.append "unreached")
    seen

define return_in_body []:
    while True:
        reset:
            return 7

define raise_in_body []:
    try:
        while True:
            reset:
                raise (ValueError "boo")
    except [ValueError as: e]:
        (str e)

define break_in_slice []:
    while True:
        reset:
            (+ 1 (shift k (k 1)) (break 11))

define break_in_slice_handler_loop []:
    reset:
        (+ 1 (shift k (while True (k 1))) (break 11))

define continue_in_slice []:
    for-each [x #[1 2 3]]:
        reset:
            (+ 1 (shift k (k 1)) (continue x))

define break_in_slice_own_loop []:
    reset:
        (+ 1 (shift k (k 1)) (while True (break 13)))
"""


class TestBreakAcrossReset(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("break_across_reset")
        vars(module)["__file__"] = "<break_across_reset>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_break_in_reset_body(self):
        mod = await self.load()
        self.assertEqual(5, await mod["break_in_body"]())


    async def test_continue_in_reset_body(self):
        mod = await self.load()
        self.assertEqual([1, 2, 3], await mod["continue_in_body"]())


    async def test_break_in_shift_handler(self):
        mod = await self.load()
        self.assertEqual(9, await mod["break_in_handler"]())


    async def test_continue_in_shift_handler(self):
        mod = await self.load()
        self.assertEqual([1, 2, 3], await mod["continue_in_handler"]())


    async def test_return_and_raise_still_cross(self):
        mod = await self.load()
        self.assertEqual(7, await mod["return_in_body"]())
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
