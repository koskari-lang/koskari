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

from koskari.lib import Datum, cons, keyword, symbol
from koskari.matcher import (
    CAPTURE,
    LITERAL,
    MATCH,
    Matcher,
    REPEAT,
    REST,
    parse_explicit_rule_pattern,
)
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
        m = Matcher.from_tuple(p.freeze())
        r = m.match((a, b))
        self.assertTrue(r.matched)
        r2 = m.match((a, symbol("c")))
        self.assertFalse(r2.matched)


    def test_symbol_declaration(self):
        """
        (SYMBOL slot) uses registry declaration path
        """

        form = build_proper(symbol("SYMBOL"), symbol("v"))
        p = parse_explicit_rule_pattern(form)
        m = Matcher.from_tuple(p.freeze())
        arg = symbol("argval")
        r = m.match((arg,))
        self.assertTrue(r.matched)
        self.assertIs(r.captures["v"], arg)


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
        m = Matcher.from_tuple(pat.freeze())
        res = m.match([1])
        self.assertTrue(res.matched)
        self.assertEqual(res.captures["r"], ())


    def test_repeat_slot_min_freeze_thaw_match(self):
        """
        (REPEAT xs literal min: 2) freezes and matches two repetitions
        """

        form = build_proper(
            symbol("REPEAT"), symbol("xs"),
            build_proper(symbol("LITERAL"), symbol("x")),
            keyword("min"), 2)
        p = parse_explicit_rule_pattern(form)
        self.assertIsInstance(p, CAPTURE)
        self.assertIsInstance(p._sub, REPEAT)
        self.assertEqual(p._sub._min_count, 2)
        m = Matcher.from_tuple(p.freeze())
        xa = symbol("x")
        ok = m.match([xa, xa])
        self.assertTrue(ok.matched)
        cap = ok.captures["xs"]
        frames = cap.datum if isinstance(cap, Datum) else cap
        self.assertEqual(len(frames), 2)
        bad_one = m.match([xa])
        self.assertFalse(bad_one.matched)
        bad_extra = m.match([xa, xa, symbol("z")])
        self.assertFalse(bad_extra.matched)


# The end.
