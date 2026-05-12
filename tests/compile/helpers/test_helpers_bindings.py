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
tests.compile.helpers.test_helpers_bindings

Contract tests for helper_binding, helper_using, gather_bindings,
convert_bindings, and unpack_lhs_bind_symbols

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: GPT-5.2 via Cursor
"""


import unittest

import pytest

from koskari.compile.helpers import (
    convert_bindings,
    gather_bindings,
    helper_binding,
    helper_using,
    unpack_lhs_bind_symbols,
)
from koskari.exceptions import SpecialError
from koskari.lib import cons, keyword, nil, symbol
from koskari.types import unpack


pytestmark = pytest.mark.phase_compile


class TestHelperBinding(unittest.TestCase):


    def test_sym_expr_form(self):
        src = cons(symbol("x"), symbol("rhs"), nil)
        name, val = helper_binding(src)
        self.assertEqual(name, symbol("x"))
        self.assertEqual(val, symbol("rhs"))

    def test_expr_as_name_form(self):
        src = cons(
            symbol("TypeExpr"),
            cons(keyword("as"), cons(symbol("n"), nil)))
        name, val = helper_binding(src)
        self.assertEqual(name, symbol("n"))
        self.assertEqual(val, symbol("TypeExpr"))

    def test_improper_list_raises(self):
        src = cons(symbol("a"), symbol("b"))
        with self.assertRaises(SpecialError):
            helper_binding(src)

    def test_unknown_shape_raises(self):
        src = cons(symbol("only"), nil)
        with self.assertRaises(SpecialError):
            helper_binding(src)


class TestHelperUsing(unittest.TestCase):


    def test_bare_expr(self):
        inner = cons(symbol("call"), symbol("arg"), nil)
        src = cons(inner, nil)
        name, val = helper_using(src)
        self.assertIsNone(name)
        self.assertEqual(val, inner)

    def test_named_like_binding(self):
        src = cons(symbol("y"), symbol("rhs"), nil)
        name, val = helper_using(src)
        self.assertEqual(name, symbol("y"))
        self.assertEqual(val, symbol("rhs"))


class TestGatherAndConvertBindings(unittest.TestCase):


    def test_empty_bindings_returns_empty_pair_lists(self):
        names, vals = gather_bindings(nil)
        self.assertEqual(names, [])
        self.assertEqual(vals, [])
        self.assertIs(convert_bindings(nil), nil)

    def test_round_trip_keyword_list(self):
        spine = cons(
            cons(symbol("a"), symbol("one"), nil),
            cons(
                cons(symbol("two"), keyword("as"), symbol("b"), nil),
                nil))
        names, vals = gather_bindings(spine)
        self.assertEqual(names, [symbol("a"), symbol("b")])
        self.assertEqual(vals, [symbol("one"), symbol("two")])
        out = convert_bindings(spine)
        self.assertEqual(
            list(unpack(out)),
            [keyword("a"), symbol("one"), keyword("b"), symbol("two")])

    def test_duplicate_name_raises(self):
        spine = cons(
            cons(symbol("z"), symbol("e1"), nil),
            cons(cons(symbol("z"), symbol("e2"), nil), nil))
        with self.assertRaises(SpecialError) as ctx:
            gather_bindings(spine, source=spine)
        self.assertIn("duplicate", str(ctx.exception).lower())


class TestUnpackLhsBindSymbols(unittest.TestCase):


    def test_single_symbol(self):
        self.assertEqual(
            list(unpack_lhs_bind_symbols(symbol("solo"))),
            [symbol("solo")])

    def test_fixed_list(self):
        lhs = cons(symbol("p"), cons(symbol("q"), cons(symbol("r"), nil)))
        self.assertEqual(
            list(unpack_lhs_bind_symbols(lhs)),
            [symbol("p"), symbol("q"), symbol("r")])

    def test_star_middle(self):
        star = keyword("*")
        lhs = cons(
            symbol("a"),
            cons(star, cons(symbol("rest"), cons(symbol("b"), nil))))
        self.assertEqual(
            list(unpack_lhs_bind_symbols(lhs)),
            [symbol("a"), symbol("rest"), symbol("b")])

    def test_flatten_nested(self):
        inner = cons(symbol("i"), symbol("j"), nil)
        lhs = cons(inner, cons(symbol("k"), nil))
        self.assertEqual(
            list(unpack_lhs_bind_symbols(lhs, flatten=True)),
            [symbol("i"), symbol("j"), symbol("k")])

    def test_improper_raises(self):
        lhs = cons(symbol("a"), symbol("tail"))
        with self.assertRaises(SpecialError):
            list(unpack_lhs_bind_symbols(lhs))

    def test_double_star_keyword_raises(self):
        star = keyword("*")
        lhs = cons(
            symbol("a"),
            star,
            symbol("b"),
            star,
            symbol("c"),
            nil)
        with self.assertRaises(SpecialError):
            list(unpack_lhs_bind_symbols(lhs))


# The end.
