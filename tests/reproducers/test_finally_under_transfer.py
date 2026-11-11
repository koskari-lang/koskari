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
tests.reproducers.test_finally_under_transfer

A finally body runs on every exit from its try, a continuation
transfer included: a call/cc escape out of the try, in the same frame
or across one, and a shift handler that abandons the captured slice.
It runs once per exit, so a slice re-entered through its continuation
runs it again when that run leaves, the way a dyn-wind after thunk
does. Nested regions leave inner first, a with on the same path runs
its __exit__ in order with them, and an escape issued from inside a
running finally body abandons the rest of that body.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
class Ctx []:
    def __init__ [self log name]:
        (setattr self "log" log)
        (setattr self "name" name)
    def __enter__ [self]:
        (self.log.append (+ "enter " self.name))
        self
    def __exit__ [self *: args]:
        (self.log.append (+ "exit " self.name))
        False

def escape_same_frame [log]:
    var r (call/cc (lambda [k]
        (try (k "escaped")
            [finally: (log.append "finally")])))
    (log.append r)
    log

def leave_via [log k]:
    try:
        (log.append "body")
        (k "escaped")
    finally:
        (log.append "finally")

def escape_across_frame [log]:
    var r (call/cc (lambda [k] (leave_via log k)))
    (log.append r)
    log

def escape_nested [log]:
    var r (call/cc (lambda [k]
        (try
            (with [c (Ctx log "c")]
                (try (k "escaped")
                    [finally: (log.append "inner")]))
            [finally: (log.append "outer")])))
    (log.append r)
    log

def shift_abandons_slice [log]:
    var r (reset
        (try (begin (shift k (begin (log.append "handler") 10)) (log.append "resumed"))
            [finally: (log.append "finally")]))
    (log.append r)
    log

def reenter [log]:
    var saved None
    var r (reset
        (try (shift k (begin (set! saved k) "first"))
            [finally: (log.append "finally")]))
    (log.append r)
    (log.append (saved "second"))
    (log.append (saved "third"))
    log

def escape_from_finally_body [log]:
    var r (call/cc (lambda [k]
        (try None
            [finally: (log.append "before") (k "escaped") (log.append "after")])))
    (log.append r)
    log
"""


class TestFinallyUnderTransfer(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("finally_under_transfer")
        vars(module)["__file__"] = "<finally_under_transfer>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_escape_same_frame(self):
        mod = await self.load()
        self.assertEqual(
            ["finally", "escaped"], await mod["escape_same_frame"]([]))


    async def test_escape_across_frame(self):
        mod = await self.load()
        self.assertEqual(
            ["body", "finally", "escaped"],
            await mod["escape_across_frame"]([]))


    async def test_escape_nested_inner_first(self):
        mod = await self.load()
        self.assertEqual(
            ["enter c", "inner", "exit c", "outer", "escaped"],
            await mod["escape_nested"]([]))


    async def test_shift_handler_abandoning_slice(self):
        mod = await self.load()
        self.assertEqual(
            ["finally", "handler", 10],
            await mod["shift_abandons_slice"]([]))


    async def test_reentry_runs_finally_per_exit(self):
        mod = await self.load()
        self.assertEqual(
            ["finally", "first",
             "finally", "second",
             "finally", "third"],
            await mod["reenter"]([]))


    async def test_escape_from_running_finally_body(self):
        mod = await self.load()
        self.assertEqual(
            ["before", "escaped"],
            await mod["escape_from_finally_body"]([]))


# The end.
