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
tests.reproducers.test_transfer_caught_raise

A transfer that runs a dyn-wind thunk or a dyn-with's __enter__ or
__exit__ on its way waits for it to return. A raise inside that call
which the call catches itself, in its own body or in a function it
calls, is the call's own business: the transfer goes on once it
returns, as if nothing had been raised. Only a raise that leaves the
call ends the transfer, at the point the raise is caught.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def raiser []:
    (raise (ValueError "callee"))

class ExitCatches []:
    def __init__ [self log]:
        (set! self.log log)
    def __enter__ [self]:
        (self.log.append "enter")
        self
    def __exit__ [self *: args]:
        (try (raise (ValueError "exit"))
             [[e ValueError] (self.log.append "exit")])
        False

class EnterCatches []:
    def __init__ [self log]:
        (set! self.log log)
    def __enter__ [self]:
        (try (raise (ValueError "enter"))
             [[e ValueError] (self.log.append "enter")])
        self
    def __exit__ [self *: args]:
        (self.log.append "exit")
        False

def after_catches []:
    var log #[]
    var r (reset
        (+ 1 (dyn-wind (log.append "before")
                       (shift k (begin (log.append "handler") 10))
                       (try (raise (ValueError "after"))
                            [[e ValueError] (log.append "after")]))))
    #[r log]

def before_catches []:
    var log #[]
    var n #[0]
    var r (reset
        (+ 1 (dyn-wind (begin
                         (set! n[0] (+ n[0] 1))
                         (try (raise (ValueError "before"))
                              [[e ValueError] (log.append #("before" n[0]))]))
                       (shift k (k 10))
                       (log.append "after"))))
    #[r log]

def exit_catches []:
    var log #[]
    var r (reset
        (+ 1 (dyn-with [c (ExitCatches log)]
               (shift k (begin (log.append "handler") 20)))))
    #[r log]

def enter_catches []:
    var log #[]
    var r (reset
        (+ 1 (dyn-with [c (EnterCatches log)]
               (shift k (k 5)))))
    #[r log]

def callee_raises []:
    var log #[]
    var r (reset
        (+ 1 (dyn-wind nil
                       (shift k (begin (log.append "handler") 30))
                       (try (raiser)
                            [[e ValueError] (log.append "caught")]))))
    #[r log]

def nested []:
    var log #[]
    var m #[0]
    var j (reset
        (dyn-wind (begin
                    (set! m[0] (+ m[0] 1))
                    (when (> m[0] 1)
                      (try (raise (ValueError "inner"))
                           [[e ValueError] (log.append "inner-caught")])))
                  (shift j j)
                  nil))
    var r (reset
        (+ 1 (dyn-wind nil
                       (shift k (begin (log.append "handler") 40))
                       (log.append (j 7)))))
    #[r log]

def after_escapes []:
    var log #[]
    var r:
        try:
            (reset
                (+ 1 (dyn-wind nil
                               (shift k (begin (log.append "handler") 50))
                               (raise (ValueError "escape")))))
        except [e ValueError]:
            "escaped"
    #[r log]
"""


class TestTransferCaughtRaise(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("transfer_caught_raise")
        vars(module)["__file__"] = "<transfer_caught_raise>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_after_thunk_catches_during_an_abort(self):
        mod = await self.load()
        self.assertEqual(
            [10, ["before", "after", "handler"]],
            await mod["after_catches"]())


    async def test_before_thunk_catches_during_reentry(self):
        mod = await self.load()
        self.assertEqual(
            [11, [("before", 1), "after", ("before", 2), "after"]],
            await mod["before_catches"]())


    async def test_exit_catches_during_an_abort(self):
        mod = await self.load()
        self.assertEqual(
            [20, ["enter", "exit", "handler"]],
            await mod["exit_catches"]())


    async def test_enter_catches_during_reentry(self):
        mod = await self.load()
        self.assertEqual(
            [6, ["enter", "exit", "enter", "exit"]],
            await mod["enter_catches"]())


    async def test_thunk_catches_its_callee_raise(self):
        mod = await self.load()
        self.assertEqual(
            [30, ["caught", "handler"]],
            await mod["callee_raises"]())


    async def test_nested_transfer_inside_a_waited_thunk(self):
        mod = await self.load()
        self.assertEqual(
            [40, ["inner-caught", 7, "handler"]],
            await mod["nested"]())


    async def test_raise_escaping_the_thunk_ends_the_transfer(self):
        mod = await self.load()
        self.assertEqual(
            ["escaped", []],
            await mod["after_escapes"]())


# The end.
