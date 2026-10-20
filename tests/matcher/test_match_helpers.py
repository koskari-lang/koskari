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
tests.matcher.test_match_helpers

Contract tests for match_pattern_raw, match, and match1

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import cons, nil, symbol
from koskari.matcher import LIST, ONEOF, SYMBOL, VALUE
from koskari.matcher.patterns import match, match1, match_pattern_raw


pytestmark = pytest.mark.phase_infra


class TestMatchPatternRaw(unittest.TestCase):


    def test_nil_matches_empty_list_pattern(self):
        pat = LIST()
        caps = match1(nil, [pat])
        self.assertIsNotNone(caps)

    def test_pair_source_first_pattern_wins(self):
        inner = cons(symbol("a"), cons(symbol("b"), nil))
        p1 = LIST(SYMBOL("x"), SYMBOL("y"))
        p2 = LIST(VALUE("u"), VALUE("v"))
        caps = match_pattern_raw(cons(inner, nil), [p2, p1])
        self.assertIsNotNone(caps)
        self.assertEqual(str(caps["u"].datum), "a")
        self.assertEqual(str(caps["v"].datum), "b")

    def test_non_pair_non_nil_source_raises(self):
        pat = LIST(SYMBOL("x"))
        with self.assertRaises(TypeError):
            match_pattern_raw(99, [pat])

    def test_non_pattern_raises(self):
        src = cons(symbol("x"), nil)
        with self.assertRaises(TypeError):
            match_pattern_raw(src, [object()])

    def test_wraps_single_pattern(self):
        inner = cons(symbol("z"), nil)
        caps = match_pattern_raw(cons(inner, nil), LIST(SYMBOL("a")))
        self.assertIsNotNone(caps)
        self.assertEqual(caps["a"].datum, symbol("z"))


class TestMatchAndMatch1(unittest.TestCase):


    def test_match1_wraps_value(self):
        inner = cons(symbol("q"), symbol("r"), nil)
        caps = match1(
            inner,
            [LIST(SYMBOL("left"), SYMBOL("right"))])
        self.assertEqual(caps["left"].datum, symbol("q"))
        self.assertEqual(caps["right"].datum, symbol("r"))

    def test_no_match_returns_none(self):
        inner = cons(symbol("only"), nil)
        src = cons(inner, nil)
        caps = match_pattern_raw(
            src,
            [LIST(SYMBOL("a"), SYMBOL("b"))])
        self.assertIsNone(caps)

    def test_oneof_second_arm(self):
        inner = cons(symbol("k"), nil)
        src = cons(inner, nil)
        pat = ONEOF(
            LIST(SYMBOL("wrong")),
            LIST(SYMBOL("k")))
        caps = match_pattern_raw(src, [pat])
        self.assertIsNotNone(caps)
        self.assertTrue(
            any(getattr(v, "datum", None) is symbol("k") for v in caps.values()))


# The end.
