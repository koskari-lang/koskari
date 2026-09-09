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

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestLoopOpcodes(CevalTest):
    """
    Tests for PUSH_LOOP, POP_LOOP, CLEAR_LOOP, CLEAR_FOREACH, BREAK, CONTINUE.

    PUSH_LOOP initializes region->result to None.
    BREAK/CONTINUE update region->result with their arg-derived value.
    POP_LOOP pushes region->result to the stack as the loop expression value.
    """

    async def test_simple_loop_break(self):
        """
        PUSH_LOOP, body, BREAK, POP_LOOP returns value after loop.
        POP_LOOP pushes the loop result (None from BREAK 0).
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
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
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_push_loop_with_pop_loop_completes(self):
        """
        PUSH_LOOP with matching POP_LOOP completes normally.
        POP_LOOP pushes the loop result (None) to the stack.
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_loop_continue(self):
        """
        PUSH_LOOP with CONTINUE jumps back to loop head
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "n"],
            ["PUSH_LOOP", 0],
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
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)


    async def test_nested_loops_break_inner(self):
        """
        Nested loops: inner BREAK exits inner loop only
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "outer"],
            ["CONST", 0],
            ["VAR_STORE", "inner"],
            ["PUSH_LOOP", 0],
            ["CONST", 0],
            ["VAR_STORE", "inner"],
            ["PUSH_LOOP", 0],
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
        result = await make_callable(code, ns)()
        self.assertEqual(result, 2)


    async def test_nested_loops_break_inner_then_outer(self):
        """
        Nested loops: BREAK exits innermost loop; outer loop then breaks too
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "x"],
            ["PUSH_LOOP", 0],
            ["PUSH_LOOP", 0],
            ["CONST", 1],
            ["VAR_STORE", "x"],
            ["BREAK", 0],
            ["LABEL", "inner_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed inner loop result (None)
            ["POP", 1],
            # x is now 1; break the outer loop too
            ["BREAK", 0],
            ["LABEL", "outer_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed outer loop result
            ["POP", 1],
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 0})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_break_outside_loop_raises(self):
        """
        BREAK without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["BREAK", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("break outside loop", str(ctx.exception))


    async def test_continue_outside_loop_raises(self):
        """
        CONTINUE without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["CONTINUE", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("continue outside loop", str(ctx.exception))


    async def test_clear_loop_without_push_raises(self):
        """
        CLEAR_LOOP with no LOOP region on the chain raises RuntimeError
        """

        code_obj, ns = build_expr([["CLEAR_LOOP", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("CLEAR_LOOP: top is not a LOOP region", str(ctx.exception))


    async def test_clear_foreach_without_push_raises(self):
        """
        CLEAR_FOREACH with no FOREACH region on the chain raises RuntimeError
        """

        code_obj, ns = build_expr([["CLEAR_FOREACH", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("CLEAR_FOREACH: top is not a FOREACH region",
                      str(ctx.exception))


    async def test_clear_loop_requires_loop_not_foreach(self):
        """
        CLEAR_LOOP rejects a FOREACH region (use CLEAR_FOREACH).
        """

        code, ns = build_expr([
            ["CONST", []],
            ["PUSH_FOREACH", 0],
            ["CLEAR_LOOP", 0],
            ["POP_FOREACH", 0],
            ["RETURN", 0],
        ])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("CLEAR_LOOP: top is not a LOOP region", str(ctx.exception))


    async def test_clear_foreach_requires_foreach_not_loop(self):
        """
        CLEAR_FOREACH rejects a LOOP region (use CLEAR_LOOP).
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["CLEAR_FOREACH", 0],
            ["POP_LOOP", 0],
            ["RETURN", 0],
        ])
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("CLEAR_FOREACH: top is not a FOREACH region",
                      str(ctx.exception))


    async def test_pop_loop_without_push_raises(self):
        """
        POP_LOOP with no matching PUSH_LOOP raises RuntimeError (finalize or run).
        """

        with self.assertRaises(RuntimeError):
            code_obj, ns = build_expr([["POP_LOOP", 0], ["RETURN", 1]])
            await make_callable(code_obj, ns)()


    def test_extent_foreach_requires_pop_foreach(self):
        """
        PUSH_FOREACH must close with POP_FOREACH; POP_LOOP is rejected at finalize.
        """

        with self.assertRaises(RuntimeError) as ctx:
            build_expr([
                ["CONST", []],
                ["PUSH_FOREACH", 0],
                ["POP_LOOP", 0],
                ["RETURN", 0],
            ])
        self.assertIn("extent mismatch", str(ctx.exception))


    def test_extent_loop_requires_pop_loop(self):
        """
        PUSH_LOOP must close with POP_LOOP; POP_FOREACH is rejected at finalize.
        """

        with self.assertRaises(RuntimeError) as ctx:
            build_expr([
                ["PUSH_LOOP", 0],
                ["POP_FOREACH", 0],
                ["RETURN", 0],
            ])
        self.assertIn("extent mismatch", str(ctx.exception))


# The end.
