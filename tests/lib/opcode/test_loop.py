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
Tests for PUSH_LOOP, POP_LOOP, BREAK, CONTINUE opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestLoopOpcodes(CevalTest):
    """
    Tests for PUSH_LOOP, POP_LOOP, CLEAR_LOOP, BREAK, CONTINUE.

    PUSH_LOOP initializes region->result to None.
    BREAK/CONTINUE update region->result with their arg-derived value.
    POP_LOOP pushes region->result to the stack as the loop expression value.
    """

    def test_simple_loop_break(self):
        """
        PUSH_LOOP, body, BREAK, POP_LOOP returns value after loop.
        POP_LOOP pushes the loop result (None from BREAK 0).
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "after_pop"],
            ["CONST", 99],
            ["BREAK", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_pop"],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_push_loop_with_pop_loop_completes(self):
        """
        PUSH_LOOP with matching POP_LOOP completes normally.
        POP_LOOP pushes the loop result (None) to the stack.
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "break_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_loop_continue(self):
        """
        PUSH_LOOP with CONTINUE jumps back to loop head
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "n"],
            ["PUSH_LOOP", "break_lbl"],
            ["VAR", "n"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "n"],
            ["VAR", "n"],
            ["CONST", 3],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "break_lbl"],
            ["CONTINUE", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["VAR", "n"],
            ["RETURN", 1],
        ], values={"n": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 3)


    def test_nested_loops_break_inner(self):
        """
        Nested loops: inner BREAK exits inner loop only
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "outer"],
            ["CONST", 0],
            ["VAR_BIND", "inner"],
            ["PUSH_LOOP", "outer_end"],
            ["CONST", 0],
            ["VAR_STORE", "inner"],
            ["PUSH_LOOP", "inner_end"],
            ["VAR", "inner"],
            ["CONST", 2],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "inner_end"],
            ["VAR", "inner"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "inner"],
            ["CONTINUE", 0],
            ["LABEL", "inner_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed inner loop result
            ["POP", 1],
            ["VAR", "outer"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "outer"],
            ["VAR", "outer"],
            ["CONST", 2],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "outer_end"],
            ["CONTINUE", 0],
            ["LABEL", "outer_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed outer loop result
            ["POP", 1],
            ["VAR", "outer"],
            ["RETURN", 1],
        ], values={"outer": 0, "inner": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 2)


    def test_nested_loops_break_outer(self):
        """
        Nested loops: inner BREAK targets outer exit, exiting both loops
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "x"],
            ["PUSH_LOOP", "outer_end"],
            ["PUSH_LOOP", "outer_end"],
            ["CONST", 1],
            ["VAR_STORE", "x"],
            ["BREAK", 0],
            ["LABEL", "inner_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed inner loop result
            ["POP", 1],
            ["LABEL", "outer_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed outer loop result
            ["POP", 1],
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_break_outside_loop_raises(self):
        """
        BREAK without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["BREAK", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            run_to_completion(make_callable(code_obj, ns)())
        self.assertIn("break outside loop", str(ctx.exception))


    def test_continue_outside_loop_raises(self):
        """
        CONTINUE without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["CONTINUE", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            run_to_completion(make_callable(code_obj, ns)())
        self.assertIn("continue outside loop", str(ctx.exception))


    def test_clear_loop_outside_loop_raises(self):
        """
        CLEAR_LOOP without preceding PUSH_LOOP/PUSH_FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["CLEAR_LOOP", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_pop_loop_without_push_loop_raises(self):
        """
        POP_LOOP without preceding PUSH_LOOP/PUSH_FOREACH raises RuntimeError
        (at code build or run).
        """

        with self.assertRaises(RuntimeError):
            code_obj, ns = build_expr([["POP_LOOP", 0], ["RETURN", 1]])
            run_to_completion(make_callable(code_obj, ns)())

# The end.
