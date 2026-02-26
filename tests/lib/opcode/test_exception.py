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
Tests for EXCEPTION, RAISE, PUSH_TRY opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestExceptionOpcodes(CevalTest):
    """
    Tests for EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH, RAISE,
    PUSH_TRY, POP_TRY, PUSH_FINALLY, POP_FINALLY, CLEAR_TRY, CLEAR_FINALLY,
    CHECK_EXCEPTION
    """

    def test_push_try_pop_try_exception_handler(self):
        """
        PUSH_TRY, RAISE, EXCEPTION, POP, POP_TRY: catch and return 42
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_eval_try_except_catch(self):
        """
        Mirrors native test_eval_try_except_catch: same bytecode operations.
        PUSH_TRY 3, CONST (ValueError), RAISE 1, LABEL 3, EXCEPTION 0,
        POP 1, CONST 42, POP_TRY 1, RETURN 1. Handler at LABEL. Expect 42.
        Run with: tox -e gdb -- tests/lib/opcode/test_exception.py::TestExceptionOpcodes::test_eval_try_except_catch
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_push_finally_pop_finally_runs_on_raise(self):
        """
        PUSH_FINALLY, RAISE, finally block runs, POP_FINALLY
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_FINALLY", "finally"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "finally"],
            ["CONST", 99],
            ["POP", 1],
            ["EXCEPTION", 0],
            ["POP_FINALLY", 0],
            ["RAISE", 1],
            ["NOOP", 0],
        ])
        with self.assertRaises(ValueError):
            run_to_completion(make_callable(code, ns)())


    def test_exception_push_current_exception(self):
        """
        EXCEPTION pushes current exception onto stack; handler pops and returns 42
        """

        exc_inst = RuntimeError("x")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_raise_propagates_exception(self):
        """
        RAISE 1 raises the exception instance on stack
        """

        exc_inst = TypeError("raised")
        code, ns = build_expr([
            ["CONST", exc_inst],
            ["RAISE", 1],
        ])
        with self.assertRaises(TypeError) as ctx:
            run_to_completion(make_callable(code, ns)())
        self.assertIn("raised", str(ctx.exception))


    def test_clear_try_clears_try_region(self):
        """
        CLEAR_TRY marks try region as cleared
        """

        code, ns = build_expr([
            ["PUSH_TRY", "skip"],
            ["CONST", 1],
            ["CLEAR_TRY", 0],
            ["POP_TRY", 0],
            ["POP", 1],
            ["LABEL", "skip"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_clear_finally_clears_finally_region(self):
        """
        CLEAR_FINALLY marks finally region as cleared
        """

        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 42],
            ["CLEAR_FINALLY", 0],
            ["POP_FINALLY", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "fin"],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_exception_clear(self):
        """
        EXCEPTION_CLEAR clears current exception state
        """

        code, ns = build_expr([
            ["PUSH_TRY", "h"],
            ["CONST", ValueError("x")],
            ["RAISE", 1],
            ["LABEL", "h"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["EXCEPTION_CLEAR", 0],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_exception_match(self):
        """
        EXCEPTION_MATCH checks if current exception matches type on stack
        """

        code, ns = build_expr([
            ["PUSH_TRY", "h"],
            ["CONST", ValueError("v")],
            ["RAISE", 1],
            ["LABEL", "h"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", ValueError],
            ["EXCEPTION_MATCH", 1],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, True)


    def test_check_exception_jumps_if_no_exception(self):
        """
        CHECK_EXCEPTION jumps to label if no exception set
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CHECK_EXCEPTION", "ok"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "ok"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_try_stack_block_isolated_on_exception(self):
        """
        Body pushes values, raises; handler sees clean stack (block cleared)
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_finally_stack_block_isolated_on_exception(self):
        """
        Body pushes values, raises; finally sees clean stack (block cleared)
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_FINALLY", "finally"],
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "finally"],
            ["CONST", 99],
            ["POP_FINALLY", 0],
            ["POP", 1],
            ["EXCEPTION", 0],
            ["RAISE", 1],
            ["NOOP", 0],
        ])
        with self.assertRaises(ValueError):
            run_to_completion(make_callable(code, ns)())


    def test_try_normal_path_drops_block(self):
        """
        No exception; body runs, CLEAR_TRY, POP_TRY, POP 1, then RETURN
        """

        code, ns = build_expr([
            ["PUSH_TRY", "skip"],
            ["CONST", 1],
            ["CLEAR_TRY", 0],
            ["POP_TRY", 0],
            ["POP", 1],
            ["LABEL", "skip"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_finally_normal_path_drops_block(self):
        """
        Normal path, CLEAR_FINALLY, POP_FINALLY
        """

        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 42],
            ["CLEAR_FINALLY", 0],
            ["POP_FINALLY", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "fin"],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_try_stack_depth_on_handler_entry(self):
        """
        Body pushes N values, raises; handler gets EXCEPTION (stack cleared)
        """

        exc_inst = RuntimeError("x")
        code, ns = build_expr([
            ["PUSH_TRY", "h"],
            ["CONST", 10],
            ["CONST", 20],
            ["CONST", 30],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "h"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_finally_runs_on_normal_exit(self):
        """
        Body completes, CLEAR_FINALLY, jump to finally, POP_FINALLY
        """

        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 100],
            ["CLEAR_FINALLY", 0],
            ["LABEL", "fin"],
            ["CONST", 5],
            ["POP_FINALLY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 5)


    def test_break_through_try_drops_block(self):
        """
        PUSH_TRY inside PUSH_LOOP; BREAK unwinds through TRY (drops its block)
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["PUSH_TRY", "handler"],
            ["CONST", 1],
            ["BREAK", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", None],
            ["POP_TRY", 0],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_nested_try_finally(self):
        """
        Outer PUSH_FINALLY, inner PUSH_TRY; inner raises, handler re-raises; finally runs
        """

        exc_inst = ValueError("nested")
        code, ns = build_expr([
            ["PUSH_FINALLY", "outer_fin"],
            ["PUSH_TRY", "inner_h"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "inner_h"],
            ["EXCEPTION", 0],
            ["POP_TRY", 0],
            ["RAISE", 1],
            ["LABEL", "outer_fin"],
            ["CONST", 99],
            ["POP", 1],
            ["EXCEPTION", 0],
            ["POP_FINALLY", 0],
            ["RAISE", 1],
            ["NOOP", 0],
        ])
        with self.assertRaises(ValueError):
            run_to_completion(make_callable(code, ns)())

# The end.
