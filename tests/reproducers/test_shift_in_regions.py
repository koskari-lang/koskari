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
tests.reproducers.test_shift_in_regions

A continuation captured from inside a region that holds state of its
own resumes with that state: an except body still has its exception
bound and the exception it displaced comes back when the shift leaves
it, and a for-each whose step made the shift takes the value the
continuation is called with as that step's element and goes on with
the iteration.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import sys

def in_handler []:
    reset:
        try:
            (raise (ValueError "inner"))
        except [e ValueError]:
            var v (shift k (k 5))
            #[v (str e)]

def in_handler_twice []:
    reset:
        try:
            (raise (ValueError "inner"))
        except [e ValueError]:
            var v (shift k (+ (k 1) (k 2)))
            (+ v (len (str e)))

def displaced []:
    try:
        (raise (KeyError "outer"))
    except [o KeyError]:
        var r:
            reset:
                try:
                    (raise (ValueError "inner"))
                except [e ValueError]:
                    (shift k "aborted")
        var info (sys.exc_info)
        #[r (repr info[1])]

def stored_from_handler []:
    var saved #[None]
    var r:
        reset:
            try:
                (raise (ValueError "inner"))
            except [e ValueError]:
                var v (shift k (begin (set! saved[0] k) 0))
                #[v (str e)]
    #[r (saved[0] 7) (saved[0] 8)]

def stepper [x]:
    (shift k (k (* x 10)))

def loop_over_map [items]:
    reset:
        var out #[]
        for-each [v (map stepper items)]:
            (out.append v)
        out

def twice [x]:
    (shift k #[(k x) (k (* x 100))])

def loop_twice [items]:
    reset:
        var last None
        for-each [v (map twice items)]:
            (setq last v)
        last

def hand_back [x]:
    (shift k #(x k))

def stepwise [items]:
    var seen #[]
    var step:
        reset:
            for-each [v (map hand_back items)]:
                (seen.append v)
            None
    while (not (is step None)):
        (setq step (step[1] (+ step[0] 1)))
    seen
"""


class TestShiftInRegions(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("shift_in_regions")
        vars(module)["__file__"] = "<shift_in_regions>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_except_body_resumes_with_its_exception(self):
        mod = await self.load()
        self.assertEqual([5, "inner"], await mod["in_handler"]())


    async def test_except_body_resumed_twice(self):
        mod = await self.load()
        self.assertEqual(13, await mod["in_handler_twice"]())


    async def test_shift_out_of_an_except_body_restores_the_outer_one(self):
        mod = await self.load()
        self.assertEqual(
            ["aborted", "KeyError('outer')"], await mod["displaced"]())


    async def test_except_body_resumed_after_the_reset(self):
        mod = await self.load()
        self.assertEqual(
            [0, [7, "inner"], [8, "inner"]],
            await mod["stored_from_handler"]())


    async def test_for_each_step_takes_the_resumed_value(self):
        mod = await self.load()
        self.assertEqual([10, 20, 30], await mod["loop_over_map"]([1, 2, 3]))


    async def test_for_each_step_resumed_twice(self):
        mod = await self.load()
        self.assertEqual([1, 100], await mod["loop_twice"]([1]))


    async def test_for_each_goes_on_after_each_later_resume(self):
        mod = await self.load()
        self.assertEqual([2, 3, 4], await mod["stepwise"]([1, 2, 3]))


# The end.
