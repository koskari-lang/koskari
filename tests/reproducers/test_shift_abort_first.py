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
tests.reproducers.test_shift_abort_first

A shift aborts to its prompt before its handler runs, so the handler
stands where the region stood and outside the region's extents: a try
or a parameterize inside the region does not reach it. A handler yields
in its own generator, and loops of its own break and continue. A
handler written in a called function has no caller left to return to.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import KoskariPromptUnreachable
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(define-parameter depth 0)

def yield_in_handler []:
    reset:
        + 100:
            shift k:
                yield (k 1)
                yield (k 2)

def run_yield_in_handler []:
    (list (yield_in_handler))

def yield_in_handler_generator []:
    reset:
        + 100:
            shift k:
                def bar []:
                    yield (k 1)
                    yield (k 2)
                list (bar)

def break_in_handler_loop []:
    reset:
        (+ 1 (shift k (while True (break 7))))

def continue_in_handler_loop []:
    var seen #[]
    reset:
        shift k:
            for-each [x #[1 2 3]]:
                (seen.append x)
                continue
                (seen.append "unreached")
            seen

def callee_handler []:
    (+ 1 (shift k (return 5)))

def return_in_callee_handler []:
    reset:
        (callee_handler)

def return_in_callee_handler_caught []:
    reset:
        (callee_handler_catches)

def callee_handler_catches []:
    shift k:
        try:
            return 5
        except [e RuntimeError]:
            "caught in the handler"

def raise_in_handler []:
    reset:
        try:
            (shift k (raise (ValueError "boom")))
        except [e ValueError]:
            "caught inside"

def param_in_handler []:
    reset:
        (parameterize [[depth 5]]
            (shift k depth))

def region_return []:
    reset:
        (+ 1 (shift k (k 5)) (return 99))

def region_return_caught []:
    reset:
        (+ 1 (shift k (k 5))
             (try (return 99) ((e RuntimeError) 100)))

def add_one [v]:
    return (+ v 1)

def region_callee_return []:
    reset:
        (add_one (shift k (k 5)))

def region_yield_own_handler []:
    reset:
        (+ (shift k (k 1)) (yield 5))

def run_region_yield_own_handler []:
    try:
        (list (region_yield_own_handler))
    except [e KoskariPromptUnreachable]:
        #[e.operation e.boundary]

def region_yield_escaping [out]:
    (out.append (reset (+ (shift k k) (yield 5))))

def other_generator [k]:
    yield (k 1)

def run_region_yield_other_generator []:
    var out #[]
    (list (region_yield_escaping out))
    var k (out.pop)
    try:
        (list (other_generator k))
    except [e KoskariPromptUnreachable]:
        #[e.operation e.boundary]
"""


class TestShiftAbortFirst(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("shift_abort_first")
        vars(module)["__file__"] = "<shift_abort_first>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_handler_yields_in_its_own_generator(self):
        mod = await self.load()
        self.assertEqual([101, 102], await mod["run_yield_in_handler"]())


    async def test_generator_defined_in_handler_applies_k(self):
        mod = await self.load()
        self.assertEqual(
            [101, 102], await mod["yield_in_handler_generator"]())


    async def test_break_reaches_a_loop_inside_the_handler(self):
        mod = await self.load()
        self.assertEqual(7, await mod["break_in_handler_loop"]())


    async def test_continue_reaches_a_loop_inside_the_handler(self):
        mod = await self.load()
        self.assertEqual(
            [1, 2, 3], await mod["continue_in_handler_loop"]())


    async def test_return_in_a_callee_handler_is_refused(self):
        mod = await self.load()
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await mod["return_in_callee_handler"]()
        self.assertEqual("return", caught.exception.operation)
        self.assertEqual("shift", caught.exception.boundary)
        self.assertIsInstance(caught.exception, RuntimeError)


    async def test_refused_return_is_raised_at_the_return(self):
        mod = await self.load()
        self.assertEqual(
            "caught in the handler",
            await mod["return_in_callee_handler_caught"]())


    async def test_try_inside_the_region_does_not_catch_the_handler(self):
        mod = await self.load()
        with self.assertRaises(ValueError):
            await mod["raise_in_handler"]()


    async def test_param_inside_the_region_is_not_the_handlers(self):
        mod = await self.load()
        self.assertEqual(0, await mod["param_in_handler"]())


    async def test_return_in_a_resumed_region_is_refused(self):
        mod = await self.load()
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await mod["region_return"]()
        self.assertEqual("return", caught.exception.operation)
        self.assertEqual("continuation", caught.exception.boundary)


    async def test_refused_region_return_is_raised_at_the_return(self):
        mod = await self.load()
        self.assertEqual(106, await mod["region_return_caught"]())


    async def test_return_in_a_function_called_by_the_region(self):
        mod = await self.load()
        self.assertEqual(6, await mod["region_callee_return"]())


    async def test_region_yield_applied_in_its_own_handler(self):
        mod = await self.load()
        self.assertEqual(
            ["yield", "continuation"],
            await mod["run_region_yield_own_handler"]())


    async def test_region_yield_applied_in_another_generator(self):
        mod = await self.load()
        self.assertEqual(
            ["yield", "continuation"],
            await mod["run_region_yield_other_generator"]())


# The end.
