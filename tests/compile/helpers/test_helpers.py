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
Tests for koskari.compile.helpers (gather_parameters, gather_formals).
"""


import unittest

from koskari.lib import OpCode
from koskari.types import cons, keyword, nil, symbol
from koskari.exceptions import ParametersError, SpecialError
from koskari.compile.compiler import Compiler
from koskari.compile.helpers import (
    gather_formals,
    gather_parameters_ng,
    parse_decl,
    using_unpack_helper,
)

import pytest

pytestmark = pytest.mark.phase_compile


class TestGatherParametersNg(unittest.TestCase):

    def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, symbol("a")), (1, symbol("b"))])
        self.assertEqual(kwds_seq, [])

    def test_empty_list(self):
        args_seq, kwds_seq = gather_parameters_ng(nil)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [])

    def test_single_symbol_rest(self):
        rest = symbol("args")
        args_seq, kwds_seq = gather_parameters_ng(rest)
        self.assertEqual(args_seq, [("*", rest)])
        self.assertEqual(kwds_seq, [])

    def test_keywords(self):
        k1 = keyword("k1")
        k2 = keyword("k2")
        v1 = symbol("v1")
        v2 = symbol("v2")
        args = cons(k1, cons(v1, cons(k2, cons(v2, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [("k1", v1), ("k2", v2)])

    def test_positional_then_keywords(self):
        a = symbol("a")
        b = symbol("b")
        k = keyword("x")
        v = symbol("v")
        args = cons(a, cons(b, cons(k, cons(v, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, a), (1, b)])
        self.assertEqual(kwds_seq, [("x", v)])

    def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [(0, a), ("*", rest)])
        self.assertEqual(kwds_seq, [])

    def test_star_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k = keyword("*")
        kk = keyword("**")
        args = cons(k, cons(star_sym, cons(kk, cons(starstar_sym, nil))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [("*", star_sym)])
        self.assertEqual(kwds_seq, [("**", starstar_sym)])

    def test_interleaved_positional_and_star(self):
        a = symbol("a")
        bargs = symbol("bargs")
        c = symbol("c")
        dargs = symbol("dargs")
        k_star = keyword("*")
        args = cons(a, cons(k_star, cons(bargs, cons(c, cons(
            k_star, cons(dargs, nil))))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [
            (0, a), ("*", bargs), (1, c), ("*", dargs)])
        self.assertEqual(kwds_seq, [])

    def test_interleaved_keyword_and_starstar(self):
        x = keyword("x")
        xval = symbol("xval")
        kw1 = symbol("kw1")
        y = keyword("y")
        yval = symbol("yval")
        kw2 = symbol("kw2")
        kk = keyword("**")
        args = cons(x, cons(xval, cons(kk, cons(kw1, cons(
            y, cons(yval, cons(kk, cons(kw2, nil))))))))
        args_seq, kwds_seq = gather_parameters_ng(args)
        self.assertEqual(args_seq, [])
        self.assertEqual(kwds_seq, [
            ("x", xval), ("**", kw1), ("y", yval), ("**", kw2)])

    def test_missing_value_for_keyword_raises(self):
        args = cons(keyword("a"), nil)
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("keyword", str(ctx.exception).lower())

    def test_star_after_keyword_raises(self):
        a = symbol("a")
        k = keyword("a")
        v = symbol("v")
        k_star = keyword("*")
        star_sym = symbol("foo")
        args = cons(a, cons(k, cons(v, cons(k_star, cons(star_sym, nil)))))
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("*", str(ctx.exception))
        self.assertIn("before keyword", str(ctx.exception).lower())

    def test_positional_after_keyword_raises(self):
        k = keyword("x")
        v = symbol("v")
        extra = symbol("extra")
        args = cons(k, cons(v, cons(extra, nil)))
        with self.assertRaises(ParametersError) as ctx:
            gather_parameters_ng(args)
        self.assertIn("positional", str(ctx.exception).lower())
        self.assertIn("before keyword", str(ctx.exception).lower())

    def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(ParametersError):
            gather_parameters_ng(42)


class TestGatherFormals(unittest.TestCase):

    def test_positional_only(self):
        args = cons(symbol("a"), cons(symbol("b"), nil))
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(len(pos), 2)
        self.assertEqual(str(pos[0]), "a")
        self.assertEqual(str(pos[1]), "b")
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)
        self.assertEqual(annotations, {})

    def test_single_symbol_rest(self):
        rest = symbol("rest")
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            rest)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)
        self.assertEqual(annotations, {})

    def test_keyword_formals_with_defaults(self):
        a = symbol("a")
        k = keyword("k")
        v = symbol("v")
        args = cons(a, cons(k, cons(v, nil)))
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(len(defaults), 1)
        self.assertEqual(defaults[0], (k, v))
        self.assertEqual(kwonly, [])
        self.assertIsNone(star)
        self.assertIsNone(starstar)
        self.assertEqual(annotations, {})

    def test_improper_list_rest(self):
        a = symbol("a")
        rest = symbol("rest")
        args = cons(a, rest)
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, rest)
        self.assertIsNone(starstar)
        self.assertEqual(annotations, {})

    def test_star_then_keyword_only(self):
        a = symbol("a")
        star_sym = symbol("args")
        k = keyword("k")
        v = symbol("v")
        k_star = keyword("*")
        kk = keyword("**")
        args = cons(a, cons(k_star, cons(star_sym, cons(k, cons(v, nil)))))
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertIs(star, star_sym)
        self.assertIsNone(starstar)
        self.assertEqual(len(kwonly), 1)
        self.assertEqual(kwonly[0], (k, v))
        self.assertEqual(annotations, {})

    def test_starstar(self):
        star_sym = symbol("args")
        starstar_sym = symbol("kwargs")
        k_star = keyword("*")
        kk = keyword("**")
        args = cons(k_star, cons(star_sym, cons(kk, cons(starstar_sym, nil))))
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [])
        self.assertEqual(defaults, [])
        self.assertEqual(kwonly, [])
        self.assertIs(star, star_sym)
        self.assertIs(starstar, starstar_sym)
        self.assertEqual(annotations, {})

    def test_improper_with_keywords_raises(self):
        a = symbol("a")
        rest = symbol("rest")
        k = keyword("x")
        args = cons(a, cons(rest, cons(k, nil)))
        with self.assertRaises(SpecialError):
            gather_formals(args)

    def test_non_symbol_positional_raises(self):
        args = cons(keyword("not-a-symbol"), nil)
        with self.assertRaises(SpecialError):
            gather_formals(args)

    def test_missing_value_for_keyword_raises(self):
        args = cons(symbol("a"), cons(keyword("k"), nil))
        with self.assertRaises(SpecialError):
            gather_formals(args)

    def test_not_symbol_or_pair_raises(self):
        with self.assertRaises(SpecialError):
            gather_formals(42)

    def test_bracket_formal_type_double_colon(self):
        a = symbol("a")
        colon = keyword("::")
        t = symbol("int")
        bracket = cons(a, cons(colon, cons(t, nil)))
        args = cons(bracket, nil)
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [])
        self.assertEqual(annotations["a"]["type"], t)

    def test_bracket_formal_type_keyword(self):
        a = symbol("a")
        t = symbol("int")
        bracket = cons(a, cons(keyword("type:"), cons(t, nil)))
        args = cons(bracket, nil)
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(annotations["a"]["type"], t)

    def test_bracket_formal_value_default(self):
        a = symbol("a")
        dv = symbol("dv")
        bracket = cons(a, cons(keyword("value:"), cons(dv, nil)))
        args = cons(bracket, nil)
        pos, defaults, kwonly, star, starstar, annotations = gather_formals(
            args)
        self.assertEqual(pos, [a])
        self.assertEqual(defaults, [(keyword("a"), dv)])
        self.assertEqual(annotations, {})


class TestParseDecl(unittest.TestCase):

    def test_bare_symbol(self):
        x = symbol("x")
        n, t = parse_decl(x)
        self.assertIs(n, x)
        self.assertIsNone(t)

    def test_bracket_name_only(self):
        x = symbol("x")
        bracket = cons(x, nil)
        n, t = parse_decl(bracket)
        self.assertIs(n, x)
        self.assertIsNone(t)

    def test_bracket_name_type_double_colon(self):
        x = symbol("x")
        t = symbol("int")
        bracket = cons(x, cons(keyword("::"), cons(t, nil)))
        n, ty = parse_decl(bracket)
        self.assertIs(n, x)
        self.assertIs(ty, t)

    def test_rejects_value_default(self):
        x = symbol("x")
        dv = symbol("dv")
        bracket = cons(x, cons(keyword("value:"), cons(dv, nil)))
        with self.assertRaises(SpecialError):
            parse_decl(bracket)

    def test_rejects_sym_expr_pair(self):
        x = symbol("x")
        e = symbol("expr")
        bracket = cons(x, cons(e, nil))
        with self.assertRaises(SpecialError):
            parse_decl(bracket)

    def test_type_as_name(self):
        t = symbol("int")
        x = symbol("x")
        form = cons(t, cons(keyword("as"), cons(x, nil)))
        n, ty = parse_decl(form)
        self.assertIs(n, x)
        self.assertIs(ty, t)


class TestUsingUnpackHelper(unittest.TestCase):

    def setUp(self):
        self.compiler = Compiler("<test>", None)

    def test_bare_symbol(self):
        x = symbol("x")
        frag = using_unpack_helper(self.compiler, x)
        self.assertEqual(frag.declares, ["x"])
        self.assertEqual(
            frag.instructions,
            [(OpCode.KOS_OP_VAR_STORE, "x")])

    def test_fixed_arity_list(self):
        a, b, c = symbol("a"), symbol("b"), symbol("c")
        lhs = cons(a, cons(b, cons(c, nil)))
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["a", "b", "c"])
        self.assertEqual(
            frag.instructions,
            [
                (OpCode.KOS_OP_UNPACK, 3),
                (OpCode.KOS_OP_VAR_STORE, "a"),
                (OpCode.KOS_OP_VAR_STORE, "b"),
                (OpCode.KOS_OP_VAR_STORE, "c"),
            ])

    def test_star_pattern(self):
        a, rest, b = symbol("a"), symbol("rest"), symbol("b")
        star = keyword("*")
        lhs = cons(a, cons(star, cons(rest, cons(b, nil))))
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["a", "rest", "b"])
        self.assertEqual(
            frag.instructions,
            [
                (OpCode.KOS_OP_UNPACK_STAR, (1, 1, 0, 0)),
                (OpCode.KOS_OP_VAR_STORE, "a"),
                (OpCode.KOS_OP_VAR_STORE, "rest"),
                (OpCode.KOS_OP_VAR_STORE, "b"),
            ])

    def test_whole_sequence_star_only(self):
        whole = symbol("whole")
        star = keyword("*")
        lhs = cons(star, cons(whole, nil))
        frag = using_unpack_helper(self.compiler, lhs)
        self.assertEqual(frag.declares, ["whole"])
        self.assertEqual(
            frag.instructions,
            [(OpCode.KOS_OP_VAR_STORE, "whole")])

    def test_non_symbol_element_raises(self):
        lhs = cons(symbol("a"), cons(42, nil))
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, lhs)

    def test_empty_pattern_raises(self):
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, nil)

    def test_double_star_raises(self):
        star = keyword("*")
        lhs = cons(
            symbol("a"),
            cons(star, cons(symbol("b"), cons(star, cons(symbol("c"), nil)))))
        with self.assertRaises(SpecialError):
            using_unpack_helper(self.compiler, lhs)


# The end.
