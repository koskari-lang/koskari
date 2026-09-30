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

Covers =, !=, <, <=, >, >=,
in, not-in, is, is-not,
instance?, subclass?,
+, -, *, **, &, |, ^,
//, @, /, %, <<, >>,
divmod,
not, repr, truth, iter, ~,
#str, len, type, dir, vars.
"""


import pytest

from koskari.exceptions import SpecialError, SyntaxMatchError
from koskari.lib import Compare, OpCode, Operand, OpStyle
from tests import SlowAIter
from tests.dis.dis import dis

from . import SpecialsTestCase


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


def _consts(code):
    """
    The constants the code loads, from CONST rows and from the fused
    RETURN_CONST a constant in tail position becomes
    """

    return [
        code.constant(arg)
        for op, arg in _ops(code)
        if op in (OpCode.CONST, OpCode.RETURN_CONST)
    ]


_CONST_OPERAND_ROWS = frozenset((
    OpCode.OBJECT_OPERAND_CONST, OpCode.VAR_OPERAND_CONST,
    OpCode.FAST_OPERAND_CONST))


def _find_operand(code, operand):
    """
    Return the operand count of the first operator instruction whose
    op-type is `operand`, or None if none is present. The arithmetic
    operators were consolidated into OBJECT_OPERAND, whose arg packs the
    operator selector in the high byte and the count in the low byte; a
    binary site whose right operand is a constant fuses into a row
    carrying the bare selector first, and counts as two.
    """

    for op, arg in _ops(code):
        if op == OpCode.OBJECT_OPERAND and (arg >> 8) == int(operand):
            return arg & 0xff
        if op in _CONST_OPERAND_ROWS \
                and code.extra_tuple(arg)[0] == int(operand):
            return 2
    return None


def _find_compare(code, sel):
    """
    Return the operand count of the first OBJECT_COMPARE instruction whose
    selector is `sel`, or None if none is present. The comparison operators
    were consolidated into OBJECT_COMPARE, whose CMPCOUNT arg packs the
    Compare selector in the high byte and the count in the low byte.
    """

    for op, arg in _ops(code):
        if op == OpCode.OBJECT_COMPARE and (arg >> 8) == int(sel):
            return arg & 0xff
    return None


# ---------------------------------------------------------------------------
# Comparison interface
# ---------------------------------------------------------------------------

class TestObjectEq(SpecialsTestCase):

    async def test_eq_true(self):
        expr, mod = self.compile_expr("(== a b)", a=1, b=1)
        self.assertTrue(await expr())


    async def test_eq_false(self):
        expr, mod = self.compile_expr("(== a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_eq_emits_opcode(self):
        code = self.compile_to_code("(== a b)")
        self.assertEqual(2, _find_compare(code, Compare.EQ))


    async def test_eq_chain_true(self):
        expr, mod = self.compile_expr("(== a b c d)", a=5, b=5, c=5, d=5)
        self.assertTrue(await expr())


    async def test_eq_chain_false(self):
        expr, mod = self.compile_expr("(== a b c d)", a=5, b=5, c=7, d=5)
        self.assertFalse(await expr())


    async def test_eq_emits_count_for_chain(self):
        code = self.compile_to_code("(== a b c)")
        count = _find_compare(code, Compare.EQ)
        self.assertEqual(3, count)


    async def test_eq_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(== a)")


class TestObjectNe(SpecialsTestCase):

    async def test_ne_true(self):
        expr, mod = self.compile_expr("(!= a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_ne_false(self):
        expr, mod = self.compile_expr("(!= a b)", a=1, b=1)
        self.assertFalse(await expr())


    async def test_ne_emits_opcode(self):
        code = self.compile_to_code("(!= a b)")
        self.assertEqual(2, _find_compare(code, Compare.NE))


    async def test_ne_chain_true(self):
        expr, mod = self.compile_expr("(!= a b c d)", a=1, b=2, c=3, d=4)
        self.assertTrue(await expr())


    async def test_ne_chain_false(self):
        expr, mod = self.compile_expr("(!= a b c d)", a=1, b=2, c=2, d=4)
        self.assertFalse(await expr())


    async def test_ne_emits_count_for_chain(self):
        code = self.compile_to_code("(!= a b c)")
        count = _find_compare(code, Compare.NE)
        self.assertEqual(3, count)


    async def test_ne_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(!= a)")


class TestObjectLt(SpecialsTestCase):

    async def test_lt_true(self):
        expr, mod = self.compile_expr("(< a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_lt_false(self):
        expr, mod = self.compile_expr("(< a b)", a=2, b=1)
        self.assertFalse(await expr())


    async def test_lt_emits_opcode(self):
        code = self.compile_to_code("(< a b)")
        self.assertEqual(2, _find_compare(code, Compare.LT))


    async def test_lt_chain_true(self):
        expr, mod = self.compile_expr("(< a b c d)", a=1, b=2, c=3, d=4)
        self.assertTrue(await expr())


    async def test_lt_chain_false(self):
        expr, mod = self.compile_expr("(< a b c d)", a=1, b=3, c=2, d=4)
        self.assertFalse(await expr())


    async def test_lt_emits_count_for_chain(self):
        code = self.compile_to_code("(< a b c)")
        count = _find_compare(code, Compare.LT)
        self.assertEqual(3, count)


    async def test_lt_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(< a)")


    async def test_lt_star_arg_rewrites_to_call(self):
        code = self.compile_to_code("(< a *: xs)")
        self.assertIsNone(_find_compare(code, Compare.LT))
        self.assertTrue(self.call_ops(code))


class TestObjectLe(SpecialsTestCase):

    async def test_le_equal(self):
        expr, mod = self.compile_expr("(<= a b)", a=2, b=2)
        self.assertTrue(await expr())


    async def test_le_less(self):
        expr, mod = self.compile_expr("(<= a b)", a=1, b=2)
        self.assertTrue(await expr())


    async def test_le_greater(self):
        expr, mod = self.compile_expr("(<= a b)", a=3, b=2)
        self.assertFalse(await expr())


    async def test_le_emits_opcode(self):
        code = self.compile_to_code("(<= a b)")
        self.assertEqual(2, _find_compare(code, Compare.LE))


    async def test_le_chain_true(self):
        expr, mod = self.compile_expr("(<= a b c d)", a=1, b=2, c=2, d=4)
        self.assertTrue(await expr())


    async def test_le_chain_false(self):
        expr, mod = self.compile_expr("(<= a b c d)", a=1, b=2, c=1, d=4)
        self.assertFalse(await expr())


    async def test_le_emits_count_for_chain(self):
        code = self.compile_to_code("(<= a b c)")
        count = _find_compare(code, Compare.LE)
        self.assertEqual(3, count)


    async def test_le_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(<= a)")


class TestObjectGt(SpecialsTestCase):

    async def test_gt_true(self):
        expr, mod = self.compile_expr("(> a b)", a=3, b=2)
        self.assertTrue(await expr())


    async def test_gt_false(self):
        expr, mod = self.compile_expr("(> a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_gt_emits_opcode(self):
        code = self.compile_to_code("(> a b)")
        self.assertEqual(2, _find_compare(code, Compare.GT))


    async def test_gt_chain_true(self):
        expr, mod = self.compile_expr("(> a b c d)", a=4, b=3, c=2, d=1)
        self.assertTrue(await expr())


    async def test_gt_chain_false(self):
        expr, mod = self.compile_expr("(> a b c d)", a=4, b=2, c=3, d=1)
        self.assertFalse(await expr())


    async def test_gt_emits_count_for_chain(self):
        code = self.compile_to_code("(> a b c)")
        count = _find_compare(code, Compare.GT)
        self.assertEqual(3, count)


    async def test_gt_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(> a)")


class TestObjectGe(SpecialsTestCase):

    async def test_ge_equal(self):
        expr, mod = self.compile_expr("(>= a b)", a=2, b=2)
        self.assertTrue(await expr())


    async def test_ge_greater(self):
        expr, mod = self.compile_expr("(>= a b)", a=3, b=2)
        self.assertTrue(await expr())


    async def test_ge_less(self):
        expr, mod = self.compile_expr("(>= a b)", a=1, b=2)
        self.assertFalse(await expr())


    async def test_ge_emits_opcode(self):
        code = self.compile_to_code("(>= a b)")
        self.assertEqual(2, _find_compare(code, Compare.GE))


    async def test_ge_chain_true(self):
        expr, mod = self.compile_expr("(>= a b c d)", a=4, b=3, c=3, d=1)
        self.assertTrue(await expr())


    async def test_ge_chain_false(self):
        expr, mod = self.compile_expr("(>= a b c d)", a=4, b=3, c=4, d=1)
        self.assertFalse(await expr())


    async def test_ge_emits_count_for_chain(self):
        code = self.compile_to_code("(>= a b c)")
        count = _find_compare(code, Compare.GE)
        self.assertEqual(3, count)


    async def test_ge_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(>= a)")


# ---------------------------------------------------------------------------
# Predicate interface
# ---------------------------------------------------------------------------

class TestObjectIn(SpecialsTestCase):

    async def test_in_true(self):
        expr, mod = self.compile_expr("(in a b)", a=2, b=[1, 2, 3])
        self.assertTrue(await expr())


    async def test_in_false(self):
        expr, mod = self.compile_expr("(in a b)", a=9, b=[1, 2, 3])
        self.assertFalse(await expr())


    async def test_in_emits_opcode(self):
        code = self.compile_to_code("(in a b)")
        self.assertEqual(2, _find_compare(code, Compare.IN))


    async def test_in_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(in a)")


class TestObjectNotIn(SpecialsTestCase):

    async def test_not_in_true(self):
        expr, mod = self.compile_expr("(not-in a b)", a=9, b=[1, 2, 3])
        self.assertTrue(await expr())


    async def test_not_in_false(self):
        expr, mod = self.compile_expr("(not-in a b)", a=2, b=[1, 2, 3])
        self.assertFalse(await expr())


    async def test_not_in_emits_opcode(self):
        code = self.compile_to_code("(not-in a b)")
        self.assertEqual(2, _find_compare(code, Compare.NOT_IN))


class TestObjectIs(SpecialsTestCase):

    async def test_is_true(self):
        sentinel = object()
        expr, mod = self.compile_expr("(is a b)", a=sentinel, b=sentinel)
        self.assertTrue(await expr())


    async def test_is_false(self):
        expr, mod = self.compile_expr("(is a b)", a=object(), b=object())
        self.assertFalse(await expr())


    async def test_is_emits_opcode(self):
        code = self.compile_to_code("(is a b)")
        self.assertEqual(2, _find_compare(code, Compare.IS))


class TestObjectIsNot(SpecialsTestCase):

    async def test_is_not_true(self):
        expr, mod = self.compile_expr("(is-not a b)", a=object(), b=object())
        self.assertTrue(await expr())


    async def test_is_not_false(self):
        sentinel = object()
        expr, mod = self.compile_expr("(is-not a b)", a=sentinel, b=sentinel)
        self.assertFalse(await expr())


    async def test_is_not_emits_opcode(self):
        code = self.compile_to_code("(is-not a b)")
        self.assertEqual(2, _find_compare(code, Compare.IS_NOT))


class TestObjectIsInstance(SpecialsTestCase):

    async def test_isinstance_true(self):
        expr, mod = self.compile_expr(
            "(instance? a b)", a=42, b=int)
        self.assertTrue(await expr())


    async def test_isinstance_false(self):
        expr, mod = self.compile_expr(
            "(instance? a b)", a="hello", b=int)
        self.assertFalse(await expr())


    async def test_isinstance_single_type_emits_arg_one(self):
        # single type: count 2 (value + one type)
        code = self.compile_to_code("(instance? a b)")
        count = _find_compare(code, Compare.IS_INSTANCE)
        self.assertEqual(2, count)


    async def test_isinstance_rejects_a_third_argument(self):
        """
        instance? has the runtime copy's signature: a value and one
        type, several types as a tuple
        """

        with self.assertRaises(SpecialError):
            self.compile_to_code("(instance? a b c)")


    async def test_isinstance_tuple_of_types_as_value(self):
        expr, mod = self.compile_expr(
            "(instance? a #(b c))", a="hello", b=int, c=str)
        self.assertTrue(await expr())


    async def test_isinstance_tuple_of_types_as_condition(self):
        expr, mod = self.compile_expr(
            "(cond ((instance? a #(b c)) 1) (else: 0))", a="hello", b=int, c=str)
        self.assertEqual(await expr(), 1)
        expr, mod = self.compile_expr(
            "(cond ((instance? a #(b c)) 1) (else: 0))", a=1.5, b=int, c=str)
        self.assertEqual(await expr(), 0)


    async def test_isinstance_requires_at_least_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(instance? a)")


    async def test_isinstance_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(instance?)")


class TestObjectIsSubclass(SpecialsTestCase):

    async def test_issubclass_true(self):
        expr, mod = self.compile_expr(
            "(subclass? a b)", a=bool, b=int)
        self.assertTrue(await expr())


    async def test_issubclass_false(self):
        expr, mod = self.compile_expr(
            "(subclass? a b)", a=str, b=int)
        self.assertFalse(await expr())


    async def test_issubclass_single_type_emits_arg_one(self):
        code = self.compile_to_code("(subclass? a b)")
        count = _find_compare(code, Compare.IS_SUBCLASS)
        self.assertEqual(2, count)


    async def test_issubclass_rejects_a_third_argument(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(subclass? a b c)")


    async def test_issubclass_tuple_of_bases_as_condition(self):
        expr, mod = self.compile_expr(
            "(cond ((subclass? a #(b c)) 1) (else: 0))", a=bool, b=int, c=str)
        self.assertEqual(await expr(), 1)
        expr, mod = self.compile_expr(
            "(cond ((subclass? a #(b c)) 1) (else: 0))", a=float, b=int, c=str)
        self.assertEqual(await expr(), 0)


# ---------------------------------------------------------------------------
# Numeric interface — variadic COUNT opcodes
# ---------------------------------------------------------------------------

class TestObjectAdd(SpecialsTestCase):

    async def test_add_two_vars(self):
        expr, mod = self.compile_expr("(+ a b)", a=10, b=32)
        self.assertEqual(42, await expr())


    async def test_add_three_vars(self):
        expr, mod = self.compile_expr("(+ a b c)", a=1, b=2, c=3)
        self.assertEqual(6, await expr())


    async def test_add_emits_count_opcode(self):
        code = self.compile_to_code("(+ a b c)")
        arg = _find_operand(code, Operand.ADD)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_add_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(+)")


    async def test_add_one_arg(self):
        expr, mod = self.compile_expr("(+ a)", a=7)
        self.assertEqual(7, await expr())


    async def test_add_const_fold_two_literals(self):
        # (+ 1 2) folds to a single CONST 3; no op needed
        code = self.compile_to_code("(+ 1 2)")
        arg = _find_operand(code, Operand.ADD)

        dis(code)

        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(3, consts)
        self.assertEqual(1, consts.count(3))


    async def test_add_const_fold_prefix_only(self):
        # (+ 1 2 a) → CONST 3, VAR a, OBJECT_ADD 2  (folded prefix, not trailing var)
        code = self.compile_to_code("(+ 1 2 a)")
        arg = _find_operand(code, Operand.ADD)

        dis(code)

        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)
        consts = _consts(code)
        self.assertIn(3, consts)


    async def test_add_no_fold_after_var(self):
        # (+ a 1 2) must NOT fold 1 and 2 — emit all three args
        code = self.compile_to_code("(+ a 1 2)")
        arg = _find_operand(code, Operand.ADD)
        self.assertIsNotNone(arg)
        self.assertEqual(3, arg)


    async def test_add_star_arg_rewrites_to_call_not_object_add(self):
        """
        *: in the argument list cannot lower to OBJECT_OPERAND; specials fall
        back to call (call opcodes, not an ADD operand).
        """

        code = self.compile_to_code("(+ 1 *: xs)")
        self.assertIsNone(_find_operand(code, Operand.ADD))
        self.assertTrue(self.call_ops(code))


    async def test_add_const_fold_star_arg(self):
        code = self.compile_to_code("(+ 1 11 *: xs)")
        arg = _find_operand(code, Operand.ADD)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(12, consts)


class TestObjectSubtract(SpecialsTestCase):

    async def test_subtract_two_vars(self):
        expr, mod = self.compile_expr("(- a b)", a=10, b=3)
        self.assertEqual(7, await expr())


    async def test_subtract_emits_count_opcode(self):
        code = self.compile_to_code("(- a b)")
        arg = _find_operand(code, Operand.SUBTRACT)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_subtract_const_fold(self):
        code = self.compile_to_code("(- 10 3)")
        arg = _find_operand(code, Operand.SUBTRACT)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(7, consts)


    async def test_subtract_unary_const_negates(self):
        # A lone constant must fold to its negation, matching the runtime
        # unary form; it used to return the literal unchanged
        code = self.compile_to_code("(- 5)")
        self.assertIsNone(_find_operand(code, Operand.SUBTRACT))
        consts = _consts(code)
        self.assertIn(-5, consts)
        self.assertNotIn(5, consts)

        expr, mod = self.compile_expr("(- 5)")
        self.assertEqual(-5, await expr())

        expr, mod = self.compile_expr("(- 2.5)")
        self.assertEqual(-2.5, await expr())


    async def test_subtract_unary_var_negates(self):
        code = self.compile_to_code("(- a)")
        self.assertEqual(1, _find_operand(code, Operand.SUBTRACT))

        expr, mod = self.compile_expr("(- a)", a=5)
        self.assertEqual(-5, await expr())


    async def test_subtract_unary_const_and_var_agree(self):
        expr, mod = self.compile_expr("#((- 5) (- a))", a=5)
        self.assertEqual((-5, -5), await expr())


    async def test_subtract_unary_non_numeric_const_errors(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(- "a")')
        with self.assertRaises(SpecialError):
            self.compile_to_code("(- None)")


class TestObjectUnaryReducers(SpecialsTestCase):
    """
    Operators with a unary form fold a lone constant through it; the
    others reject a lone argument at compile time
    """

    async def test_add_unary_const_is_identity(self):
        code = self.compile_to_code("(+ 5)")
        self.assertIsNone(_find_operand(code, Operand.ADD))
        expr, mod = self.compile_expr("(+ 5)")
        self.assertEqual(5, await expr())


    async def test_add_unary_var_emits_count_one(self):
        code = self.compile_to_code("(+ a)")
        self.assertEqual(1, _find_operand(code, Operand.ADD))
        expr, mod = self.compile_expr("(+ a)", a=5)
        self.assertEqual(5, await expr())


    async def test_add_unary_non_numeric_const_errors(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(+ "a")')


    async def test_str_unary_const_converts(self):
        code = self.compile_to_code("(#str 5)")
        self.assertIsNone(_find_op(code, OpCode.OBJECT_STR))
        expr, mod = self.compile_expr("#((#str 5) (#str a))", a=5)
        self.assertEqual(("5", "5"), await expr())


    async def test_operators_without_unary_form_reject_one_arg(self):
        for form in ("(* a)", "(/ 5)", "(// a)", "(& 5)", "(| a)", "(^ 5)",
                     "(% a)", "(<< 5)", "(>> a)", "(** 5)", "(@ a)"):
            with self.assertRaises(SpecialError, msg=form):
                self.compile_to_code(form)


    async def test_all_constants_still_fold_to_one_const(self):
        code = self.compile_to_code("(- 10 3 2)")
        self.assertIsNone(_find_operand(code, Operand.SUBTRACT))
        consts = _consts(code)
        self.assertIn(5, consts)


class TestObjectMultiply(SpecialsTestCase):

    async def test_multiply_two_vars(self):
        expr, mod = self.compile_expr("(* a b)", a=6, b=7)
        self.assertEqual(42, await expr())


    async def test_multiply_three_args(self):
        expr, mod = self.compile_expr("(* a b c)", a=2, b=3, c=4)
        self.assertEqual(24, await expr())


    async def test_multiply_emits_count_opcode(self):
        code = self.compile_to_code("(* a b)")
        arg = _find_operand(code, Operand.MULTIPLY)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_multiply_const_fold(self):
        code = self.compile_to_code("(* 3 4)")
        arg = _find_operand(code, Operand.MULTIPLY)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(12, consts)


class TestObjectPower(SpecialsTestCase):

    async def test_power_two_args(self):
        expr, mod = self.compile_expr("(** a b)", a=2, b=10)
        self.assertEqual(1024, await expr())


    async def test_power_emits_count_opcode(self):
        code = self.compile_to_code("(** a b)")
        arg = _find_operand(code, Operand.POWER)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_power_no_const_fold(self):
        # ** has py_fold=None; constants are never folded
        code = self.compile_to_code("(** 2 10)")
        self.assertIsNotNone(_find_operand(code, Operand.POWER))


class TestObjectBitAnd(SpecialsTestCase):

    async def test_bit_and_vars(self):
        expr, mod = self.compile_expr("(& a b)", a=0b1100, b=0b1010)
        self.assertEqual(0b1000, await expr())


    async def test_bit_and_emits_count_opcode(self):
        code = self.compile_to_code("(& a b)")
        arg = _find_operand(code, Operand.BIT_AND)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_and_const_fold(self):
        code = self.compile_to_code("(& 12 10)")
        arg = _find_operand(code, Operand.BIT_AND)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(8, consts)


class TestObjectBitOr(SpecialsTestCase):

    async def test_bit_or_vars(self):
        expr, mod = self.compile_expr("(| a b)", a=0b1100, b=0b0011)
        self.assertEqual(0b1111, await expr())


    async def test_bit_or_emits_count_opcode(self):
        code = self.compile_to_code("(| a b)")
        arg = _find_operand(code, Operand.BIT_OR)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_or_const_fold(self):
        code = self.compile_to_code("(| 12 3)")
        arg = _find_operand(code, Operand.BIT_OR)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(15, consts)


class TestObjectBitXor(SpecialsTestCase):

    async def test_bit_xor_vars(self):
        expr, mod = self.compile_expr("(^ a b)", a=0b1100, b=0b1010)
        self.assertEqual(0b0110, await expr())


    async def test_bit_xor_emits_count_opcode(self):
        code = self.compile_to_code("(^ a b)")
        arg = _find_operand(code, Operand.BIT_XOR)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_bit_xor_const_fold(self):
        code = self.compile_to_code("(^ 12 10)")
        arg = _find_operand(code, Operand.BIT_XOR)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(6, consts)


class TestObjectFloorDivide(SpecialsTestCase):

    async def test_floor_divide_vars(self):
        expr, mod = self.compile_expr("(// a b)", a=7, b=2)
        self.assertEqual(3, await expr())


    async def test_floor_divide_emits_count_opcode(self):
        code = self.compile_to_code("(// a b)")
        arg = _find_operand(code, Operand.FLOOR_DIVIDE)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_floor_divide_const_fold(self):
        code = self.compile_to_code("(// 7 2)")
        arg = _find_operand(code, Operand.FLOOR_DIVIDE)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(3, consts)


class TestObjectMatrixMultiply(SpecialsTestCase):

    async def test_matrix_multiply_emits_count_opcode(self):
        code = self.compile_to_code("(@ a b)")
        arg = _find_operand(code, Operand.MATRIX_MULTIPLY)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


# ---------------------------------------------------------------------------
# Numeric interface — NOARG binary opcodes
# ---------------------------------------------------------------------------

class TestObjectTrueDivide(SpecialsTestCase):

    async def test_true_divide_vars(self):
        expr, mod = self.compile_expr("(/ a b)", a=7, b=2)
        self.assertEqual(3.5, await expr())


    async def test_true_divide_emits_count_opcode(self):
        code = self.compile_to_code("(/ a b)")
        arg = _find_operand(code, Operand.TRUE_DIVIDE)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_true_divide_requires_at_least_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(/)")


    async def test_true_divide_const_fold(self):
        code = self.compile_to_code("(/ 7.0 2.0)")
        arg = _find_operand(code, Operand.TRUE_DIVIDE)
        self.assertIsNone(arg)
        consts = _consts(code)
        self.assertIn(3.5, consts)


class TestObjectModulo(SpecialsTestCase):

    async def test_modulo_vars(self):
        expr, mod = self.compile_expr("(% a b)", a=10, b=3)
        self.assertEqual(1, await expr())


    async def test_modulo_emits_opcode_with_arg_two(self):
        # C handler requires opa==2
        code = self.compile_to_code("(% a b)")
        arg = _find_operand(code, Operand.MODULO)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_modulo_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(% a)")


class TestObjectBitLeft(SpecialsTestCase):

    async def test_bit_left_vars(self):
        expr, mod = self.compile_expr("(<< a b)", a=1, b=3)
        self.assertEqual(8, await expr())


    async def test_bit_left_emits_opcode(self):
        code = self.compile_to_code("(<< a b)")
        self.assertIsNotNone(_find_operand(code, Operand.BIT_LEFT))


    async def test_bit_left_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(<< a)")


class TestObjectBitRight(SpecialsTestCase):

    async def test_bit_right_vars(self):
        expr, mod = self.compile_expr("(>> a b)", a=16, b=2)
        self.assertEqual(4, await expr())


    async def test_bit_right_emits_opcode(self):
        code = self.compile_to_code("(>> a b)")
        self.assertIsNotNone(_find_operand(code, Operand.BIT_RIGHT))


    async def test_bit_right_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(>> a)")


class TestObjectDivmod(SpecialsTestCase):

    async def test_divmod_vars(self):
        expr, mod = self.compile_expr("(divmod a b)", a=10, b=3)
        self.assertEqual((3, 1), await expr())


    async def test_divmod_emits_opcode_with_arg_two(self):
        # C handler requires opa==2
        code = self.compile_to_code("(divmod a b)")
        arg = _find_op(code, OpCode.OBJECT_DIVMOD)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_divmod_requires_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(divmod a)")


# ---------------------------------------------------------------------------
# General object interface — NOARG unary opcodes
# ---------------------------------------------------------------------------

class TestObjectNot(SpecialsTestCase):

    async def test_not_true_becomes_false(self):
        expr, mod = self.compile_expr("(not a)", a=True)
        self.assertFalse(await expr())


    async def test_not_false_becomes_true(self):
        expr, mod = self.compile_expr("(not a)", a=False)
        self.assertTrue(await expr())


    async def test_not_emits_opcode(self):
        code = self.compile_to_code("(not a)")
        self.assertIn(OpCode.OBJECT_NOT, _opcodes(code))


    async def test_not_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(not)")


    async def test_not_rejects_two_args(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(not a b)")


class TestObjectRepr(SpecialsTestCase):

    async def test_repr_result(self):
        expr, mod = self.compile_expr("(repr a)", a=42)
        self.assertEqual("42", await expr())


    async def test_repr_emits_opcode(self):
        code = self.compile_to_code("(repr a)")
        self.assertIn(OpCode.OBJECT_REPR, _opcodes(code))


    async def test_repr_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(repr)")


class TestObjectTruth(SpecialsTestCase):

    async def test_truth_nonempty(self):
        expr, mod = self.compile_expr("(truth a)", a=[1])
        self.assertTrue(await expr())


    async def test_truth_empty(self):
        expr, mod = self.compile_expr("(truth a)", a=[])
        self.assertFalse(await expr())


    async def test_truth_emits_opcode(self):
        code = self.compile_to_code("(truth a)")
        self.assertIn(OpCode.OBJECT_TRUTH, _opcodes(code))


    async def test_truth_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(truth)")


class TestObjectIter(SpecialsTestCase):

    async def test_iter_produces_iterator(self):
        expr, mod = self.compile_expr("(iter a)", a=[1, 2, 3])
        result = await expr()
        self.assertEqual([1, 2, 3], list(result))


    async def test_iter_async_iterable(self):
        # a python async generator's __aiter__ returns itself; the
        # uniform acquisition takes the async protocol instead of
        # raising "not iterable"
        async def gen():
            yield 1

        a = gen()
        expr, mod = self.compile_expr("(iter a)", a=a)
        result = await expr()
        self.assertIs(result, a)


    async def test_iter_emits_opcode(self):
        # a bare VAR feeder fuses into VAR_ITER; a computed iterable
        # keeps the stack-based OBJECT_ITER
        code = self.compile_to_code("(iter a)")
        name_ix = _find_op(code, OpCode.VAR_ITER)
        self.assertEqual("a", str(code.constant(name_ix)))
        code = self.compile_to_code("(iter (a))")
        self.assertIn(OpCode.OBJECT_ITER, _opcodes(code))


    async def test_iter_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(iter)")


class TestObjectNext(SpecialsTestCase):

    async def test_next_value(self):
        expr, mod = self.compile_expr("(next a)", a=iter([5, 6]))
        self.assertEqual(5, await expr())


    async def test_next_exhausted_default(self):
        expr, mod = self.compile_expr(
            "(next a b)", a=iter([]), b="empty")
        self.assertEqual("empty", await expr())


    async def test_next_exhausted_raises_catchable(self):
        expr, mod = self.compile_expr(
            '(try (next a) [[e StopIteration] "exhausted"])',
            a=iter([]), StopIteration=StopIteration)
        self.assertEqual("exhausted", await expr())


    async def test_next_async_iterator_value(self):
        async def gen():
            yield 10

        expr, mod = self.compile_expr("(next a)", a=gen())
        self.assertEqual(10, await expr())


    async def test_next_awaiting_step_value(self):
        expr, mod = self.compile_expr(
            "(next a)", a=SlowAIter([7]))
        self.assertEqual(7, await expr())


    async def test_next_awaiting_step_default(self):
        expr, mod = self.compile_expr(
            "(next a b)", a=SlowAIter([]), b="done")
        self.assertEqual("done", await expr())


    async def test_next_emits_opcode_arity(self):
        # a bare VAR iterator feeder fuses into VAR_NEXT carrying the
        # arity; a bare default rides the stack
        code = self.compile_to_code("(next a)")
        a0, a1, _a2, _a3 = code.extra_tuple(
            _find_op(code, OpCode.VAR_NEXT))
        self.assertEqual(("a", 1), (str(code.constant(a0)), a1))
        code = self.compile_to_code("(next a b)")
        a0, a1, _a2, _a3 = code.extra_tuple(
            _find_op(code, OpCode.VAR_NEXT))
        self.assertEqual(("a", 2), (str(code.constant(a0)), a1))

        # a computed iterator keeps the stack-based OBJECT_NEXT, as
        # does a computed default (the iterator load cannot move past
        # a non-bare row)
        code = self.compile_to_code("(next (a))")
        self.assertEqual(1, _find_op(code, OpCode.OBJECT_NEXT))
        code = self.compile_to_code("(next a (b))")
        self.assertEqual(2, _find_op(code, OpCode.OBJECT_NEXT))


    async def test_next_requires_one_or_two_args(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(next)")
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(next a b c)")


class TestObjectBitInvert(SpecialsTestCase):

    async def test_bit_invert_value(self):
        expr, mod = self.compile_expr("(~ a)", a=0)
        self.assertEqual(-1, await expr())


    async def test_bit_invert_emits_opcode(self):
        code = self.compile_to_code("(~ a)")
        self.assertIn(OpCode.OBJECT_BIT_INVERT, _opcodes(code))


    async def test_bit_invert_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(~)")


class TestObjectStr(SpecialsTestCase):

    async def test_str_result(self):
        expr, mod = self.compile_expr("(#str a)", a=99)
        self.assertEqual("99", await expr())


    async def test_str_empty(self):
        expr, mod = self.compile_expr("(#str)")
        self.assertEqual("", await expr())


    async def test_str_emits_count_opcode(self):
        code = self.compile_to_code("(#str a)")
        arg = _find_op(code, OpCode.OBJECT_STR)
        self.assertIsNotNone(arg)
        self.assertEqual(1, arg)


class TestObjectLen(SpecialsTestCase):

    async def test_len_list(self):
        expr, mod = self.compile_expr("(len a)", a=[1, 2, 3])
        self.assertEqual(3, await expr())


    async def test_len_string(self):
        expr, mod = self.compile_expr("(len a)", a="hello")
        self.assertEqual(5, await expr())


    async def test_len_emits_opcode(self):
        code = self.compile_to_code("(len a)")
        self.assertIn(OpCode.OBJECT_LEN, _opcodes(code))


    async def test_len_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(len)")


class TestObjectType(SpecialsTestCase):

    async def test_type_of_int(self):
        expr, mod = self.compile_expr("(type a)", a=42)
        self.assertIs(int, await expr())


    async def test_type_emits_opcode(self):
        code = self.compile_to_code("(type a)")
        self.assertIn(OpCode.OBJECT_TYPE, _opcodes(code))


    async def test_type_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(type)")


class TestObjectDir(SpecialsTestCase):

    async def test_dir_returns_list(self):
        expr, mod = self.compile_expr("(dir a)", a=42)
        result = await expr()
        self.assertIsInstance(result, list)
        self.assertIn("__class__", result)


    async def test_dir_emits_opcode(self):
        code = self.compile_to_code("(dir a)")
        self.assertIn(OpCode.OBJECT_DIR, _opcodes(code))


    async def test_dir_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(dir)")


class TestObjectVars(SpecialsTestCase):

    async def test_vars_returns_dict(self):
        class Obj:
            pass
        o = Obj()
        o.x = 1
        o.y = 2
        expr, mod = self.compile_expr("(vars a)", a=o)
        result = await expr()
        self.assertIsInstance(result, dict)
        self.assertEqual(1, result["x"])
        self.assertEqual(2, result["y"])


    async def test_vars_emits_opcode(self):
        code = self.compile_to_code("(vars a)")
        self.assertIn(OpCode.OBJECT_VARS, _opcodes(code))


    async def test_vars_requires_one_arg(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code("(vars)")


class TestObjectFormat(SpecialsTestCase):

    async def test_format_no_rules_emits_opcode(self):
        # a bare VAR feeder fuses into VAR_FORMAT; a computed object
        # keeps the stack-based OBJECT_FORMAT
        code = self.compile_to_code("(format a)")
        name_ix = _find_op(code, OpCode.VAR_FORMAT)
        self.assertEqual("a", str(code.constant(name_ix)))
        code = self.compile_to_code("(format (a))")
        self.assertEqual(1, _find_op(code, OpCode.OBJECT_FORMAT))


    async def test_format_const_rules_emits_opcode(self):
        code = self.compile_to_code('(format a "04d")')
        a0, a1, _a2, _a3 = code.extra_tuple(
            _find_op(code, OpCode.VAR_FORMAT_CONST))
        self.assertEqual("a", str(code.constant(a0)))
        self.assertEqual("04d", code.constant(a1))
        code = self.compile_to_code('(format (a) "04d")')
        arg = _find_op(code, OpCode.OBJECT_FORMAT_CONST)
        self.assertEqual("04d", code.constant(arg))


    async def test_format_quoted_symbol_rules_emits_opcode(self):
        code = self.compile_to_code("(format a (quote z))")
        a0, a1, _a2, _a3 = code.extra_tuple(
            _find_op(code, OpCode.VAR_FORMAT_CONST))
        self.assertEqual("a", str(code.constant(a0)))
        self.assertEqual("z", code.constant(a1))


    async def test_format_dynamic_rules_emits_object_format_arg_two(self):
        code = self.compile_to_code("(format a r)")
        arg = _find_op(code, OpCode.OBJECT_FORMAT)
        self.assertIsNotNone(arg)
        self.assertEqual(2, arg)


    async def test_format_const_fold_without_rules(self):
        code = self.compile_to_code("(format 99)")
        self.assertNotIn(OpCode.OBJECT_FORMAT, _opcodes(code))
        consts = _consts(code)
        self.assertIn("99", consts)


    async def test_format_const_fold_with_const_rules(self):
        code = self.compile_to_code('(format 7 "04d")')
        self.assertNotIn(OpCode.OBJECT_FORMAT, _opcodes(code))
        self.assertNotIn(OpCode.OBJECT_FORMAT_CONST, _opcodes(code))
        consts = _consts(code)
        self.assertIn("0007", consts)


    async def test_format_requires_at_least_one_arg(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(format)")


    async def test_format_rejects_three_args(self):
        with self.assertRaises(SyntaxMatchError):
            self.compile_to_code("(format a b c)")


    async def test_object_str_opcode_style_is_count(self):
        self.assertEqual(
            OpStyle.COUNT,
            OpCode.OBJECT_STR.style)


# The end.
