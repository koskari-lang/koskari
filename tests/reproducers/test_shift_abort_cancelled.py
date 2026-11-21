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
tests.reproducers.test_shift_abort_cancelled

A shift leaves its region before its handler runs. A dyn-wind after
body on the way out runs during that leave, and when it raises, the
abort is cancelled and the region carries on where the raise is
caught, with the operands it had pending around the shift still in
place. A finally body is local to its try and does not run when a
shift aborts the region, so a raise, break, continue or return written
in it cancels nothing: the reset's value is the handler's.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def raise_pending_one []:
    reset:
        (+ 1 (try (try (shift k 0) [finally: (raise (ValueError "x"))])
                  ((e ValueError) 5)))

def raise_pending_none []:
    reset:
        (try (try (shift k 0) [finally: (raise (ValueError "x"))])
             ((e ValueError) 5))

def raise_pending_two []:
    reset:
        (+ 10 (+ 20 (try (try (shift k 0)
                              [finally: (raise (ValueError "x"))])
                         ((e ValueError) 5))))

def callee_raises []:
    (try (shift k 0) [finally: (raise (ValueError "x"))])

def raise_in_callee []:
    reset:
        (+ 1 (try (callee_raises) ((e ValueError) 5)))

def break_pending_one []:
    reset:
        (+ 1 (while True (try (shift k 0) [finally: (break 5)])))

def continue_pending_one []:
    var n 0
    reset:
        (+ 1 (begin
               (while (< n 3)
                 (set! n (+ n 1))
                 (try (shift k 0) [finally: (continue)]))
               n))

def callee_returns []:
    (try (shift k 0) [finally: (return 5)])

def return_in_callee []:
    reset:
        (+ 1 (callee_returns))

def wind_after_raises []:
    var log #[]
    var r (reset
        (+ 1 (try (dyn-wind (log.append "before")
                            (shift k (begin (log.append "handler") 0))
                            (begin (log.append "after")
                                   (raise (ValueError "x"))))
                  ((e ValueError) (begin (log.append "caught") 5)))))
    (log.append r)
    log

def wind_raise_pending_one []:
    reset:
        (+ 1 (try (dyn-wind nil (shift k 0) (raise (ValueError "x")))
                  ((e ValueError) 5)))

def wind_raise_pending_two []:
    reset:
        (+ 10 (+ 20 (try (dyn-wind nil (shift k 0) (raise (ValueError "x")))
                         ((e ValueError) 5))))

def break_then_shift []:
    var r (reset (while True (try (shift k 0) [finally: (break 5)])))
    var s (reset (+ 100 (shift k (k 1))))
    #[r s]
"""


class TestShiftAbortCancelled(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("shift_abort_cancelled")
        vars(module)["__file__"] = "<shift_abort_cancelled>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_finally_raise_does_not_run_one_pending(self):
        mod = await self.load()
        self.assertEqual(0, await mod["raise_pending_one"]())


    async def test_finally_raise_does_not_run_nothing_pending(self):
        mod = await self.load()
        self.assertEqual(0, await mod["raise_pending_none"]())


    async def test_finally_raise_does_not_run_two_pending(self):
        mod = await self.load()
        self.assertEqual(0, await mod["raise_pending_two"]())


    async def test_callee_finally_raise_does_not_run(self):
        mod = await self.load()
        self.assertEqual(0, await mod["raise_in_callee"]())


    async def test_finally_break_does_not_run(self):
        mod = await self.load()
        self.assertEqual(0, await mod["break_pending_one"]())


    async def test_finally_continue_does_not_run(self):
        mod = await self.load()
        self.assertEqual(0, await mod["continue_pending_one"]())


    async def test_callee_finally_return_does_not_run(self):
        mod = await self.load()
        self.assertEqual(0, await mod["return_in_callee"]())


    async def test_raise_in_wind_after_body(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "after", "caught", 6],
            await mod["wind_after_raises"]())


    async def test_wind_after_raise_keeps_one_pending_operand(self):
        mod = await self.load()
        self.assertEqual(6, await mod["wind_raise_pending_one"]())


    async def test_wind_after_raise_keeps_two_pending_operands(self):
        mod = await self.load()
        self.assertEqual(35, await mod["wind_raise_pending_two"]())


    async def test_shift_after_an_unrun_finally_break(self):
        mod = await self.load()
        self.assertEqual([0, 101], await mod["break_then_shift"]())


# The end.
