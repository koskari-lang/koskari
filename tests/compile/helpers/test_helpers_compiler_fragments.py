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
tests.compile.helpers.test_helpers_compiler_fragments

Contract tests for const_only_expr, helper_begin, and using_unpack_helper

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.compiler import Compiler
from koskari.compile.helpers import (
    const_only_expr,
    helper_begin,
    using_unpack_helper,
)
from koskari.exceptions import SpecialError
from koskari.lib import OpCode, cons, keyword, nil, symbol


pytestmark = pytest.mark.phase_compile


class TestConstOnlyExpr(unittest.TestCase):


    def setUp(self):
        self.compiler = Compiler("<helpers_test>", None)


    def test_single_const_true(self):
        frag = self.compiler.fragment("c")
        frag.instruction(OpCode.CONST, 7)
        ok, val = const_only_expr(frag)
        self.assertTrue(ok)
        self.assertEqual(val, 7)

    def test_two_consts_false(self):
        frag = self.compiler.fragment("c")
        frag.instruction(OpCode.CONST, 1)
        frag.instruction(OpCode.CONST, 2)
        ok, val = const_only_expr(frag)
        self.assertFalse(ok)
        self.assertIsNone(val)

    def test_const_then_pop_resets(self):
        frag = self.compiler.fragment("c")
        frag.instruction(OpCode.CONST, 3)
        frag.instruction(OpCode.POP, 1)
        ok, val = const_only_expr(frag)
        self.assertFalse(ok)
        self.assertIsNone(val)


class TestHelperBegin(unittest.TestCase):


    def setUp(self):
        self.compiler = Compiler("<helpers_test>", None)


    def test_empty_body_emits_const_none(self):
        frag = self.compiler.fragment("b")
        out = helper_begin(frag, nil)
        self.assertIs(out, frag)
        self.assertEqual(
            frag.instructions,
            [(OpCode.CONST, None)])

    def test_two_const_exprs_tail_pops_first(self):
        frag = self.compiler.fragment("b")
        body = cons(10, cons(20, nil))
        out = helper_begin(frag, body)
        self.assertIs(out, frag)
        ops = [op for op, _ in frag.instructions]
        self.assertIn(OpCode.CONST, ops)
        self.assertIn(OpCode.POP, ops)


class TestUsingUnpackHelper(unittest.TestCase):


    def setUp(self):
        self.compiler = Compiler("<helpers_test>", None)


    def test_bare_symbol(self):
        x = symbol("x")
        frag = using_unpack_helper(self.compiler, x)
        self.assertEqual(frag.declares, ["x"])
        self.assertEqual(
            frag.instructions,
            [(OpCode.VAR_STORE, "x")])

    def test_fixed_arity_list(self):
        a, b, c = symbol("a"), symbol("b"), symbol("c")
        lhs = cons(a, b, c, nil)
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["a", "b", "c"])
        self.assertEqual(
            frag.instructions,
            [
                (OpCode.UNPACK, 3),
                (OpCode.VAR_STORE, "a"),
                (OpCode.VAR_STORE, "b"),
                (OpCode.VAR_STORE, "c"),
            ])

    def test_two_symbol_unpack_var(self):
        lhs = cons(symbol("a"), symbol("b"), nil)
        frag = using_unpack_helper(self.compiler, lhs, declare=False)
        self.assertEqual(frag.declares, [])
        self.assertEqual(
            frag.instructions,
            [(OpCode.UNPACK_VAR, ("a", "b", 0, 0))])

    def test_star_pattern(self):
        star = keyword("*")
        lhs = cons(symbol("a"), star, symbol("rest"), symbol("b"), nil)
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["a", "rest", "b"])
        self.assertEqual(
            frag.instructions,
            [
                (OpCode.UNPACK_STAR, (1, 1, 0, 0)),
                (OpCode.VAR_STORE, "a"),
                (OpCode.VAR_STORE, "rest"),
                (OpCode.VAR_STORE, "b"),
            ])

    def test_whole_sequence_star_only(self):
        whole = symbol("whole")
        star = keyword("*")
        lhs = cons(star, whole, nil)
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["whole"])
        self.assertEqual(
            frag.instructions,
            [(OpCode.VAR_STORE, "whole")])

    def test_nested_pair_tail(self):
        inner = cons(symbol("i"), symbol("j"), nil)
        lhs = cons(symbol("a"), inner, nil)
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertIn((OpCode.UNPACK, 2), frag.instructions)

    def test_non_symbol_element_raises(self):
        lhs = cons(symbol("a"), 42, nil)
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, lhs)

    def test_empty_pattern_raises(self):
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, nil)

    def test_double_star_raises(self):
        star = keyword("*")
        lhs = cons(
            symbol("a"),
            star,
            symbol("b"),
            star,
            symbol("c"),
            nil)
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, lhs)

    def test_declare_false_omits_declares(self):
        lhs = cons(symbol("a"), symbol("b"), symbol("c"), nil)
        frag = using_unpack_helper(self.compiler, lhs, declare=False)
        self.assertEqual(frag.declares, [])
        self.assertEqual(
            frag.instructions,
            [
                (OpCode.UNPACK, 3),
                (OpCode.VAR_STORE, "a"),
                (OpCode.VAR_STORE, "b"),
                (OpCode.VAR_STORE, "c"),
            ])


# The end.
