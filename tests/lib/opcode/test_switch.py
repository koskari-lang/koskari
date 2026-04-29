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


from koskari.lib import FormalsSpec

from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

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


class TestSwitchEqConstOpcode(CevalTest):
    """
    Tests for SWITCH_EQ_CONST.

    Stack `[..., s]` with the scrutinee at TOS. EXTRA schema
    (LABEL, CONST, NOARG, NOARG) loads the RHS from the constant pool.
    Match drops the discriminant; mismatch retains it and jumps to the
    label slot.
    """


    async def test_switch_eq_const_match_falls_through(self):
        """
        SWITCH_EQ_CONST drops the discriminant on match and falls through
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_CONST", ("miss", 10, 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_const_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ_CONST retains the discriminant on mismatch so the next
        arm can compare another key against it
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_CONST", ("next_arm", 11, 0, 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["SWITCH_EQ_CONST", ("else", 10, 0, 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_switch_eq_const_chain_to_else(self):
        """
        SWITCH_EQ_CONST chain falls to else when no arm matches
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["SWITCH_EQ_CONST", ("arm2", 1, 0, 0)],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["SWITCH_EQ_CONST", ("else", 2, 0, 0)],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_switch_eq_const_int_bool_equality(self):
        """
        SWITCH_EQ_CONST uses Py_EQ semantics: 1 == True matches
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["SWITCH_EQ_CONST", ("miss", True, 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


class TestSwitchEqVarOpcode(CevalTest):
    """
    Tests for SWITCH_EQ_VAR.

    EXTRA schema (LABEL, VAR, NOARG, NOARG) loads the RHS from a name
    in the frame namespace.
    """


    async def test_switch_eq_var_match_falls_through(self):
        """
        SWITCH_EQ_VAR drops the discriminant on match and falls through
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_VAR", ("miss", "key", 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"key": 10})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_var_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ_VAR retains the discriminant on mismatch
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_VAR", ("next_arm", "first", 0, 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["SWITCH_EQ_VAR", ("else", "second", 0, 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"first": 11, "second": 10})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_switch_eq_var_chain_to_else(self):
        """
        SWITCH_EQ_VAR chain falls to else when no arm matches
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["SWITCH_EQ_VAR", ("arm2", "a", 0, 0)],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["SWITCH_EQ_VAR", ("else", "b", 0, 0)],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"a": 1, "b": 2})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_switch_eq_var_missing_var_raises(self):
        """
        SWITCH_EQ_VAR raises when the named variable is undefined
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["SWITCH_EQ_VAR", ("miss", "nonexistent", 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestSwitchEqFastOpcode(CevalTest):
    """
    Tests for SWITCH_EQ_FAST.

    EXTRA schema (LABEL, FASTVAR, NOARG, NOARG) loads the RHS from a
    fast local slot. Uses Function with FormalsSpec so the frame has
    fast locals.
    """


    async def test_switch_eq_fast_match_falls_through(self):
        """
        SWITCH_EQ_FAST drops the discriminant on match and falls through
        """

        code = build_code([
            ["CONST", 10],
            ["SWITCH_EQ_FAST", ("miss", 0, 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("key",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("key",)))
        result = await fn(10)
        self.assertEqual(result, 42)


    async def test_switch_eq_fast_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ_FAST retains the discriminant on mismatch and jumps
        """

        code = build_code([
            ["CONST", 10],
            ["SWITCH_EQ_FAST", ("next_arm", 0, 0, 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["SWITCH_EQ_FAST", ("else", 1, 0, 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("first", "second"), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("first", "second")))
        result = await fn(11, 10)
        self.assertEqual(result, 99)


    async def test_switch_eq_fast_chain_to_else(self):
        """
        SWITCH_EQ_FAST chain falls to else when no arm matches
        """

        code = build_code([
            ["CONST", 5],
            ["SWITCH_EQ_FAST", ("arm2", 0, 0, 0)],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["SWITCH_EQ_FAST", ("else", 1, 0, 0)],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("a", "b"), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("a", "b")))
        result = await fn(1, 2)
        self.assertEqual(result, 0)


    async def test_switch_eq_fast_int_float_equality(self):
        """
        SWITCH_EQ_FAST uses Py_EQ semantics: 0 == 0.0 matches
        """

        code = build_code([
            ["CONST", 0],
            ["SWITCH_EQ_FAST", ("miss", 0, 0, 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("key",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("key",)))
        result = await fn(0.0)
        self.assertEqual(result, 42)


class TestSwitchEqVarAttrOpcode(CevalTest):
    """
    Tests for SWITCH_EQ_VAR_ATTR.

    EXTRA schema (LABEL, VAR, CONST, NOARG) loads the RHS from
    getattr(frame_ns_deref(var_name), attr_name).
    """


    async def test_switch_eq_var_attr_match_falls_through(self):
        """
        SWITCH_EQ_VAR_ATTR drops the discriminant on match
        """

        class Box(object):
            value = 10
        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_VAR_ATTR", ("miss", "obj", "value", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"obj": Box()})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_switch_eq_var_attr_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ_VAR_ATTR retains the discriminant on mismatch
        """

        class Box(object):
            first = 11
            second = 10
        code, ns = build_expr([
            ["CONST", 10],
            ["SWITCH_EQ_VAR_ATTR", ("next_arm", "obj", "first", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["SWITCH_EQ_VAR_ATTR", ("else", "obj", "second", 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"obj": Box()})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_switch_eq_var_attr_chain_to_else(self):
        """
        SWITCH_EQ_VAR_ATTR chain falls to else when no arm matches
        """

        class Box(object):
            a = 1
            b = 2
        code, ns = build_expr([
            ["CONST", 5],
            ["SWITCH_EQ_VAR_ATTR", ("arm2", "obj", "a", 0)],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["SWITCH_EQ_VAR_ATTR", ("else", "obj", "b", 0)],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"obj": Box()})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_switch_eq_var_attr_missing_attr_raises(self):
        """
        SWITCH_EQ_VAR_ATTR raises AttributeError when attr is missing
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["SWITCH_EQ_VAR_ATTR", ("miss", "obj", "nonexistent", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"obj": 42})
        with self.assertRaises(AttributeError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("nonexistent", str(ctx.exception))


class TestSwitchEqFastAttrOpcode(CevalTest):
    """
    Tests for SWITCH_EQ_FAST_ATTR.

    EXTRA schema (LABEL, FASTVAR, CONST, NOARG) loads the RHS from
    getattr(KosNamespace_FastDeref(ns, slot), attr_name). Uses Function
    with FormalsSpec so the frame has fast locals.
    """


    async def test_switch_eq_fast_attr_match_falls_through(self):
        """
        SWITCH_EQ_FAST_ATTR drops the discriminant on match
        """

        class Box(object):
            value = 10
        code = build_code([
            ["CONST", 10],
            ["SWITCH_EQ_FAST_ATTR", ("miss", 0, "value", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(Box())
        self.assertEqual(result, 42)


    async def test_switch_eq_fast_attr_mismatch_jumps_and_keeps_disc(self):
        """
        SWITCH_EQ_FAST_ATTR retains the discriminant on mismatch
        """

        class Box(object):
            first = 11
            second = 10
        code = build_code([
            ["CONST", 10],
            ["SWITCH_EQ_FAST_ATTR", ("next_arm", 0, "first", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "next_arm"],
            ["SWITCH_EQ_FAST_ATTR", ("else", 0, "second", 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(Box())
        self.assertEqual(result, 99)


    async def test_switch_eq_fast_attr_chain_to_else(self):
        """
        SWITCH_EQ_FAST_ATTR chain falls to else when no arm matches
        """

        class Box(object):
            a = 1
            b = 2
        code = build_code([
            ["CONST", 5],
            ["SWITCH_EQ_FAST_ATTR", ("arm2", 0, "a", 0)],
            ["CONST", 100],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["SWITCH_EQ_FAST_ATTR", ("else", 0, "b", 0)],
            ["CONST", 200],
            ["RETURN", 1],
            ["LABEL", "else"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        result = await fn(Box())
        self.assertEqual(result, 0)


    async def test_switch_eq_fast_attr_missing_attr_raises(self):
        """
        SWITCH_EQ_FAST_ATTR raises AttributeError when attr is missing
        """

        code = build_code([
            ["CONST", 1],
            ["SWITCH_EQ_FAST_ATTR", ("miss", 0, "nonexistent", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("obj",), name="fn")
        base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
        fn = make_callable(code, base_ns, FormalsSpec(("obj",)))
        with self.assertRaises(AttributeError) as ctx:
            await fn(42)
        self.assertIn("nonexistent", str(ctx.exception))


# The end.
