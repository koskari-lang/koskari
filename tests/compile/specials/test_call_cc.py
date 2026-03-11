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
Tests for koskari.compile compile/call/cc, compile/let/cc, and
compile/capture/cc special forms.

call/cc captures the current continuation and passes it to a callable.
let/cc pushes a child namespace, captures the continuation and binds it
to a name inside that namespace, then runs a body.
capture/cc captures the continuation and binds it to a name in the
current (ambient) namespace, then runs a body.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import unittest

import koskari.lib as lib

from koskari.exceptions import SpecialError

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestCallCcSpecial(SpecialsTestCase):

    async def test_call_cc_invokes_continuation_with_value(self):
        """
        (call/cc (lambda [k] (k 42))) invokes the continuation with 42 and
        the result is 42
        """

        src_str = "(compile/call/cc (lambda [k] (compile/call k 42)))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_call_cc_returns_normally_when_continuation_not_invoked(self):
        """
        (call/cc fn) where fn ignores k and returns 99 yields 99
        """

        def fn(k):
            return 99

        src_str = "(compile/call/cc fn)"
        expr, mod = self.compile_expr(src_str, fn=fn)
        result = await expr()
        self.assertEqual(result, 99)


    async def test_call_cc_captures_callable_continuation(self):
        """
        (call/cc fn) where fn returns k unapplied yields a Continuation object
        """

        def fn(k):
            return k

        src_str = "(compile/call/cc fn)"
        expr, mod = self.compile_expr(src_str, fn=fn)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertIsInstance(result, lib.Continuation)


    async def test_call_cc_python_callable_invokes_continuation_upward(self):
        """
        When a plain Python callable is passed to call/cc and calls k from
        within Python, the KoskariContinuationApply exception propagates up
        through the Python call frame and is caught by the owning koskari eval
        loop, which applies the continuation and resumes with the given value
        """

        def fn(k):
            return k(42)

        src_str = "(compile/call/cc fn)"
        expr, mod = self.compile_expr(src_str, fn=fn)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_call_cc_wrong_arg_count_raises(self):
        """
        (call/cc) with no arguments raises SpecialError
        """

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/call/cc)")


    async def test_call_cc_too_many_args_raises(self):
        """
        (call/cc f g) with two arguments raises SpecialError
        """

        def fn(k):
            return k

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/call/cc fn fn)", fn=fn)


class TestLetCcSpecial(SpecialsTestCase):

    async def test_let_cc_runs_body_and_returns_value(self):
        """
        (let/cc k 42) binds k but does not invoke it; body returns 42
        """

        src_str = "(compile/let/cc k 42)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_let_cc_invoke_continuation_escapes_body(self):
        """
        (let/cc k (k 7) 99) invokes k with 7, bypassing the rest of the body;
        result is 7
        """

        src_str = "(compile/let/cc k (compile/call k 7) 99)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 7)


    async def test_let_cc_continuation_variable_must_be_symbol(self):
        """
        (let/cc 42 body) raises SpecialError because 42 is not a symbol
        """

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/let/cc 42 0)")


    async def test_let_cc_k_is_continuation_instance(self):
        """
        (let/cc k k) returns the captured continuation as a Continuation object
        """

        src_str = "(compile/let/cc k k)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertIsInstance(result, lib.Continuation)


    async def test_let_cc_k_not_visible_outside(self):
        """
        k is scoped to the let/cc child namespace and is not visible in the
        enclosing scope after the form completes
        """

        src_str = """
        (compile/begin
          (compile/let/cc k 0)
          k)
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(Exception):
            await expr()


class TestCaptureCcSpecial(SpecialsTestCase):

    async def test_capture_cc_runs_body_and_returns_value(self):
        """
        (let [[k 0]] (capture/cc k 42)) binds the continuation to k in the let
        namespace and the body yields 42
        """

        src_str = """
        (compile/let [[k 0]]
          (compile/capture/cc k 42))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_capture_cc_k_is_continuation_instance(self):
        """
        After (capture/cc k ...) the binding k holds a Continuation object
        """

        results = []

        def record(val):
            results.append(val)
            return val

        src_str = """
        (compile/let [[k 0]]
          (compile/capture/cc k
            (compile/call record k)))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        await expr()
        self.assertEqual(len(results), 1)
        self.assertIsInstance(results[0], lib.Continuation)


    async def test_capture_cc_invoke_continuation_escapes_body(self):
        """
        Invoking k inside capture/cc body resumes after the CC block with the
        given value, bypassing the remainder of the body
        """

        src_str = """
        (compile/let [[k 0]]
          (compile/capture/cc k
            (compile/call k 55)
            99))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 55)


    async def test_capture_cc_continuation_variable_must_be_symbol(self):
        """
        (capture/cc 42 body) raises SpecialError because 42 is not a symbol
        """

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/capture/cc 42 0)")


# The end.
