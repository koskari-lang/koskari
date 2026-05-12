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
Tests for matcher.thaw and Matcher.from_tuple.
"""


import unittest

import pytest

from koskari.lib import Datum, cons, nil, symbol
from koskari.matcher import Matcher, MatchResult, thaw


pytestmark = pytest.mark.phase_infra


def _make_lambda_matcher_tuple():
    """
    Build a frozen tuple for a one-branch lambda-style matcher:
    MATCH(CAPTURE(name, TEST_TYPE symbol), TEST_LITERAL '::',
          TEST_ANY, TEST_ANY, REST(body))
    """

    slots = ("name", "body")
    externs = ("symbol",)
    consts = ("::",)
    nodes = (
        ("TEST", "TYPE", 0),                # 0: TEST_TYPE symbol
        ("CAPTURE", 0, 0, 0, -1),           # 1: CAPTURE name
        ("TEST", "LITERAL", 0),             # 2: TEST_LITERAL "::"
        ("TEST", "ANY", -1),                # 3: TEST_ANY (return)
        ("TEST", "ANY", -1),                # 4: TEST_ANY (formals)
        ("REST", 1),                        # 5: REST min_tail
        ("CAPTURE", 1, 5, 0, -1),           # 6: CAPTURE body
        ("MATCH", 1, 2, 3, 4, 6),           # 7: MATCH entry
    )
    return ("MATCHER", slots, externs, consts, nodes, 7)


class TestMatcherFromTuple(unittest.TestCase):

    def test_thaw_lambda_pattern(self):
        """
        Round-trip a hand-built lambda-style frozen tuple and run it
        """

        m = thaw(_make_lambda_matcher_tuple())
        self.assertIsInstance(m, Matcher)
        self.assertEqual(m.slots, ("name", "body"))
        self.assertEqual(m.externs, ("symbol",))
        self.assertEqual(m.consts, ("::",))

        subj = (symbol("foo"), "::", "ret-type", "formals", "b1", "b2")
        result = m.match(subj, symbol=symbol)
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"].datum, subj[0])
        self.assertEqual(
            result.captures["body"].datum, ("b1", "b2"))


    def test_thaw_round_trips_dict_captures(self):
        """
        A simple matcher with two captures returns a dict with both
        slots populated
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 0, 0, 0, -1),
            ("TEST", "ANY", -1),
            ("CAPTURE", 1, 2, 0, -1),
            ("MATCH", 1, 3),
        )
        frozen = (
            "MATCHER",
            ("a", "b"), (), (),
            nodes, 4)
        m = thaw(frozen)
        result = m.match([7, 8])
        self.assertTrue(result.matched)
        self.assertEqual(
            {k: v.datum for k, v in result.captures.items()},
            {"a": 7, "b": 8})


    def test_thaw_kospair_subject_yields_datum_captures(self):
        """
        KosPair-backed subjects store captures as Datum with position
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 0, 0, 0, -1),
            ("REST", 0),
            ("CAPTURE", 1, 2, 0, -1),
            ("MATCH", 1, 3),
        )
        frozen = (
            "MATCHER",
            ("head", "tail"), (), (),
            nodes, 4)
        m = thaw(frozen)
        subj = cons(1, 2, 3, nil)
        result = m.match(subj)
        self.assertTrue(result.matched)
        head = result.captures["head"]
        self.assertIsInstance(head, Datum)
        self.assertEqual(head.datum, 1)
        tail = result.captures["tail"]
        self.assertIsInstance(tail, Datum)
        self.assertEqual(tail.datum, cons(2, 3, nil))


    def test_thaw_oneof_sparse_captures(self):
        """
        A ONEOF with two branches does not populate keys for the
        non-matching branch
        """

        nodes = (
            ("TEST", "LITERAL", 0),
            ("CAPTURE", 0, 0, 0, -1),
            ("MATCH", 1),
            ("TEST", "LITERAL", 1),
            ("CAPTURE", 1, 3, 0, -1),
            ("MATCH", 4),
            ("ONEOF", 2, 5),
        )
        frozen = (
            "MATCHER",
            ("xs", "ys"), (), ("X", "Y"),
            nodes, 6)
        m = thaw(frozen)

        r1 = m.match(["X"])
        self.assertTrue(r1.matched)
        self.assertEqual(r1.captures["xs"].datum, "X")
        self.assertNotIn("ys", r1.captures)

        r2 = m.match(["Y"])
        self.assertTrue(r2.matched)
        self.assertNotIn("xs", r2.captures)
        self.assertEqual(r2.captures["ys"].datum, "Y")


    def test_thaw_raises_on_wrong_tuple_length(self):
        """
        Thaw rejects frozen tuples whose length is not 6
        """

        frozen = ("MATCHER", (), (), (), ())
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_named_group_nested_dict(self):
        """
        GROUP nodes bind a nested dict under the group slot name
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 1, 0, 0, -1),
            ("GROUP", 1),
            ("CAPTURE", 0, 2, 0, -1),
        )
        frozen = (
            "MATCHER",
            ("outer", "inner"), (), (),
            nodes, 3)
        m = thaw(frozen)
        result = m.match([42])
        self.assertTrue(result.matched)
        self.assertEqual(list(result.captures.keys()), ["outer"])
        inner = result.captures["outer"]
        self.assertIsInstance(inner, dict)
        self.assertEqual(inner["inner"].datum, 42)


    def test_thaw_raises_on_oob_slot_index(self):
        """
        Thaw rejects a CAPTURE whose slot index references a slot that
        does not exist
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 5, 0, 0, -1),
        )
        frozen = (
            "MATCHER",
            ("x",), (), (),
            nodes, 1)
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_raises_on_unknown_node_tag(self):
        """
        Thaw rejects node tuples whose tag is unknown
        """

        nodes = (("BOGUS", 0),)
        frozen = (
            "MATCHER", (), (), (), nodes, 0)
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_rejects_duplicate_child_index(self):
        """
        A frozen MATCH that names the same child twice is rejected
        because the intrusive next chain cannot represent it
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("TEST", "ANY", -1),
            ("MATCH", 0, 1, 0),
        )
        frozen = (
            "MATCHER", (), (), (),
            nodes, 2)
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_match_sequential_node(self):
        """
        MATCH node tuples denote sequential spine composition
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 0, 0, 0, -1),
            ("MATCH", 1),
        )
        frozen = (
            "MATCHER",
            ("x",), (), (),
            nodes, 2)
        m = thaw(frozen)
        result = m.match([42])
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["x"].datum, 42)


    def test_thaw_class_kw_and_pos_round_trip(self):
        """
        Thaw a CLASS / FIELD shape (one positional, one keyword) and
        execute it against a small inline class instance
        """

        class Point:
            __match_args__ = ("x",)


        nodes = (
            ("TEST", "ANY", -1),                # 0
            ("CAPTURE", 0, 0, 0, -1),           # 1: capture x
            ("FIELD_POS", 1),                   # 2
            ("TEST", "ANY", -1),                # 3
            ("CAPTURE", 1, 3, 0, -1),           # 4: capture y
            ("FIELD_KW", 4, "y"),               # 5: name will be added
            ("CLASS", 0, 2, 5),                 # 6
        )
        frozen = (
            "MATCHER",
            ("x", "y"), ("Point",), (),
            nodes, 6)

        m = thaw(frozen)
        self.assertIsInstance(m, Matcher)
        self.assertIn("y", m.consts)

        p = Point()
        p.x = 10
        p.y = 20
        result = m.match((p,), Point=Point)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["x"].datum, 10)
        self.assertEqual(result.captures["y"].datum, 20)

        not_point = object()
        result_bad = m.match((not_point,), Point=Point)
        self.assertFalse(result_bad.matched)


# The end.
