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
Tests for BREAK/CONTINUE result values opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


class TestLoopResultOpcodes(CevalTest):
    """
    Tests for break/continue result values and loop result semantics.

    BREAK N and CONTINUE N pop N values from the stack:
    - N=0: result is None
    - N=1: result is the single popped value
    - N>1: result is a tuple of the N popped values

    BREAK pushes the result and jumps to break target.
    CONTINUE updates region->result and jumps to continue target.
    POP_LOOP pushes region->result as the loop expression value.
    """

    async def test_break_with_single_value(self):
        """
        BREAK 1 pops one value and uses it as the loop result
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["CONST", 42],
            ["BREAK", 1],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK pushed the result (42)
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_break_with_multiple_values_creates_tuple(self):
        """
        BREAK 3 pops three values and creates a tuple result
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["BREAK", 3],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK pushed the result (1, 2, 3)
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (1, 2, 3))


    async def test_continue_with_single_value(self):
        """
        CONTINUE 1 pops one value and updates region->result
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "n"],
            ["PUSH_LOOP", 0],
            ["VAR", "n"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["DUP", 1],
            ["VAR_STORE", "n"],
            # Stack now has n on top; use it as continue result
            ["VAR", "n"],
            ["CONST", 3],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "break_lbl"],
            ["CONTINUE", 1],  # continue with n as result
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed the last continue result
            ["RETURN", 1],
        ], values={"n": 0})
        result = await make_callable(code, ns)()
        # Loop runs 3 times: n=1, n=2, n=3; last continue was with n=2
        self.assertEqual(result, 2)


    async def test_pop_loop_returns_loop_result(self):
        """
        POP_LOOP pushes region->result (None when no break/continue)
        """

        code, ns = build_expr([
            ["PUSH_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed None (initial result)
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)


    async def test_foreach_break_with_value(self):
        """
        FOREACH BREAK 1 uses the value as the loop result
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", [10, 20, 30]],  # iterable
            ["PUSH_FOREACH", 0],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            # Stack: [iter_value]
            ["DUP", 1],
            ["CONST", 20],
            ["OBJECT_EQ", 2],
            # Stack: [iter_value, is_20]
            ["JUMP_FALSE", "loop_cont"],
            # If iter_value == 20, break with 99
            ["POP", 1],  # discard iter_value
            ["CONST", 99],
            ["BREAK", 1],  # break with value 99
            ["LABEL", "loop_cont"],
            # Stack: [iter_value]
            ["POP", 1],  # discard the iteration value
            ["CONTINUE", 0],  # continue to next iteration
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK or POP_LOOP pushed the result
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


# The end.
