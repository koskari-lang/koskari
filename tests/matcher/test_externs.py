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
Tests for keyword externs on Matcher.match and Matcher.missing_externs
"""


import unittest

import pytest

from koskari.lib import symbol
from koskari.matcher import Matcher, MatchResult, thaw


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
        A declared extern must be given by keyword on each call; a call
        without it is refused before the match runs
        """

        m = thaw(_lambda_style_tuple())
        self.assertIsInstance(m, Matcher)
        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        good = m.match(subj, symbol=symbol)
        self.assertIsInstance(good, MatchResult)
        self.assertTrue(good.matched)
        bad = m.match(subj, symbol=int)
        self.assertFalse(bad.matched)
        with self.assertRaises(TypeError) as caught:
            m.match(subj)
        self.assertIn("symbol", str(caught.exception))
        # the keyword of a previous call does not carry over
        m.match(subj, symbol=symbol)
        with self.assertRaises(TypeError):
            m.match(subj)

    def test_extern_named_subject(self):
        """
        An extern spelled subject is an extern like any other: it does
        not collide with the function's own formal, and the keyword
        reaches the match as an extern
        """

        m = thaw((
            "MATCHER", (), ("subject",), (), (("TEST", "TYPE", 0),), 0))
        self.assertTrue(m.match((7,), subject=int).matched)
        self.assertFalse(m.match(("s",), subject=int).matched)


    def test_user_type_extern(self):
        class Point:
            pass

        m = thaw((
            "MATCHER", (), ("Point",), (), (("TEST", "TYPE", 0),), 0))
        self.assertTrue(m.match((Point(),), Point=Point).matched)
        self.assertFalse(m.match((7,), Point=Point).matched)


class TestMissingExterns(unittest.TestCase):

    def test_missing_externs_basic(self):
        """
        Names absent from provided are reported in extern order
        """

        m = thaw(_two_extern_tuple())
        missing = m.missing_externs(provided={"symbol": symbol("x")})
        self.assertEqual(missing, ("Point",))

    def test_missing_externs_validates_provided(self):
        """
        Non-mapping provided raises TypeError
        """

        m = thaw(_two_extern_tuple())
        with self.assertRaises(TypeError):
            m.missing_externs(provided="not-a-mapping")


class TestKeywordsAreIgnoredByName(unittest.TestCase):

    def test_zero_extern_matcher_ignores_keywords(self):
        """
        A keyword naming no extern of a matcher is ignored, whether or
        not the matcher declares any externs
        """

        consts = (42,)
        nodes = (("TEST", "LITERAL", 0), ("MATCH", 0))
        m = thaw(("MATCHER", (), (), consts, nodes, 1))
        self.assertTrue(m.match((42,), bogus=1).matched)


# The end.
