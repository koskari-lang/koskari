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
Tests for the externs a Matcher takes at construction and
Matcher.missing_externs
"""


import unittest

import pytest

from koskari.lib import symbol
from koskari.matcher import (
    APP, MATCH, TYPE, VALUE, Matcher, MatchResult, make_matcher, thaw)


pytestmark = pytest.mark.phase_infra


def _lambda_style_tuple():
    """
    Frozen matcher tuple with extern ('symbol',): TYPE(symbol) capture + REST
    """

    slots = ("name", "body")
    externs = ("symbol",)
    consts = ("::",)
    nodes = (
        ("TEST", "TYPE", 0),
        ("CAPTURE", 0, 0),
        ("TEST", "LITERAL", 0),
        ("TEST", "ANY", -1),
        ("TEST", "ANY", -1),
        ("REST", 1),
        ("CAPTURE", 1, 5),
        ("MATCH", 1, 2, 3, 4, 6),
    )
    return ("MATCHER", slots, externs, consts, nodes, 7)


def _two_extern_tuple():
    """
    Matcher declaring externs ('symbol', 'Point'); entry node uses symbol only
    """

    return (
        "MATCHER",
        (),
        ("symbol", "Point"),
        (),
        (("TEST", "TYPE", 0),),
        0,
    )


class TestKeywordExterns(unittest.TestCase):

    def test_every_extern_is_a_keyword_on_every_call(self):
        """
        A declared extern must be given when the matcher is made, and a
        matcher made without it is refused; two matchers over one
        frozen tuple each read their own externs, and match takes no
        keywords
        """

        with self.assertRaises(TypeError) as caught:
            thaw(_lambda_style_tuple())
        self.assertIn("symbol", str(caught.exception))

        m = thaw(_lambda_style_tuple(), {"symbol": symbol})
        self.assertIsInstance(m, Matcher)
        other = thaw(_lambda_style_tuple(), {"symbol": int})
        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        good = m.match(subj)
        self.assertIsInstance(good, MatchResult)
        self.assertTrue(good.matched)
        self.assertFalse(other.match(subj).matched)
        self.assertTrue(m.match(subj).matched)
        with self.assertRaises(TypeError):
            m.match(subj, symbol=symbol)

    def test_extern_named_subject(self):
        """
        An extern spelled subject is an extern like any other: it does
        not collide with the function's own formal, and its value
        reaches the match as an extern
        """

        m = thaw((
            "MATCHER", (), ("subject",), (), (("TEST", "TYPE", 0),), 0),
            {"subject": int})
        self.assertTrue(m.match((7,)).matched)
        self.assertFalse(m.match(("s",)).matched)


    def test_type_extern_named_match_subject(self):
        """
        A TYPE extern spelled match-subject reads its extern value, not
        the subject the matcher function takes
        """

        m = make_matcher(MATCH(TYPE("match-subject")), {"match-subject": int})
        self.assertTrue(m.match((7,)).matched)
        self.assertFalse(m.match(("s",)).matched)


    def test_app_extern_named_match_subject(self):
        """
        An APP extractor spelled match-subject is called on the element,
        not replaced by the subject the matcher function takes
        """

        m = make_matcher(
            MATCH(APP("match-subject", VALUE("a"))),
            {"match-subject": lambda v: v * 10})
        hit = m.match((3,))
        self.assertTrue(hit.matched)
        self.assertEqual(hit.captures["a"], 30)


    def test_user_type_extern(self):
        class Point:
            pass

        m = thaw((
            "MATCHER", (), ("Point",), (), (("TEST", "TYPE", 0),), 0),
            {"Point": Point})
        self.assertTrue(m.match((Point(),)).matched)
        self.assertFalse(m.match((7,)).matched)


class TestExternsPerMatcher(unittest.TestCase):

    def test_a_predicate_matching_with_another_matcher(self):
        """
        Two matchers over one frozen tuple each read their own externs:
        an outer predicate that runs the inner matcher mid-walk leaves
        the outer walk reading the outer predicate
        """

        frozen = (
            "MATCHER", (), ("p",), (),
            (("TEST", "PREDICATE", 0), ("TEST", "PREDICATE", 0),
             ("MATCH", 0, 1)),
            2)
        inner = Matcher.from_tuple(frozen, externs={"p": lambda y: False})
        seen = []

        def outer_p(x):
            seen.append(x)
            if x == 1:
                self.assertFalse(inner.match((9, 9)).matched)
            return True

        outer = Matcher.from_tuple(frozen, externs={"p": outer_p})
        self.assertTrue(outer.match((1, 2)).matched)
        self.assertEqual(seen, [1, 2])


class TestMissingExterns(unittest.TestCase):

    def test_missing_externs_basic(self):
        """
        Names absent from provided are reported in extern order
        """

        m = thaw(_two_extern_tuple(), {"symbol": symbol, "Point": object})
        missing = m.missing_externs(provided={"symbol": symbol("x")})
        self.assertEqual(missing, ("Point",))

    def test_missing_externs_validates_provided(self):
        """
        Non-mapping provided raises TypeError
        """

        m = thaw(_two_extern_tuple(), {"symbol": symbol, "Point": object})
        with self.assertRaises(TypeError):
            m.missing_externs(provided="not-a-mapping")


class TestKeywordsAreIgnoredByName(unittest.TestCase):

    def test_zero_extern_matcher_ignores_keywords(self):
        """
        A name in the externs naming no extern of a matcher is ignored,
        whether or not the matcher declares any externs
        """

        consts = (42,)
        nodes = (("TEST", "LITERAL", 0), ("MATCH", 0))
        m = thaw(("MATCHER", (), (), consts, nodes, 1), {"bogus": 1})
        self.assertTrue(m.match((42,)).matched)


# The end.
