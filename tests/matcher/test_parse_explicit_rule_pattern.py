# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for matcher.parse_explicit_rule_pattern (MATCH-dialect rule spines)
"""


import unittest

import pytest

from koskari.lib import cons, demand, gensym, keyword, nil, symbol
from koskari.matcher import (
    CAPTURE, CLASS, FORM, LITERAL, MATCH, REPEAT, REST,
    parse_explicit_rule_pattern)
from koskari.matcher.heads import parse_matcher
from koskari.types import build_proper


pytestmark = pytest.mark.phase_infra


class TestParseExplicitRulePattern(unittest.TestCase):


    def test_atom_is_literal(self):
        """
        Bare atom lowers to LITERAL
        """

        x = symbol("x")
        p = parse_explicit_rule_pattern(x)
        self.assertIsInstance(p, LITERAL)
        self.assertIs(p._value, x)


    def test_sequential_two_literals(self):
        """
        (a b) spine becomes MATCH of two LITERAL arms
        """

        a = symbol("a")
        b = symbol("b")
        form = build_proper(a, b)
        p = parse_explicit_rule_pattern(form)
        m = p.build()
        r = demand(m)((a, b))
        self.assertIsNotNone(r)
        r2 = demand(m)((a, symbol("c")))
        self.assertIsNone(r2)


    def test_field_name_is_the_symbols_base(self):
        """
        (FIELD NAME pat) names an attribute, which has no marks to
        carry: a template-written NAME arrives as a gensym and the
        lookup uses its base spelling
        """

        class Point:
            def __init__(self, x):
                self.x = x

        marked_x = gensym(symbol("x"), marks=1)
        field = parse_matcher(build_proper(
            symbol("FIELD"), marked_x,
            build_proper(symbol("VALUE"), symbol("v"))))
        m = MATCH(CLASS("Point", field)).build({"Point": Point})
        r = demand(m)((Point(7),))
        self.assertIsNotNone(r)
        self.assertEqual(r["v"], 7)


    def test_symbol_declaration(self):
        """
        (SYMBOL slot) uses registry declaration path
        """

        form = build_proper(symbol("SYMBOL"), symbol("v"))
        p = parse_explicit_rule_pattern(form)
        m = p.build()
        arg = symbol("argval")
        r = demand(m)((arg,))
        self.assertIsNotNone(r)
        self.assertIs(r["v"], arg)


    def test_anonymous_registered_head(self):
        """
        (LIST (SYMBOL k)) has no slot name and is still the LIST matcher,
        not an inline group headed by the literal LIST
        """

        form = build_proper(
            symbol("LIST"), build_proper(symbol("SYMBOL"), symbol("k")))
        p = parse_explicit_rule_pattern(form)
        self.assertIsInstance(p, FORM)
        m = MATCH(p).build()
        r = demand(m)((build_proper(symbol("a")),))
        self.assertIsNotNone(r)
        self.assertIs(r["k"].datum, symbol("a"))
        self.assertIsNone(demand(m)((symbol("LIST"), symbol("a"))))


    def test_unregistered_head_is_inline_group(self):
        """
        (foo (VALUE x)) with foo not a registered head is a MATCH of the
        literal foo and the inline cells
        """

        foo = symbol("foo")
        form = build_proper(foo, build_proper(symbol("VALUE"), symbol("x")))
        p = parse_explicit_rule_pattern(form)
        self.assertIsInstance(p, MATCH)
        self.assertIsInstance(p._children[0], LITERAL)
        self.assertIs(p._children[0]._value, foo)
        m = p.build()
        r = demand(m)((foo, 1))
        self.assertIsNotNone(r)
        self.assertEqual(r["x"], 1)


    def test_ellipsis_rejected(self):
        """
        Top-level Ellipsis is invalid
        """

        with self.assertRaises(ValueError) as ar:
            parse_explicit_rule_pattern(Ellipsis)
        self.assertIn("ellipsis", str(ar.exception).lower())


    def test_improper_list_rejected(self):
        """
        Improper cons spine is rejected
        """

        bad = cons(symbol("a"), symbol("b"))
        with self.assertRaises(ValueError) as ar:
            parse_explicit_rule_pattern(bad)
        self.assertIn("proper", str(ar.exception).lower())


    def test_empty_cell_rejected(self):
        """
        An empty list is an empty inline group, refused by name at the
        top level and as a cell
        """

        for form in (nil, cons(symbol("a"), nil, nil)):
            with self.assertRaises(ValueError) as ar:
                parse_explicit_rule_pattern(form)
            self.assertIn("empty", str(ar.exception).lower())


    def test_rest_min_tail_zero_binds_empty_tuple(self):
        """
        REST with min: 0 captures an empty spine tail as ()
        """

        r = symbol("r")
        form = build_proper(
            symbol("REST"), r, keyword("min"), 0)
        rest_pat = parse_explicit_rule_pattern(form)
        self.assertIsInstance(rest_pat, REST)
        self.assertEqual(rest_pat._min_tail, 0)
        pat = MATCH(LITERAL(1), rest_pat)
        m = pat.build()
        res = demand(m)([1])
        self.assertIsNotNone(res)
        self.assertEqual(res["r"], ())


    def test_repeat_slot_min_build_match(self):
        """
        (REPEAT xs literal min: 2) builds and matches two repetitions
        """

        form = build_proper(
            symbol("REPEAT"), symbol("xs"),
            build_proper(symbol("LITERAL"), symbol("x")),
            keyword("min"), 2)
        p = parse_explicit_rule_pattern(form)
        self.assertIsInstance(p, CAPTURE)
        self.assertIsInstance(p._sub, REPEAT)
        self.assertEqual(p._sub._min_count, 2)
        m = p.build()
        xa = symbol("x")
        ok = demand(m)([xa, xa])
        self.assertIsNotNone(ok)
        frames = ok["xs"]
        self.assertEqual(len(frames), 2)
        bad_one = demand(m)([xa])
        self.assertIsNone(bad_one)
        bad_extra = demand(m)([xa, xa, symbol("z")])
        self.assertIsNone(bad_extra)


# The end.
