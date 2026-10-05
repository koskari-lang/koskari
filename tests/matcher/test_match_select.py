# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for koskari.lib.match_select.
"""


import unittest

import pytest

from koskari.lib import Matcher, match_select
from koskari.matcher import MatchResult, thaw


pytestmark = pytest.mark.phase_infra


def _frozen_literal_tuple(value):
    """
    Single-spine-element matcher: one literal test on the MATCH entry
    """

    consts = (value,)
    nodes = (
        ("TEST", "LITERAL", 0),
        ("MATCH", 0),
    )
    return ("MATCHER", (), (), consts, nodes, 1)


class TestMatchSelect(unittest.TestCase):

    def test_first_wins(self):
        """
        First matcher matches; second is never consulted
        """

        m0 = thaw(_frozen_literal_tuple(42))
        m1 = thaw(_frozen_literal_tuple(99))
        self.assertIsInstance(m0, Matcher)
        self.assertIsInstance(m1, Matcher)

        idx, result = match_select((m0, m1), (42,))
        self.assertEqual(idx, 0)
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)


    def test_second_arm_used(self):
        """
        First matcher misses; second matches
        """

        m0 = thaw(_frozen_literal_tuple(42))
        m1 = thaw(_frozen_literal_tuple(99))

        idx, result = match_select((m0, m1), (99,))
        self.assertEqual(idx, 1)
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)


    def test_no_match_returns_sentinel(self):
        """
        No arm matches -> (-1, None)
        """

        m0 = thaw(_frozen_literal_tuple(42))
        m1 = thaw(_frozen_literal_tuple(99))

        idx, result = match_select((m0, m1), (1,))
        self.assertEqual(idx, -1)
        self.assertIsNone(result)


    def test_non_matcher_element_raises(self):
        """
        Sequence entries must be Matcher instances
        """

        m0 = thaw(_frozen_literal_tuple(42))

        with self.assertRaises(TypeError) as ctx:
            match_select((m0, "not-a-matcher"), (99,))
        self.assertIn("not a Matcher", str(ctx.exception))


class TestExternKeywordsAreIgnoredByName(unittest.TestCase):
    """
    A keyword naming no extern of a matcher is ignored, whether or not
    the matcher declares any externs, so one keyword set serves every
    matcher in a selection
    """

    def test_zero_extern_matcher_ignores_keywords(self):
        m = thaw(_frozen_literal_tuple(42))
        self.assertTrue(m.match((42,), bogus=1).matched)

    def test_select_shares_keywords_across_mixed_matchers(self):
        from koskari.lib import symbol
        from koskari.matcher import CAPTURE, MATCH, TYPE

        plain = thaw(_frozen_literal_tuple(42))
        typed = MATCH(CAPTURE("a", TYPE("symbol"))).build()
        self.assertEqual(typed.externs, ("symbol",))
        idx, result = match_select(
            (plain, typed), (symbol("x"),), symbol=symbol)
        self.assertEqual(idx, 1)
        self.assertTrue(result.matched)
