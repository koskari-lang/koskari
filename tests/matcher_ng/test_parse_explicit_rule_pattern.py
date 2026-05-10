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
Tests for matcher_ng.parse_explicit_rule_pattern (MATCH-dialect rule spines)
"""


import unittest

import pytest

from koskari.lib import cons, symbol
from koskari.matcher_ng import (
    LITERAL,
    Matcher,
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
        self.assertIs(r.captures["v"].datum, arg)


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


# The end.
