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

A shift leaves its region before its handler runs, and a finally body
or a dyn-wind after body on the way out runs during that leave. When
that body raises, breaks, continues or returns, the abort is cancelled
and the region carries on from where the escape lands, with the
operands it had pending around the shift still in place.

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


    async def test_raise_in_finally_keeps_one_pending_operand(self):
        mod = await self.load()
        self.assertEqual(6, await mod["raise_pending_one"]())


    async def test_raise_in_finally_with_nothing_pending(self):
        mod = await self.load()
        self.assertEqual(5, await mod["raise_pending_none"]())


    async def test_raise_in_finally_keeps_two_pending_operands(self):
        mod = await self.load()
        self.assertEqual(35, await mod["raise_pending_two"]())


    async def test_raise_in_a_callee_finally(self):
        mod = await self.load()
        self.assertEqual(6, await mod["raise_in_callee"]())


    async def test_break_out_of_finally(self):
        mod = await self.load()
        self.assertEqual(6, await mod["break_pending_one"]())


    async def test_continue_out_of_finally(self):
        mod = await self.load()
        self.assertEqual(4, await mod["continue_pending_one"]())


    async def test_return_out_of_a_callee_finally(self):
        mod = await self.load()
        self.assertEqual(6, await mod["return_in_callee"]())


    async def test_raise_in_wind_after_body(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "after", "caught", 6],
            await mod["wind_after_raises"]())


    async def test_shift_after_a_break_escape(self):
        mod = await self.load()
        self.assertEqual([5, 101], await mod["break_then_shift"]())


# The end.
