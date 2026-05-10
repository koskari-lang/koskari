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
slot_prefix, BINDING_CELL, and thaw round-trip (matcher_ng phase 4.5).
"""


import unittest

import pytest

from koskari.lib import keyword, symbol
from koskari.matcher_ng import (
    BINDING_CELL,
    MATCH,
    Matcher,
    binding_inner_pattern,
    repeat_capture,
    slot_prefix,
    thaw,
)


pytestmark = pytest.mark.phase_infra


SYMBOL_TYPE = type(symbol("x"))


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
        result = m.match(subj, symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        frames = result.captures["bindings"].datum
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
        result = m.match(subj, symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        frames = result.captures["rows"].datum
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["expr"].datum, 99)
        self.assertEqual(frames[0]["name"].datum, sx)


class TestSlotPrefixAndBindingCell(unittest.TestCase):


    def test_group_two_binding_cells_freezes(self):
        """
        Two BINDING_CELL instances use disjoint mangled slots so freeze
        succeeds (duplicate bare name/expr would fail without prefix)
        """

        pat = MATCH(BINDING_CELL("a"), BINDING_CELL("b"))
        frozen = pat.freeze()
        self.assertIn("a__name", frozen[1])
        self.assertIn("b__expr", frozen[1])


    def test_slot_prefix_rest_slot(self):
        """
        slot_prefix rewrites REST slots when nesting calls for it
        """

        from koskari.matcher_ng import REST

        pat = slot_prefix(MATCH(REST("tail"), BINDING_CELL("x")), "p")
        frozen = pat.freeze()
        slots = frozen[1]
        self.assertIn("p__tail", slots)


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
        result = m.match(subj, symbol=SYMBOL_TYPE)
        self.assertTrue(result.matched)
        frames = result.captures["bindings"].datum
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0]["name"].datum, sa)
        self.assertEqual(frames[0]["expr"].datum, 10)


# The end.
