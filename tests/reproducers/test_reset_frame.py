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
tests.reproducers.test_reset_frame

A reset's body runs in a frame of its own over the enclosing function's
code and names, with the prompt under it: a return in it gives the
reset its value, a loop outside it is out of reach of its break and
continue, a yield written in it is refused, and a var in it is a name
of the enclosing function. A continuation holds whole frames up to and
including the body's, so a return in a resumed copy is that copy's
value.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import traceback
import unittest

import pytest

from koskari.exceptions import SpecialError
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def return_in_body []:
    var log #[]
    var r (reset (begin (log.append "in") (return 7) (log.append "unreached")))
    (log.append r)
    log

def return_in_copy []:
    (reset (+ 1 (shift k (+ 100 (k 5))) (return 99)))

def break_outer []:
    try:
        (while True (reset (break 5)))
    except [e RuntimeError]:
        (str e)

def continue_outer []:
    var seen #[]
    try:
        for-each [x #[1 2]]:
            (reset (continue x))
            (seen.append x)
    except [e RuntimeError]:
        (seen.append (str e))
    seen

def inner_loop []:
    (reset (while True (break 3)))

def generator_in_body []:
    reset:
        def g []:
            yield 1
            yield 2
        (list (g))

def var_is_enclosing []:
    var x 1
    reset:
        var x 2
        x
    x

def set_enclosing []:
    var x 1
    reset:
        (set! x 5)
    x

def locals_across_copies []:
    var acc #[]
    var n 1
    var r (reset (begin
                   (var v (shift k (+ (k 1) (k 2))))
                   (set! n (+ n v))
                   (acc.append #(v n))
                   (* v 10)))
    #[r n acc]

def tail_reset [x]:
    (reset (+ x (shift k (k (k 1)))))

def deep_reset [n]:
    (if (== n 0) 0 (+ 1 (reset (deep_reset (- n 1)))))

def gen_with_reset []:
    yield (reset (+ 1 (shift k (k 1))))
    yield (reset 5)

def gen_pair []:
    yield 1
    yield 2

def consume_in_reset []:
    reset:
        var out #[]
        for-each [x (gen_pair)]:
            (out.append x)
        out

def two_tags []:
    var a (object)
    var b (object)
    var r1 (reset-at a (+ 1 (reset-at b (+ 10 (shift-at a k (k 100))))))
    var r2 (reset-at b (+ 1 (reset-at a (+ 10 (shift-at b k (k 100))))))
    var r3 (reset-at a (+ 1 (reset-at b (+ 10 (shift-at a k 5)))))
    #[r1 r2 r3]

def raise_in_body []:
    (reset (+ 1 (raise (ValueError "in body"))))
"""


YIELD_SRC = """
def yield_in_body []:
    reset:
        yield 1
"""


class TestResetFrame(unittest.IsolatedAsyncioTestCase):

    async def load(self, src=SRC):
        module = new_module("reset_frame")
        vars(module)["__file__"] = "<reset_frame>"
        await load_module_source_async(module, src)
        return vars(module)


    async def test_return_in_body_is_the_reset_value(self):
        mod = await self.load()
        self.assertEqual(["in", 7], await mod["return_in_body"]())


    async def test_return_in_a_resumed_copy_is_its_value(self):
        mod = await self.load()
        self.assertEqual(199, await mod["return_in_copy"]())


    async def test_break_to_an_outer_loop_is_outside_loop(self):
        mod = await self.load()
        self.assertEqual("break outside loop", await mod["break_outer"]())


    async def test_continue_to_an_outer_loop_is_outside_loop(self):
        mod = await self.load()
        self.assertEqual(
            ["continue outside loop"], await mod["continue_outer"]())


    async def test_loop_inside_the_body(self):
        mod = await self.load()
        self.assertEqual(3, await mod["inner_loop"]())


    async def test_yield_in_body_is_refused(self):
        with self.assertRaises(SpecialError):
            await self.load(YIELD_SRC)


    async def test_generator_defined_in_body(self):
        mod = await self.load()
        self.assertEqual([1, 2], await mod["generator_in_body"]())


    async def test_var_in_body_is_the_enclosing_function_name(self):
        mod = await self.load()
        self.assertEqual(2, await mod["var_is_enclosing"]())


    async def test_set_of_an_enclosing_local(self):
        mod = await self.load()
        self.assertEqual(5, await mod["set_enclosing"]())


    async def test_enclosing_locals_across_copies(self):
        mod = await self.load()
        self.assertEqual(
            [30, 4, [(1, 2), (2, 4)]],
            await mod["locals_across_copies"]())


    async def test_reset_in_tail_position(self):
        mod = await self.load()
        self.assertEqual(7, await mod["tail_reset"](3))


    async def test_recursion_through_reset(self):
        mod = await self.load()
        try:
            result = await mod["deep_reset"](20000)
        except RecursionError:
            return
        self.assertEqual(20000, result)


    async def test_reset_inside_a_generator(self):
        mod = await self.load()
        self.assertEqual([2, 5], list(mod["gen_with_reset"]()))


    async def test_generator_consumed_inside_a_reset(self):
        mod = await self.load()
        self.assertEqual([1, 2], await mod["consume_in_reset"]())


    async def test_two_tags_nested_both_ways(self):
        mod = await self.load()
        self.assertEqual([111, 111, 5], await mod["two_tags"]())


    async def test_traceback_names_the_body_frame_for_its_function(self):
        mod = await self.load()
        # assertRaises detaches the traceback from the exception it
        # keeps, so the frames are read inside the except arm
        try:
            await mod["raise_in_body"]()
        except ValueError as exc:
            names = [f.f_code.co_name
                     for f, _ in traceback.walk_tb(exc.__traceback__)]
        else:
            self.fail("no ValueError")
        self.assertEqual(["raise_in_body", "raise_in_body"], names[-2:])


# The end.
