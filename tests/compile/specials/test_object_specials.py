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
Tests for koskari.compile object operator special forms (specials/object.py).

Covers compile/=, compile/!=, compile/<, compile/<=, compile/>, compile/>=,
compile/in, compile/not-in, compile/is, compile/is-not,
compile/isinstance, compile/issubclass,
compile/+, compile/-, compile/*, compile/**, compile/&, compile/|, compile/^,
compile///, compile/@, compile//, compile/%, compile/<<, compile/>>,
compile/divmod,
compile/not, compile/repr, compile/truth, compile/iter, compile/~,
compile/#str, compile/len, compile/type, compile/dir, compile/vars,
compile/aiter.
"""


from koskari.exceptions import SpecialError, SyntaxMatchError
from koskari.lib import OpCode, OpStyle
from koskari.dis import dis

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


def _ops(code):
    return [code.oparg(i) for i in range(code.length())]


def _opcodes(code):
    return [op for op, _ in _ops(code)]


def _find_op(code, opcode):
    for op, arg in _ops(code):
        if op == opcode:
            return arg
    return None


# ---------------------------------------------------------------------------
# Comparison interface
# ---------------------------------------------------------------------------

class TestObjectEq(SpecialsTestCase):

    async def test_eq_true(self):
        expr, mod = self.compile_expr("(compile/== a b)", a=1, b=1)
        self.assertTrue(await expr())


    async def test_eq_false(self):
        expr, mod = self.compile_expr("(compile/== a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_eq_emits_opcode(self):
        code = self.compile_to_code("(compile/== a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_EQ, _opcodes(code))


    async def test_eq_chain_true(self):
        expr, mod = self.compile_expr("(compile/== a b c d)", a=5, b=5, c=5, d=5)
        self.assertTrue(await expr())


    async def test_eq_chain_false(self):
        expr, mod = self.compile_expr("(compile/== a b c d)", a=5, b=5, c=7, d=5)
        self.assertFalse(await expr())


    async def test_eq_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/== a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_EQ)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_eq_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/== a)")


class TestObjectNe(SpecialsTestCase):

    async def test_ne_true(self):
        expr, mod = self.compile_expr("(compile/!= a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_ne_false(self):
        expr, mod = self.compile_expr("(compile/!= a b)", a=1, b=1)
        self.assertFalse(await expr())


    async def test_ne_emits_opcode(self):
        code = self.compile_to_code("(compile/!= a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_NE, _opcodes(code))


    async def test_ne_chain_true(self):
        expr, mod = self.compile_expr("(compile/!= a b c d)", a=1, b=2, c=3, d=4)
        self.assertTrue(await expr())


    async def test_ne_chain_false(self):
        expr, mod = self.compile_expr("(compile/!= a b c d)", a=1, b=2, c=2, d=4)
        self.assertFalse(await expr())


    async def test_ne_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/!= a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_NE)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_ne_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/!= a)")


class TestObjectLt(SpecialsTestCase):

    async def test_lt_true(self):
        expr, mod = self.compile_expr("(compile/< a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_lt_false(self):
        expr, mod = self.compile_expr("(compile/< a b)", a=2, b=1)
        self.assertFalse(await expr())


    async def test_lt_emits_opcode(self):
        code = self.compile_to_code("(compile/< a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_LT, _opcodes(code))


    async def test_lt_chain_true(self):
        expr, mod = self.compile_expr("(compile/< a b c d)", a=1, b=2, c=3, d=4)
        self.assertTrue(await expr())


    async def test_lt_chain_false(self):
        expr, mod = self.compile_expr("(compile/< a b c d)", a=1, b=3, c=2, d=4)
        self.assertFalse(await expr())


    async def test_lt_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/< a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_LT)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_lt_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/< a)")


    async def test_lt_star_arg_rewrites_to_call(self):
        code = self.compile_to_code("(compile/< a *: xs)")
        self.assertIsNone(_find_op(code, OpCode.KOS_OP_OBJECT_LT))
        self.assertTrue(self.call_ops(code))


class TestObjectLe(SpecialsTestCase):

    async def test_le_equal(self):
        expr, mod = self.compile_expr("(compile/<= a b)", a=2, b=2)
        self.assertTrue(await expr())


    async def test_le_less(self):
        expr, mod = self.compile_expr("(compile/<= a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_le_greater(self):
        expr, mod = self.compile_expr("(compile/<= a b)", a=3, b=2)
        self.assertFalse(await expr())


    async def test_le_emits_opcode(self):
        code = self.compile_to_code("(compile/<= a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_LE, _opcodes(code))


    async def test_le_chain_true(self):
        expr, mod = self.compile_expr("(compile/<= a b c d)", a=1, b=2, c=2, d=4)
        self.assertTrue(await expr())


    async def test_le_chain_false(self):
        expr, mod = self.compile_expr("(compile/<= a b c d)", a=1, b=2, c=1, d=4)
        self.assertFalse(await expr())


    async def test_le_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/<= a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_LE)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_le_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/<= a)")


class TestObjectGt(SpecialsTestCase):

    async def test_gt_true(self):
        expr, mod = self.compile_expr("(compile/> a b)", a=3, b=2)
        self.assertTrue(await expr())


    async def test_gt_false(self):
        expr, mod = self.compile_expr("(compile/> a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_gt_emits_opcode(self):
        code = self.compile_to_code("(compile/> a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_GT, _opcodes(code))


    async def test_gt_chain_true(self):
        expr, mod = self.compile_expr("(compile/> a b c d)", a=4, b=3, c=2, d=1)
        self.assertTrue(await expr())


    async def test_gt_chain_false(self):
        expr, mod = self.compile_expr("(compile/> a b c d)", a=4, b=2, c=3, d=1)
        self.assertFalse(await expr())


    async def test_gt_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/> a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_GT)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_gt_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/> a)")


class TestObjectGe(SpecialsTestCase):

    async def test_ge_equal(self):
        expr, mod = self.compile_expr("(compile/>= a b)", a=2, b=2)
        self.assertTrue(await expr())


    async def test_ge_greater(self):
        expr, mod = self.compile_expr("(compile/>= a b)", a=3, b=2)
        self.assertTrue(await expr())


    async def test_ge_less(self):
        expr, mod = self.compile_expr("(compile/>= a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_ge_emits_opcode(self):
        code = self.compile_to_code("(compile/>= a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_GE, _opcodes(code))


    async def test_ge_chain_true(self):
        expr, mod = self.compile_expr("(compile/>= a b c d)", a=4, b=3, c=3, d=1)
        self.assertTrue(await expr())


    async def test_ge_chain_false(self):
        expr, mod = self.compile_expr("(compile/>= a b c d)", a=4, b=3, c=4, d=1)
        self.assertFalse(await expr())


    async def test_ge_emits_count_for_chain(self):
        code = self.compile_to_code("(compile/>= a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_GE)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_ge_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/>= a)")


# ---------------------------------------------------------------------------
# Predicate interface
# ---------------------------------------------------------------------------

class TestObjectIn(SpecialsTestCase):

    async def test_in_true(self):
        expr, mod = self.compile_expr("(compile/in a b)", a=2, b=[1, 2, 3])
        self.assertTrue(await expr())


    async def test_in_false(self):
        expr, mod = self.compile_expr("(compile/in a b)", a=9, b=[1, 2, 3])
        self.assertFalse(await expr())


    async def test_in_emits_opcode(self):
        code = self.compile_to_code("(compile/in a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_IN, _opcodes(code))


    async def test_in_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/in a)")


class TestObjectNotIn(SpecialsTestCase):

    async def test_not_in_true(self):
        expr, mod = self.compile_expr("(compile/not-in a b)", a=9, b=[1, 2, 3])
        self.assertTrue(await expr())


    async def test_not_in_false(self):
        expr, mod = self.compile_expr("(compile/not-in a b)", a=2, b=[1, 2, 3])
        self.assertFalse(await expr())


    async def test_not_in_emits_opcode(self):
        code = self.compile_to_code("(compile/not-in a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_NOT_IN, _opcodes(code))


class TestObjectIs(SpecialsTestCase):

    async def test_is_true(self):
        sentinel = object()
        expr, mod = self.compile_expr("(compile/is a b)", a=sentinel, b=sentinel)
        self.assertTrue(await expr())


    async def test_is_false(self):
        expr, mod = self.compile_expr("(compile/is a b)", a=object(), b=object())
        self.assertFalse(await expr())


    async def test_is_emits_opcode(self):
        code = self.compile_to_code("(compile/is a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_IS, _opcodes(code))


class TestObjectIsNot(SpecialsTestCase):

    async def test_is_not_true(self):
        expr, mod = self.compile_expr("(compile/is-not a b)", a=object(), b=object())
        self.assertTrue(await expr())


    async def test_is_not_false(self):
        sentinel = object()
        expr, mod = self.compile_expr("(compile/is-not a b)", a=sentinel, b=sentinel)
        self.assertFalse(await expr())


    async def test_is_not_emits_opcode(self):
        code = self.compile_to_code("(compile/is-not a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_IS_NOT, _opcodes(code))


class TestObjectIsInstance(SpecialsTestCase):

    async def test_isinstance_true(self):
        expr, mod = self.compile_expr(
            "(compile/isinstance a b)", a=42, b=int)
        self.assertTrue(await expr())


    async def test_isinstance_false(self):
        expr, mod = self.compile_expr(
            "(compile/isinstance a b)", a="hello", b=int)
        self.assertFalse(await expr())


    async def test_isinstance_single_type_emits_arg_one(self):
        # single type: arg=1 uses the pop-two-separately C path
        code = self.compile_to_code("(compile/isinstance a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_IS_INSTANCE)
        self.assertIsNotNone(arg)
        self.assertEqual(1, arg)


    async def test_isinstance_multi_type_emits_arg_n(self):
        # two types: arg=2 uses the PopTail(2) C path
        code = self.compile_to_code("(compile/isinstance a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_IS_INSTANCE)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_isinstance_requires_at_least_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/isinstance a)")


    async def test_isinstance_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/isinstance)")


class TestObjectIsSubclass(SpecialsTestCase):

    async def test_issubclass_true(self):
        expr, mod = self.compile_expr(
            "(compile/issubclass a b)", a=bool, b=int)
        self.assertTrue(await expr())


    async def test_issubclass_false(self):
        expr, mod = self.compile_expr(
            "(compile/issubclass a b)", a=str, b=int)
        self.assertFalse(await expr())


    async def test_issubclass_single_type_emits_arg_one(self):
        code = self.compile_to_code("(compile/issubclass a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_IS_SUBCLASS)
        self.assertIsNotNone(arg)
        self.assertEqual(1, arg)


    async def test_issubclass_multi_type_emits_arg_n(self):
        code = self.compile_to_code("(compile/issubclass a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_IS_SUBCLASS)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


# ---------------------------------------------------------------------------
# Numeric interface — variadic COUNT opcodes
# ---------------------------------------------------------------------------

class TestObjectAdd(SpecialsTestCase):

    async def test_add_two_vars(self):
        expr, mod = self.compile_expr("(compile/+ a b)", a=10, b=32)
        self.assertEqual(42, await expr())


    async def test_add_three_vars(self):
        expr, mod = self.compile_expr("(compile/+ a b c)", a=1, b=2, c=3)
        self.assertEqual(6, await expr())


    async def test_add_emits_count_opcode(self):
        code = self.compile_to_code("(compile/+ a b c)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_ADD)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_add_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/+)")


    async def test_add_one_arg(self):
        expr, mod = self.compile_expr("(compile/+ a)", a=7)
        self.assertEqual(7, await expr())


    async def test_add_const_fold_two_literals(self):
        # (+ 1 2) folds to a single CONST 3; no op needed
        code = self.compile_to_code("(compile/+ 1 2)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_ADD)

        dis(code)

        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(3, consts)
        self.assertEqual(1, consts.count(3))


    async def test_add_const_fold_prefix_only(self):
        # (+ 1 2 a) → CONST 3, VAR a, OBJECT_ADD 2  (folded prefix, not trailing var)
        code = self.compile_to_code("(compile/+ 1 2 a)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_ADD)

        dis(code)

        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(3, consts)


    async def test_add_no_fold_after_var(self):
        # (+ a 1 2) must NOT fold 1 and 2 — emit all three args
        code = self.compile_to_code("(compile/+ a 1 2)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_ADD)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_add_star_arg_rewrites_to_call_not_object_add(self):
        """
        *: in the argument list cannot lower to OBJECT_ADD; specials fall back
        to compile/call (call opcodes, not KOS_OP_OBJECT_ADD).
        """

        code = self.compile_to_code("(compile/+ 1 *: xs)")
        self.assertIsNone(_find_op(code, OpCode.KOS_OP_OBJECT_ADD))
        self.assertTrue(self.call_ops(code))


    async def test_add_const_fold_star_arg(self):
        code = self.compile_to_code("(compile/+ 1 11 *: xs)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_ADD)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(12, consts)


class TestObjectSubtract(SpecialsTestCase):

    async def test_subtract_two_vars(self):
        expr, mod = self.compile_expr("(compile/- a b)", a=10, b=3)
        self.assertEqual(7, await expr())


    async def test_subtract_emits_count_opcode(self):
        code = self.compile_to_code("(compile/- a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_SUBTRACT)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_subtract_const_fold(self):
        code = self.compile_to_code("(compile/- 10 3)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_SUBTRACT)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(7, consts)


class TestObjectMultiply(SpecialsTestCase):

    async def test_multiply_two_vars(self):
        expr, mod = self.compile_expr("(compile/* a b)", a=6, b=7)
        self.assertEqual(42, await expr())


    async def test_multiply_three_args(self):
        expr, mod = self.compile_expr("(compile/* a b c)", a=2, b=3, c=4)
        self.assertEqual(24, await expr())


    async def test_multiply_emits_count_opcode(self):
        code = self.compile_to_code("(compile/* a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_MULTIPLY)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_multiply_const_fold(self):
        code = self.compile_to_code("(compile/* 3 4)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_MULTIPLY)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(12, consts)


class TestObjectPower(SpecialsTestCase):

    async def test_power_two_args(self):
        expr, mod = self.compile_expr("(compile/** a b)", a=2, b=10)
        self.assertEqual(1024, await expr())


    async def test_power_emits_count_opcode(self):
        code = self.compile_to_code("(compile/** a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_POWER)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_power_no_const_fold(self):
        # ** has py_fold=None; constants are never folded
        code = self.compile_to_code("(compile/** 2 10)")
        self.assertIn(OpCode.KOS_OP_OBJECT_POWER, _opcodes(code))


class TestObjectBitAnd(SpecialsTestCase):

    async def test_bit_and_vars(self):
        expr, mod = self.compile_expr("(compile/& a b)", a=0b1100, b=0b1010)
        self.assertEqual(0b1000, await expr())


    async def test_bit_and_emits_count_opcode(self):
        code = self.compile_to_code("(compile/& a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_AND)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_and_const_fold(self):
        code = self.compile_to_code("(compile/& 12 10)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_AND)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(8, consts)


class TestObjectBitOr(SpecialsTestCase):

    async def test_bit_or_vars(self):
        expr, mod = self.compile_expr("(compile/| a b)", a=0b1100, b=0b0011)
        self.assertEqual(0b1111, await expr())


    async def test_bit_or_emits_count_opcode(self):
        code = self.compile_to_code("(compile/| a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_OR)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_or_const_fold(self):
        code = self.compile_to_code("(compile/| 12 3)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_OR)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(15, consts)


class TestObjectBitXor(SpecialsTestCase):

    async def test_bit_xor_vars(self):
        expr, mod = self.compile_expr("(compile/^ a b)", a=0b1100, b=0b1010)
        self.assertEqual(0b0110, await expr())


    async def test_bit_xor_emits_count_opcode(self):
        code = self.compile_to_code("(compile/^ a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_XOR)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_xor_const_fold(self):
        code = self.compile_to_code("(compile/^ 12 10)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_BIT_XOR)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(6, consts)


class TestObjectFloorDivide(SpecialsTestCase):

    async def test_floor_divide_vars(self):
        expr, mod = self.compile_expr("(compile/// a b)", a=7, b=2)
        self.assertEqual(3, await expr())


    async def test_floor_divide_emits_count_opcode(self):
        code = self.compile_to_code("(compile/// a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FLOOR_DIVIDE)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_floor_divide_const_fold(self):
        code = self.compile_to_code("(compile/// 7 2)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FLOOR_DIVIDE)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(3, consts)


class TestObjectMatrixMultiply(SpecialsTestCase):

    async def test_matrix_multiply_emits_count_opcode(self):
        code = self.compile_to_code("(compile/@ a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_MATRIX_MULTIPLY)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


# ---------------------------------------------------------------------------
# Numeric interface — NOARG binary opcodes
# ---------------------------------------------------------------------------

class TestObjectTrueDivide(SpecialsTestCase):

    async def test_true_divide_vars(self):
        expr, mod = self.compile_expr("(compile// a b)", a=7, b=2)
        self.assertEqual(3.5, await expr())


    async def test_true_divide_emits_count_opcode(self):
        code = self.compile_to_code("(compile// a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_TRUE_DIVIDE)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_true_divide_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile//)")


    async def test_true_divide_const_fold(self):
        code = self.compile_to_code("(compile// 7.0 2.0)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_TRUE_DIVIDE)
        self.assertIsNone(arg)
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn(3.5, consts)


class TestObjectModulo(SpecialsTestCase):

    async def test_modulo_vars(self):
        expr, mod = self.compile_expr("(compile/% a b)", a=10, b=3)
        self.assertEqual(1, await expr())


    async def test_modulo_emits_opcode_with_arg_two(self):
        # C handler requires opa==2
        code = self.compile_to_code("(compile/% a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_MODULO)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_modulo_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/% a)")


class TestObjectBitLeft(SpecialsTestCase):

    async def test_bit_left_vars(self):
        expr, mod = self.compile_expr("(compile/<< a b)", a=1, b=3)
        self.assertEqual(8, await expr())


    async def test_bit_left_emits_opcode(self):
        code = self.compile_to_code("(compile/<< a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_BIT_LEFT, _opcodes(code))


    async def test_bit_left_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/<< a)")


class TestObjectBitRight(SpecialsTestCase):

    async def test_bit_right_vars(self):
        expr, mod = self.compile_expr("(compile/>> a b)", a=16, b=2)
        self.assertEqual(4, await expr())


    async def test_bit_right_emits_opcode(self):
        code = self.compile_to_code("(compile/>> a b)")
        self.assertIn(OpCode.KOS_OP_OBJECT_BIT_RIGHT, _opcodes(code))


    async def test_bit_right_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/>> a)")


class TestObjectDivmod(SpecialsTestCase):

    async def test_divmod_vars(self):
        expr, mod = self.compile_expr("(compile/divmod a b)", a=10, b=3)
        self.assertEqual((3, 1), await expr())


    async def test_divmod_emits_opcode_with_arg_two(self):
        # C handler requires opa==2
        code = self.compile_to_code("(compile/divmod a b)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_DIVMOD)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_divmod_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/divmod a)")


# ---------------------------------------------------------------------------
# General object interface — NOARG unary opcodes
# ---------------------------------------------------------------------------

class TestObjectNot(SpecialsTestCase):

    async def test_not_true_becomes_false(self):
        expr, mod = self.compile_expr("(compile/not a)", a=True)
        self.assertFalse(await expr())


    async def test_not_false_becomes_true(self):
        expr, mod = self.compile_expr("(compile/not a)", a=False)
        self.assertTrue(await expr())


    async def test_not_emits_opcode(self):
        code = self.compile_to_code("(compile/not a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_NOT, _opcodes(code))


    async def test_not_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/not)")


    async def test_not_rejects_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/not a b)")


class TestObjectRepr(SpecialsTestCase):

    async def test_repr_result(self):
        expr, mod = self.compile_expr("(compile/repr a)", a=42)
        self.assertEqual("42", await expr())


    async def test_repr_emits_opcode(self):
        code = self.compile_to_code("(compile/repr a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_REPR, _opcodes(code))


    async def test_repr_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/repr)")


class TestObjectTruth(SpecialsTestCase):

    async def test_truth_nonempty(self):
        expr, mod = self.compile_expr("(compile/truth a)", a=[1])
        self.assertTrue(await expr())


    async def test_truth_empty(self):
        expr, mod = self.compile_expr("(compile/truth a)", a=[])
        self.assertFalse(await expr())


    async def test_truth_emits_opcode(self):
        code = self.compile_to_code("(compile/truth a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_TRUTH, _opcodes(code))


    async def test_truth_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/truth)")


class TestObjectIter(SpecialsTestCase):

    async def test_iter_produces_iterator(self):
        expr, mod = self.compile_expr("(compile/iter a)", a=[1, 2, 3])
        result = await expr()
        self.assertEqual([1, 2, 3], list(result))


    async def test_iter_emits_opcode(self):
        code = self.compile_to_code("(compile/iter a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_ITER, _opcodes(code))


    async def test_iter_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/iter)")


class TestObjectBitInvert(SpecialsTestCase):

    async def test_bit_invert_value(self):
        expr, mod = self.compile_expr("(compile/~ a)", a=0)
        self.assertEqual(-1, await expr())


    async def test_bit_invert_emits_opcode(self):
        code = self.compile_to_code("(compile/~ a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_BIT_INVERT, _opcodes(code))


    async def test_bit_invert_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/~)")


class TestObjectStr(SpecialsTestCase):

    async def test_str_result(self):
        expr, mod = self.compile_expr("(compile/#str a)", a=99)
        self.assertEqual("99", await expr())


    async def test_str_empty(self):
        expr, mod = self.compile_expr("(compile/#str)")
        self.assertEqual("", await expr())


    async def test_str_emits_count_opcode(self):
        code = self.compile_to_code("(compile/#str a)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_STR)
        self.assertIsNotNone(arg)
        self.assertEqual(1, arg)


class TestObjectLen(SpecialsTestCase):

    async def test_len_list(self):
        expr, mod = self.compile_expr("(compile/len a)", a=[1, 2, 3])
        self.assertEqual(3, await expr())


    async def test_len_string(self):
        expr, mod = self.compile_expr("(compile/len a)", a="hello")
        self.assertEqual(5, await expr())


    async def test_len_emits_opcode(self):
        code = self.compile_to_code("(compile/len a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_LEN, _opcodes(code))


    async def test_len_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/len)")


class TestObjectType(SpecialsTestCase):

    async def test_type_of_int(self):
        expr, mod = self.compile_expr("(compile/type a)", a=42)
        self.assertIs(int, await expr())


    async def test_type_emits_opcode(self):
        code = self.compile_to_code("(compile/type a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_TYPE, _opcodes(code))


    async def test_type_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/type)")


class TestObjectDir(SpecialsTestCase):

    async def test_dir_returns_list(self):
        expr, mod = self.compile_expr("(compile/dir a)", a=42)
        result = await expr()
        self.assertIsInstance(result, list)
        self.assertIn("__class__", result)


    async def test_dir_emits_opcode(self):
        code = self.compile_to_code("(compile/dir a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_DIR, _opcodes(code))


    async def test_dir_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/dir)")


class TestObjectVars(SpecialsTestCase):

    async def test_vars_returns_dict(self):
        class Obj:
            pass
        o = Obj()
        o.x = 1
        o.y = 2
        expr, mod = self.compile_expr("(compile/vars a)", a=o)
        result = await expr()
        self.assertIsInstance(result, dict)
        self.assertEqual(1, result["x"])
        self.assertEqual(2, result["y"])


    async def test_vars_emits_opcode(self):
        code = self.compile_to_code("(compile/vars a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_VARS, _opcodes(code))


    async def test_vars_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/vars)")


class TestObjectAiter(SpecialsTestCase):

    async def test_aiter_emits_opcode(self):
        code = self.compile_to_code("(compile/aiter a)")
        self.assertIn(OpCode.KOS_OP_OBJECT_AITER, _opcodes(code))


    async def test_aiter_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/aiter)")


    async def test_aiter_rejects_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(compile/aiter a b)")


class TestObjectFormat(SpecialsTestCase):

    async def test_format_no_rules_emits_object_format_arg_one(self):
        code = self.compile_to_code("(compile/format a)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FORMAT)
        self.assertIsNotNone(arg)
        self.assertEqual(1, arg)


    async def test_format_const_rules_emits_const_opcode(self):
        code = self.compile_to_code('(compile/format a "04d")')
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FORMAT_CONST)
        self.assertIsNotNone(arg)
        self.assertEqual("04d", code.constant(arg))


    async def test_format_quoted_symbol_rules_emits_const_opcode(self):
        code = self.compile_to_code("(compile/format a (compile/quote z))")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FORMAT_CONST)
        self.assertIsNotNone(arg)
        self.assertEqual("z", code.constant(arg))


    async def test_format_dynamic_rules_emits_object_format_arg_two(self):
        code = self.compile_to_code("(compile/format a r)")
        arg = _find_op(code, OpCode.KOS_OP_OBJECT_FORMAT)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_format_const_fold_without_rules(self):
        code = self.compile_to_code("(compile/format 99)")
        self.assertNotIn(OpCode.KOS_OP_OBJECT_FORMAT, _opcodes(code))
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn("99", consts)


    async def test_format_const_fold_with_const_rules(self):
        code = self.compile_to_code('(compile/format 7 "04d")')
        self.assertNotIn(OpCode.KOS_OP_OBJECT_FORMAT, _opcodes(code))
        self.assertNotIn(OpCode.KOS_OP_OBJECT_FORMAT_CONST, _opcodes(code))
        consts = [
            code.constant(arg)
            for op, arg in _ops(code)
            if op == OpCode.KOS_OP_CONST
        ]
        self.assertIn("0007", consts)


    async def test_format_requires_at_least_one_arg(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(compile/format)")


    async def test_format_rejects_three_args(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(compile/format a b c)")


    async def test_object_str_opcode_style_is_count(self):
        self.assertEqual(
            OpStyle.KOSTYLE_COUNT,
            OpCode.KOS_OP_OBJECT_STR.style)


# The end.
