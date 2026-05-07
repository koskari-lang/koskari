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
Tests for matcher_ng.thaw and Matcher.from_tuple.
"""


import unittest

import pytest

from koskari.lib import cons, nil, symbol
from koskari.matcher_ng import ABSENT, Matcher, MatchResult, thaw


pytestmark = pytest.mark.phase_infra


def _make_lambda_matcher_tuple():
    """
    Build a frozen tuple for a one-branch lambda-style matcher:
    GROUP(CAPTURE(name, TEST_TYPE symbol), TEST_LITERAL '::',
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
        ("REST", 1),                        # 5: REST body
        ("GROUP", 1, 2, 3, 4, 5),           # 6: GROUP entry
    )
    return ("MATCHER", 1, slots, externs, consts, nodes, 6, {})


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
        result = m.match(subj, symbol=type(symbol("x")))
        self.assertIsInstance(result, MatchResult)
        self.assertTrue(result.matched)
        self.assertIs(result.captures["name"], subj[0])
        self.assertEqual(result.captures["body"], ("b1", "b2"))


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
            ("GROUP", 1, 3),
        )
        frozen = (
            "MATCHER", 1,
            ("a", "b"), (), (),
            nodes, 4, {})
        m = thaw(frozen)
        result = m.match([7, 8])
        self.assertTrue(result.matched)
        self.assertEqual(result.captures, {"a": 7, "b": 8})


    def test_thaw_kospair_subject(self):
        """
        A matcher thawed from a frozen tuple works against a kospair
        subject as well as a Python sequence
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 0, 0, 0, -1),
            ("REST", 1),
            ("GROUP", 1, 2),
        )
        frozen = (
            "MATCHER", 1,
            ("head", "tail"), (), (),
            nodes, 3, {})
        m = thaw(frozen)
        subj = cons(1, cons(2, cons(3, nil)))
        result = m.match(subj)
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["head"], 1)
        # rest is a kospair tail — verify by walking it
        tail = result.captures["tail"]
        self.assertEqual(tail, cons(2, cons(3, nil)))


    def test_thaw_oneof_absent_for_other_branches(self):
        """
        A ONEOF with two branches yields ABSENT for slots from the
        non-matching branch
        """

        nodes = (
            ("TEST", "LITERAL", 0),
            ("CAPTURE", 0, 0, 0, -1),
            ("GROUP", 1),
            ("TEST", "LITERAL", 1),
            ("CAPTURE", 1, 3, 0, -1),
            ("GROUP", 4),
            ("ONEOF", 2, 5),
        )
        frozen = (
            "MATCHER", 1,
            ("xs", "ys"), (), ("X", "Y"),
            nodes, 6, {})
        m = thaw(frozen)

        r1 = m.match(["X"])
        self.assertTrue(r1.matched)
        self.assertEqual(r1.captures["xs"], "X")
        self.assertIs(r1.captures["ys"], ABSENT)

        r2 = m.match(["Y"])
        self.assertTrue(r2.matched)
        self.assertIs(r2.captures["xs"], ABSENT)
        self.assertEqual(r2.captures["ys"], "Y")


    def test_thaw_raises_on_bad_version(self):
        """
        Thaw rejects frozen tuples whose version is not 1
        """

        frozen = ("MATCHER", 99, (), (), (), (), 0, {})
        with self.assertRaises(ValueError):
            thaw(frozen)


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
            "MATCHER", 1,
            ("x",), (), (),
            nodes, 1, {})
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_raises_on_unknown_node_tag(self):
        """
        Thaw rejects node tuples whose tag is unknown
        """

        nodes = (("BOGUS", 0),)
        frozen = (
            "MATCHER", 1, (), (), (), nodes, 0, {})
        with self.assertRaises(ValueError):
            thaw(frozen)


    def test_thaw_match_alias_for_group(self):
        """
        'MATCH' is accepted as an alias for 'GROUP' in node tuples
        """

        nodes = (
            ("TEST", "ANY", -1),
            ("CAPTURE", 0, 0, 0, -1),
            ("MATCH", 1),
        )
        frozen = (
            "MATCHER", 1,
            ("x",), (), (),
            nodes, 2, {})
        m = thaw(frozen)
        result = m.match([42])
        self.assertTrue(result.matched)
        self.assertEqual(result.captures["x"], 42)


# The end.
