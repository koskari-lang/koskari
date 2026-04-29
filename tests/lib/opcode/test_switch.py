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
Tests for SWITCH_EQ opcode (peek-discriminant equality dispatch)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.7 via Cursor
"""


from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestSwitchEqOpcode(CevalTest):
    """
    Tests for SWITCH_EQ.

    Stack convention `[..., left, right]` with right = TOS. On match
    both are popped and execution falls through; on mismatch only the
    key (right) is popped and execution jumps to the label, leaving
    the discriminant on the stack for the next arm.
    """


    async def test_switch_eq_match_falls_through(self):
        """
        SWITCH_EQ pops both operands and falls through when left == right
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 10],
            ["SWITCH_EQ", "miss"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ retains the discriminant on the stack on mismatch so
        the next arm can compare another key against it without reloading
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 11],
            ["SWITCH_EQ", "next_arm"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["CONST", 10],
            ["SWITCH_EQ", "else"],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_switch_eq_chain_to_else(self):
        """
        SWITCH_EQ chain falls to else when no arm matches, leaving the
        retained discriminant for the else block to drop
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["CONST", 1],
            ["SWITCH_EQ", "arm2"],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["CONST", 2],
            ["SWITCH_EQ", "else"],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_switch_eq_match_first_arm_skips_rest(self):
        """
        After a matching arm, an explicit JUMP bypasses subsequent arms
        and a leftover discriminant is not observable in the result
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 10],
            ["SWITCH_EQ", "arm2"],
            ["CONST", 42],
            ["JUMP", "done"],
            ["LABEL", "arm2"],
            ["CONST", 20],
            ["SWITCH_EQ", "else"],
            ["CONST", 99],
            ["JUMP", "done"],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["LABEL", "done"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_int_bool_equality(self):
        """
        SWITCH_EQ uses Py_EQ semantics: 1 == True matches
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", True],
            ["SWITCH_EQ", "miss"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_int_float_equality(self):
        """
        SWITCH_EQ uses Py_EQ semantics: 0 == 0.0 matches
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", 0.0],
            ["SWITCH_EQ", "miss"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


# The end.
