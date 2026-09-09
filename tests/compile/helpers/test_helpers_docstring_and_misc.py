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
tests.compile.helpers.test_helpers_docstring_and_misc

Contract tests for discover_docstring, strip_leading_docstring,
helper_elsebody, pycons, and requote

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.helpers import (
    discover_docstring, helper_elsebody, pycons, requote,
    strip_leading_docstring)
from koskari.lib import car, cdr, cons, keyword, nil, symbol


pytestmark = pytest.mark.phase_compile


class TestDiscoverDocstring(unittest.TestCase):


    def test_leading_string(self):
        body = cons("doc text", cons(symbol("x"), nil))
        self.assertEqual(discover_docstring(body), "doc text")

    def test_peels_empty_begin_then_string(self):
        beg = symbol("begin")
        inner = cons("inner doc", symbol("form"), nil)
        wrapped = cons(cons(beg, inner), cons(symbol("after"), nil))
        self.assertEqual(discover_docstring(wrapped), "inner doc")

    def test_non_doc_returns_none(self):
        body = cons(symbol("f"), nil)
        self.assertIsNone(discover_docstring(body))


class TestStripLeadingDocstring(unittest.TestCase):


    def test_removes_leading_str(self):
        body = cons("d", cons(symbol("r"), nil))
        doc, rest = strip_leading_docstring(body)
        self.assertEqual(doc, "d")
        self.assertEqual(rest, cons(symbol("r"), nil))

    def test_no_doc_returns_none_and_original_body(self):
        body = cons(symbol("z"), nil)
        doc, rest = strip_leading_docstring(body)
        self.assertIsNone(doc)
        self.assertIs(rest, body)


class TestHelperElsebody(unittest.TestCase):


    def test_trailing_else_clause(self):
        main1 = cons(symbol("m1"), nil)
        main2 = cons(symbol("m2"), nil)
        else_kw = keyword("else")
        else_arm = cons(else_kw, symbol("e1"), nil)
        body = cons(main1, cons(main2, cons(else_arm, nil)))
        stripped, else_body = helper_elsebody(body)
        self.assertEqual(stripped, cons(main1, cons(main2, nil)))
        self.assertEqual(else_body, cons(symbol("e1"), nil))

    def test_trailing_else_clause_keeps_every_form(self):
        main1 = cons(symbol("m1"), nil)
        else_arm = cons(keyword("else"), symbol("e1"), symbol("e2"), nil)
        body = cons(main1, cons(else_arm, nil))
        stripped, else_body = helper_elsebody(body)
        self.assertEqual(stripped, cons(main1, nil))
        self.assertEqual(else_body, cons(symbol("e1"), symbol("e2"), nil))

    def test_empty_else_returns_none(self):
        body = cons(symbol("only"), cons(cons(keyword("else"), nil), nil))
        stripped, else_body = helper_elsebody(body)
        self.assertEqual(stripped, cons(symbol("only"), nil))
        self.assertIsNone(else_body)

    def test_no_else_returns_nil_else(self):
        body = cons(symbol("only"), nil)
        stripped, else_expr = helper_elsebody(body)
        self.assertEqual(stripped, body)
        self.assertIs(else_expr, nil)


class TestPycons(unittest.TestCase):


    def test_empty_list_is_nil(self):
        self.assertIs(pycons([]), nil)

    def test_nested_tuple(self):
        out = pycons(((symbol("a"), symbol("b")), symbol("c")))
        self.assertEqual(out[0][0], symbol("a"))
        self.assertEqual(out[0][1][0], symbol("b"))
        self.assertEqual(out[1][0], symbol("c"))
        self.assertEqual(out[1][1], nil)

    def test_direct_atom_passthrough(self):
        self.assertIs(pycons(symbol("z")), symbol("z"))


class TestRequote(unittest.TestCase):


    def test_quotes_each_spine_element(self):
        src = cons(symbol("a"), cons(1, nil))
        out = requote(src)
        cq = symbol("quote")
        cc = symbol("cons")
        self.assertEqual(out[0], cc)
        q1 = car(cdr(out))
        self.assertEqual(car(q1), cq)
        self.assertEqual(car(cdr(q1)), symbol("a"))
        q2 = car(cdr(cdr(out)))
        self.assertEqual(car(q2), cq)
        self.assertEqual(car(cdr(q2)), 1)
        self.assertEqual(car(cdr(cdr(cdr(out)))), nil)
        self.assertEqual(cdr(cdr(cdr(cdr(out)))), nil)

    def test_nil_is_not_valid_input(self):
        with self.assertRaises(ValueError):
            requote(nil)


# The end.
