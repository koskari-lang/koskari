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
The matcher walks bracket-pair subjects exactly like plain pairs.

The reader does not yet emit bracket-pairs, so subjects are built
directly. Distinguishing bracket vs paren is a later post-matcher pass;
the matcher itself just walks either.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import bracket_nil, bracket_pair, cons, demand, nil
from koskari.matcher import CAPTURE, MATCH, REST


pytestmark = pytest.mark.phase_infra


def _blist(*items, tail=nil):
    """Build a bracket-pair spine from items, terminated by tail."""

    result = tail
    for item in reversed(items):
        result = bracket_pair(item, result)
    return result


class TestBracketPairSubject(unittest.TestCase):
    """A bracket-pair list is a valid match subject"""

    def test_matches_like_a_paren_list(self):
        """head/REST over a bracket list matches like the paren equivalent"""

        m = MATCH(CAPTURE("head"), REST("tail")).build()

        r = demand(m)(_blist(1, 2, 3))
        self.assertIsNotNone(r)
        self.assertEqual(r["head"].datum, 1)
        # the captured tail is the [2 3] sub-spine, equal to (2 3)
        self.assertEqual(r["tail"].datum, cons(2, 3, nil))

    def test_fixed_arity_proper_match(self):
        """a fixed-arity pattern matches a proper, nil-terminated bracket list"""

        m = MATCH(CAPTURE("a"), CAPTURE("b"), CAPTURE("c")).build()

        r = demand(m)(_blist(1, 2, 3))
        self.assertIsNotNone(r)
        self.assertEqual(r["a"].datum, 1)
        self.assertEqual(r["b"].datum, 2)
        self.assertEqual(r["c"].datum, 3)

    def test_bracket_nil_terminator_is_proper(self):
        """a bracket-nil terminator counts as a proper list end"""

        m = MATCH(CAPTURE("a"), CAPTURE("b")).build()

        r = demand(m)(_blist(1, 2, tail=bracket_nil))
        self.assertIsNotNone(r)
        self.assertEqual(r["a"].datum, 1)
        self.assertEqual(r["b"].datum, 2)


# The end.
