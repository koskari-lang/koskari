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
tests.syntax.test_syntax_dispatch

A Syntax tries each arm's matcher in order; the first arm that
matches expands

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import cons, nil, symbol
from koskari.matcher import (
    CALLEDBY, CAPTURE, LITERAL, MATCH, PREDICATE, TYPE, VALUE, first_match)
from koskari.syntax import SpecialTransform, Syntax


pytestmark = pytest.mark.phase_infra


def form(*items):
    return cons(*items, nil)


class TestSyntaxDispatch(unittest.TestCase):

    def test_syntax_selects_and_expands(self):
        name = symbol("compile/d")
        s = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("x")),
                lambda c, src, mr: ("one", mr.captures["x"].datum)),
            SpecialTransform(
                MATCH(CALLEDBY()),
                lambda c, src, mr: "none")))
        self.assertEqual(s.compile_impl(None, form(name, 5)), ("one", 5))
        self.assertEqual(s.compile_impl(None, form(name)), "none")

        tf, mr = s.match_transformer(form(name, 5))
        self.assertIs(tf, s.transforms[0])
        self.assertTrue(mr.matched)
        self.assertEqual(mr.captures["x"].datum, 5)
        self.assertEqual(s.match_transformer(form(name, 1, 2)), (None, None))


    def test_composed_syntax_dispatches_across_both(self):
        """
        An extension prepended with + is tried first, then the arms of
        the syntax it extends
        """

        name = symbol("compile/d")
        base = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("x")),
                lambda c, src, mr: "base"),))
        extension = Syntax(name, (
            SpecialTransform(
                MATCH(CALLEDBY(), LITERAL(1)),
                lambda c, src, mr: "extension"),
            SpecialTransform(
                MATCH(CALLEDBY(), VALUE("a"), VALUE("b")),
                lambda c, src, mr: "wide"),))
        composed = base + extension
        self.assertEqual(composed.compile_impl(None, form(name, 1)), "extension")
        self.assertEqual(composed.compile_impl(None, form(name, 2)), "base")
        self.assertEqual(composed.compile_impl(None, form(name, 1, 2)), "wide")
        self.assertEqual(base.compile_impl(None, form(name, 1)), "base")


class TestFirstMatch(unittest.TestCase):
    """
    The function a Syntax selects its arm with: each callable is called
    with the subject in order, and the first result that is not None
    gives the arm's index and the captures
    """

    def test_first_hit_wins(self):
        seen = []

        def miss(subject):
            seen.append("miss")
            return None

        def hit(subject):
            seen.append("hit")
            return {"s": subject}

        def later(subject):
            seen.append("later")
            return {}

        self.assertEqual(first_match(5, (miss, hit, later)), (1, {"s": 5}))
        self.assertEqual(seen, ["miss", "hit"])


    def test_empty_captures_are_a_hit(self):
        self.assertEqual(
            first_match(5, (lambda s: None, lambda s: {})), (1, {}))


    def test_miss(self):
        self.assertEqual(
            first_match(5, (lambda s: None, lambda s: None)), (-1, None))


    def test_no_callables(self):
        self.assertEqual(first_match(5, ()), (-1, None))


    def test_matchers_with_and_without_externs(self):
        """
        Built matchers, some declaring externs and some none, are tried
        in order over one subject, each reading its own externs
        """

        def big(v):
            return isinstance(v, int) and v > 9

        arms = (
            MATCH(CAPTURE("a", PREDICATE("big"))).build({"big": big}),
            MATCH(LITERAL(5)).build(),
            MATCH(CAPTURE("t", TYPE("Kind"))).build({"Kind": str}),
            MATCH(CAPTURE("any")).build(),
        )
        self.assertEqual(first_match((50,), arms), (0, {"a": 50}))
        self.assertEqual(first_match((5,), arms), (1, {}))
        self.assertEqual(first_match(("s",), arms), (2, {"t": "s"}))
        self.assertEqual(first_match((7,), arms), (3, {"any": 7}))
        self.assertEqual(first_match((1, 2), arms), (-1, None))


# The end.
