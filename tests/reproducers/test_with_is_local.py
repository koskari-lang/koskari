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
tests.reproducers.test_with_is_local

A plain with is local to its body: __exit__ runs when the body
completes, raises, or is left by a return, break or continue, and a
continuation transfer runs neither __exit__ nor __enter__. A call/cc
escape or a shift that aborts the region leaves the manager entered,
and each copy of the body a continuation resumes runs __exit__ when
that copy exits. A dyn-with is the form a transfer runs.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
class Mgr []:
    def __init__ [self log]:
        (set! self.log log)
        (set! self.open False)
        (set! self.seen None)
    def __enter__ [self]:
        (self.log.append "enter")
        (set! self.open True)
        self
    def __exit__ [self *: args]:
        (self.log.append "exit")
        (set! self.open False)
        (set! self.seen args[0])
        False

def escape [log]:
    var r (call/cc (lambda [k] (with [c (Mgr log)] (k "escaped"))))
    #[r log]

def handler_drops_k [log]:
    var r (reset
        (+ 1 (with [c (Mgr log)]
               (shift k (begin (log.append "handler") 0)))))
    #[r log]

def handler_resumes [log]:
    var r (reset
        (+ 1 (with [c (Mgr log)]
               (shift k (begin (log.append "handler") (k 1))))))
    #[r log]

def handler_resumes_twice [log]:
    var r (reset
        (+ 1 (with [c (Mgr log)]
               (shift k (begin (log.append "handler") (+ (k 1) (k 2)))))))
    #[r log]

def dyn_with_resumes [log]:
    var r (reset
        (+ 1 (dyn-with [c (Mgr log)]
               (shift k (begin (log.append "handler") (k 1))))))
    #[r log]

def body_uses_manager [log]:
    var m (Mgr log)
    var r (reset
        (with [c m]
          (shift k (k 1))
          c.open))
    #[r log]

def copy_raises [log]:
    var m (Mgr log)
    try:
        reset:
            with [c m]:
                (shift k (k 1))
                (raise (ValueError "boom"))
    except [e ValueError]:
        (log.append "caught")
    #[(is m.seen ValueError) log]

def returns_from_copy [log m]:
    with [c m]:
        (shift k (k 1))
        (log.append "body")
        return "returned"
    "fell-through"

def copy_returns [log]:
    var m (Mgr log)
    var r (reset (returns_from_copy log m))
    #[r log]
"""


class TestWithIsLocal(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("with_is_local")
        vars(module)["__file__"] = "<with_is_local>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_call_cc_escape_does_not_exit(self):
        mod = await self.load()
        self.assertEqual(["escaped", ["enter"]], await mod["escape"]([]))


    async def test_shift_abort_does_not_exit(self):
        mod = await self.load()
        self.assertEqual(
            [0, ["enter", "handler"]], await mod["handler_drops_k"]([]))


    async def test_resumed_copy_exits_on_completion(self):
        mod = await self.load()
        self.assertEqual(
            [2, ["enter", "handler", "exit"]],
            await mod["handler_resumes"]([]))


    async def test_each_resumed_copy_exits(self):
        mod = await self.load()
        self.assertEqual(
            [5, ["enter", "handler", "exit", "exit"]],
            await mod["handler_resumes_twice"]([]))


    async def test_dyn_with_is_run_by_the_transfer(self):
        mod = await self.load()
        self.assertEqual(
            [2, ["enter", "exit", "handler", "enter", "exit"]],
            await mod["dyn_with_resumes"]([]))


    async def test_resumed_body_sees_the_manager_entered(self):
        mod = await self.load()
        self.assertEqual(
            [True, ["enter", "exit"]], await mod["body_uses_manager"]([]))


    async def test_raise_in_resumed_copy_exits_with_it(self):
        mod = await self.load()
        self.assertEqual(
            [True, ["enter", "exit", "caught"]],
            await mod["copy_raises"]([]))


    async def test_return_from_resumed_copy_exits(self):
        mod = await self.load()
        self.assertEqual(
            ["returned", ["enter", "body", "exit"]],
            await mod["copy_returns"]([]))


# The end.
