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
tests.reproducers.test_abort

An abort leaves everything up to its prompt and runs its handler there
in a frame of its own, with the prompt still in place; the handler's
value is the reset's. It is a shift that captures no continuation:
abort-at seeks a reset-at by tag as shift-at does, a dyn-wind around
the abort runs its after thunk first, and a handler may return, shift
or abort again.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import (
    KoskariPromptNotFound, KoskariPromptUnreachable, SpecialError)
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def abort_value []:
    (reset (+ 1 (abort 5)))

def pending_outside []:
    (* 3 (reset (+ 1 (abort 4))))

def callee_aborts []:
    var n 11
    (abort (+ n 1))

def abort_in_callee []:
    reset:
        (+ 100 (callee_aborts))

def abort_at_crosses_a_reset []:
    var t (object)
    (reset-at t (+ 1 (reset (+ 10 (abort-at t 7)))))

def abort_stops_at_the_plain_reset []:
    var t (object)
    (reset-at t (+ 1 (reset (+ 10 (abort 7)))))

def abort_at_none []:
    (reset (+ 1 (abort-at None 5)))

def wind_after_runs_first []:
    var log #[]
    var r (reset
        (dyn-wind (log.append "before")
                  (abort (begin (log.append "handler") 3))
                  (log.append "after")))
    (log.append r)
    log

def abort_in_before_thunk []:
    var log #[]
    var r (reset
        (dyn-wind (abort (begin (log.append "handler") 3))
                  (log.append "body")
                  (log.append "after")))
    (log.append r)
    log

def wind_after_raises []:
    var log #[]
    var r (reset
        (+ 1 (try (dyn-wind (log.append "before")
                            (abort (begin (log.append "handler") 0))
                            (begin (log.append "after")
                                   (raise (ValueError "x"))))
                  [[e ValueError] (begin (log.append "caught") 5)])))
    (log.append r)
    log

def abort_in_handler []:
    (reset (+ 1 (abort (* 2 (abort 4)))))

def shift_in_handler []:
    (reset (+ 1 (abort (* 2 (shift k (k 5))))))

def abort_in_shift_handler []:
    (reset (+ 1 (shift k (* 2 (abort (k 5))))))

def return_in_handler []:
    var r (reset (+ 1 (abort (return 5))))
    (* 10 r)

def raise_passes_region_try []:
    try:
        reset:
            try:
                (+ 1 (abort (raise (ValueError "x"))))
            except [e ValueError]:
                "inside"
    except [e ValueError]:
        "outside"

def break_outer_loop []:
    try:
        (while True (reset (abort (break 9))))
    except [e RuntimeError]:
        (str e)

def abort_in_copy []:
    (reset (+ (shift k (+ 100 (k 1))) (abort 10)))

class RefusesEntry [object]:
    def __enter__ [self]:
        (abort "from enter")
    def __exit__ [self *exc]:
        (raise (RuntimeError "exit of a manager never entered"))

def abort_in_enter []:
    reset:
        with [m (RefusesEntry)]:
            "body"

def no_reset []:
    (abort 1)

def no_reset_at [t]:
    (abort-at t 1)

def aborting_generator []:
    yield (abort 9)

def across_generator []:
    (reset (list (aborting_generator)))
"""


YIELD_SRC = """
def yield_in_handler []:
    reset:
        + 100:
            abort:
                yield 1
"""


class TestAbort(unittest.IsolatedAsyncioTestCase):

    async def load(self, src=SRC):
        module = new_module("abort")
        vars(module)["__file__"] = "<abort>"
        await load_module_source_async(module, src)
        return vars(module)


    async def test_handler_value_is_the_reset_value(self):
        mod = await self.load()
        self.assertEqual(5, await mod["abort_value"]())


    async def test_operands_outside_the_reset(self):
        mod = await self.load()
        self.assertEqual(12, await mod["pending_outside"]())


    async def test_handler_reads_the_callee_local(self):
        mod = await self.load()
        self.assertEqual(12, await mod["abort_in_callee"]())


    async def test_abort_at_crosses_a_reset_of_another_tag(self):
        mod = await self.load()
        self.assertEqual(7, await mod["abort_at_crosses_a_reset"]())


    async def test_abort_stops_at_the_plain_reset(self):
        mod = await self.load()
        self.assertEqual(8, await mod["abort_stops_at_the_plain_reset"]())


    async def test_none_is_the_default_tag(self):
        mod = await self.load()
        self.assertEqual(5, await mod["abort_at_none"]())


    async def test_wind_after_runs_before_the_handler(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "after", "handler", 3],
            await mod["wind_after_runs_first"]())


    async def test_abort_in_a_before_thunk_leaves_the_wind_unentered(self):
        mod = await self.load()
        self.assertEqual(
            ["handler", 3], await mod["abort_in_before_thunk"]())


    async def test_abort_in_an_enter_leaves_the_with_unentered(self):
        mod = await self.load()
        self.assertEqual("from enter", await mod["abort_in_enter"]())


    async def test_wind_after_raise_cancels_the_handler(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "after", "caught", 6],
            await mod["wind_after_raises"]())


    async def test_abort_in_a_handler_finds_the_prompt(self):
        mod = await self.load()
        self.assertEqual(4, await mod["abort_in_handler"]())


    async def test_shift_in_a_handler_finds_the_prompt(self):
        mod = await self.load()
        self.assertEqual(10, await mod["shift_in_handler"]())


    async def test_abort_in_a_shift_handler(self):
        mod = await self.load()
        self.assertEqual(6, await mod["abort_in_shift_handler"]())


    async def test_return_in_a_handler_is_the_reset_value(self):
        mod = await self.load()
        self.assertEqual(50, await mod["return_in_handler"]())


    async def test_handler_raise_passes_a_region_try(self):
        mod = await self.load()
        self.assertEqual("outside", await mod["raise_passes_region_try"]())


    async def test_break_does_not_reach_a_loop_around_the_reset(self):
        mod = await self.load()
        self.assertEqual("break outside loop", await mod["break_outer_loop"]())


    async def test_abort_in_a_resumed_copy(self):
        # the copy's abort goes to the copy's prompt: its 10 is the
        # value of (k 1)
        mod = await self.load()
        self.assertEqual(110, await mod["abort_in_copy"]())


    async def test_yield_in_a_handler_is_refused(self):
        with self.assertRaises(SpecialError) as caught:
            await self.load(YIELD_SRC)
        self.assertIn("abort handler cannot yield", str(caught.exception))


    async def test_abort_without_a_reset(self):
        mod = await self.load()
        with self.assertRaises(KoskariPromptNotFound) as caught:
            await mod["no_reset"]()
        exc = caught.exception
        self.assertEqual("abort has no enclosing reset", str(exc))
        self.assertEqual("abort", exc.operation)
        self.assertIsNone(exc.tag)


    async def test_abort_at_without_its_reset_at(self):
        mod = await self.load()
        tag = object()
        with self.assertRaises(KoskariPromptNotFound) as caught:
            await mod["no_reset_at"](tag)
        exc = caught.exception
        self.assertEqual(
            "abort-at has no enclosing reset-at carrying its tag", str(exc))
        self.assertEqual("abort", exc.operation)
        self.assertIs(tag, exc.tag)


    async def test_abort_across_a_generator(self):
        mod = await self.load()
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await mod["across_generator"]()
        exc = caught.exception
        self.assertEqual("cannot abort across a generator boundary", str(exc))
        self.assertEqual("abort", exc.operation)
        self.assertEqual("generator", exc.boundary)


# The end.
