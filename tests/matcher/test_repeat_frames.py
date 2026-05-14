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
Tests for CAPTURE(REPEAT(inner)) frame lists, binding_inner_pattern,
and thaw round-trip (matcher phase 4.5).
"""


import unittest

import pytest

from koskari.lib import Datum, keyword, symbol
from koskari.matcher import (
    Matcher,
    binding_inner_pattern,
    repeat_capture,
    thaw,
)


pytestmark = pytest.mark.phase_infra


class TestRepeatCapture(unittest.TestCase):


    def test_repeat_capture_two_sym_expr_pairs(self):
        """
        repeat_capture over binding_inner_pattern matches two (symbol,
        expr) cells and yields a list of two frame dicts
        """

        pat = repeat_capture("bindings", binding_inner_pattern())
        m = pat.build()
        sa = symbol("a")
        sb = symbol("b")
        subj = ((sa, 1), (sb, 2))
        result = m.match(subj, symbol=symbol)
        self.assertTrue(result.matched)
        cap = result.captures["bindings"]
        frames = cap.datum if isinstance(cap, Datum) else cap
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["name"].datum, sa)
        self.assertEqual(frames[0]["expr"].datum, 1)
        self.assertEqual(frames[1]["name"].datum, sb)
        self.assertEqual(frames[1]["expr"].datum, 2)
        self.assertNotIn("name", result.captures)
        self.assertNotIn("expr", result.captures)


    def test_repeat_capture_expr_as_name_branch(self):
        """
        The ONEOF second branch matches (expr as: name) inside each
        repeated cell
        """

        pat = repeat_capture("rows", binding_inner_pattern())
        m = pat.build()
        sx = symbol("x")
        cell = (99, keyword("as"), sx)
        subj = (cell,)
        result = m.match(subj, symbol=symbol)
        self.assertTrue(result.matched)
        cap = result.captures["rows"]
        frames = cap.datum if isinstance(cap, Datum) else cap
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["expr"].datum, 99)
        self.assertEqual(frames[0]["name"].datum, sx)


class TestRepeatFramesThawRoundTrip(unittest.TestCase):


    def test_thaw_repeat_capture_same_frames(self):
        """
        Frozen tuple from repeat_capture round-trips through thaw and
        matches with the same list-of-dicts captures shape
        """

        pat = repeat_capture("bindings", binding_inner_pattern())
        frozen = pat.freeze()
        m = thaw(frozen)
        self.assertIsInstance(m, Matcher)
        sa = symbol("u")
        sb = symbol("v")
        subj = ((sa, 10), (sb, 20))
        result = m.match(subj, symbol=symbol)
        self.assertTrue(result.matched)
        cap = result.captures["bindings"]
        frames = cap.datum if isinstance(cap, Datum) else cap
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["name"].datum, sa)
        self.assertEqual(frames[0]["expr"].datum, 10)


# The end.
