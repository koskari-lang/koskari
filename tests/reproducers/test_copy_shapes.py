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
tests.reproducers.test_copy_shapes

Each application of a continuation attaches a fresh copy of what it
captured. A long copy runs as often as it is applied, reads its frames'
locals after resuming, and a raise inside it reaches the call that
applied it. Every dyn-wind in a copy is entered again, outermost first,
with its thunks reading the namespace of the frame they were written
in, whether or not frames lie between the winds. A full continuation
re-enters the winds above the extent it shares with the call site. A
resumed copy's frames stand where the call site puts them, so deep
recursion inside one stops at the frame limit as fresh code does.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def deep [n]:
    var mine (* n 10)
    try:
        var r (if (== n 0) (shift k k) (deep (- n 1)))
        (+ r mine)
    except [e KeyError]:
        "never"

def copy_long []:
    var k (reset (deep 40))
    var a (k 1)
    var b (k 2)
    var c:
        try:
            (k "x")
        except [e TypeError]:
            "raised at the call"
    #[a b c]

def winds_inline []:
    var log #[]
    var a 1
    var k:
        reset:
            (dyn-wind (log.append #("in" 1 a))
                (dyn-wind (log.append #("in" 2 a))
                    (dyn-wind (log.append #("in" 3 a))
                        (shift k k)
                        (log.append #("out" 3 a)))
                    (log.append #("out" 2 a)))
                (log.append #("out" 1 a)))
    (set! a 2)
    (log.clear)
    var r (k 5)
    #[r log]

def wound [log depth a]:
    (if (== depth 0)
        (shift k k)
        (dyn-wind (log.append #("in" depth a))
                  (wound log (- depth 1) a)
                  (log.append #("out" depth a))))

def winds_framed []:
    var log #[]
    var k (reset (wound log 3 "x"))
    (log.clear)
    var r (k 7)
    #[r log]

def cc_winds []:
    var log #[]
    var saved #[None]
    var count 0
    var v:
        (dyn-wind (log.append "in1")
            (dyn-wind (log.append "in2")
                (call/cc (lambda [k] (set! saved[0] k) 0))
                (log.append "out2"))
            (log.append "out1"))
    (log.append v)
    (set! count (+ count 1))
    (when (< count 3)
        (saved[0] count))
    #[count log]

def down [box n]:
    (set! box[0] n)
    (+ 1 (down box (+ n 1)))

def recursion_shapes []:
    var fresh #[0]
    var copy #[0]
    var fresh_out:
        try:
            (down fresh 0)
        except [e RecursionError]:
            "recursion"
    var k (reset (+ (shift k k) (down copy 0)))
    var copy_out:
        try:
            (k 1)
        except [e RecursionError]:
            "recursion"
    #[fresh_out fresh[0] copy_out copy[0]]
"""


class TestCopyShapes(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("copy_shapes")
        vars(module)["__file__"] = "<copy_shapes>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_long_copy_applied_twice_and_raising(self):
        mod = await self.load()
        self.assertEqual(
            [8201, 8202, "raised at the call"],
            await mod["copy_long"]())


    async def test_winds_with_no_frame_between(self):
        mod = await self.load()
        self.assertEqual(
            [5, [("in", 1, 2), ("in", 2, 2), ("in", 3, 2),
                 ("out", 3, 2), ("out", 2, 2), ("out", 1, 2)]],
            await mod["winds_inline"]())


    async def test_winds_with_a_frame_between_each(self):
        mod = await self.load()
        self.assertEqual(
            [7, [("in", 3, "x"), ("in", 2, "x"), ("in", 1, "x"),
                 ("out", 1, "x"), ("out", 2, "x"), ("out", 3, "x")]],
            await mod["winds_framed"]())


    async def test_call_cc_reenters_winds_above_the_seam(self):
        mod = await self.load()
        once = ["in1", "in2", "out2", "out1"]
        self.assertEqual(
            [3, once + [0] + once + [1] + once + [2]],
            await mod["cc_winds"]())


    async def test_recursion_inside_a_resumed_copy(self):
        mod = await self.load()
        try:
            fresh_out, fresh_n, copy_out, copy_n = \
                await mod["recursion_shapes"]()
        except RecursionError:
            return
        self.assertEqual("recursion", fresh_out)
        self.assertEqual("recursion", copy_out)
        # the copy's frame sits one above the call site, so its
        # recursion starts one frame deeper than the fresh one
        self.assertEqual(fresh_n - 1, copy_n)


# The end.
