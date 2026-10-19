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
tests.matcher.test_pattern_build

Pattern.build compiles the pattern to a Function: one positional
formal, the subject; the captures on a match and None on a miss, a
dict for a sequence subject and Datum values over a pair spine; a
missing extern refused at build; Pattern.match gives a MatchResult

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import Datum, Function, cons, demand, nil
from koskari.matcher import (
    APP, CAPTURE, GROUP, LITERAL, MATCH, MatchResult, ONEOF, PREDICATE, REST,
    TYPE, VALUE, make_matcher)


pytestmark = pytest.mark.phase_infra


class TestPatternBuild(unittest.TestCase):

    def test_build_returns_a_function(self):
        fn = MATCH(VALUE("x")).build()
        self.assertIsInstance(fn, Function)
        self.assertIsInstance(make_matcher(MATCH(VALUE("x"))), Function)


    def test_the_function_returns_captures_or_none(self):
        call = demand(MATCH(CAPTURE("x", TYPE("int"))).build())
        self.assertEqual(call((7,)), {"x": 7})
        self.assertIsNone(call(("s",)))


    def test_a_missing_extern_is_refused_at_build(self):
        with self.assertRaises(TypeError) as caught:
            MATCH(TYPE("Point")).build()
        self.assertIn("Point", str(caught.exception))


    def test_one_missing_extern_among_given_ones_is_refused(self):
        pattern = MATCH(PREDICATE("p"), TYPE("T"), APP("f", CAPTURE("x")))
        with self.assertRaises(TypeError) as caught:
            pattern.build({"p": bool, "f": abs})
        self.assertIn("T", str(caught.exception))
        m = demand(pattern.build({"p": bool, "f": abs, "T": int}))
        self.assertEqual(m((1, 2, -3)), {"x": 3})


    def test_match_returns_a_match_result(self):
        pattern = MATCH(CAPTURE("x", PREDICATE("p")))
        hit = pattern.match((4,), p=lambda v: v > 3)
        self.assertIsInstance(hit, MatchResult)
        self.assertEqual((hit.matched, hit.captures), (True, {"x": 4}))
        miss = pattern.match((1,), p=lambda v: v > 3)
        self.assertIsInstance(miss, MatchResult)
        self.assertEqual((miss.matched, miss.captures), (False, {}))


class TestBuiltCaptures(unittest.TestCase):

    def test_sequence_subject_yields_dict_captures(self):
        """
        A simple matcher with two captures returns a dict with both
        slots populated
        """

        m = MATCH(CAPTURE("a"), CAPTURE("b")).build()
        result = demand(m)([7, 8])
        self.assertIsNotNone(result)
        self.assertEqual(dict(result), {"a": 7, "b": 8})


    def test_kospair_subject_yields_datum_captures(self):
        """
        KosPair-backed subjects store captures as Datum with position
        """

        m = MATCH(CAPTURE("head"), REST("tail")).build()
        subj = cons(1, 2, 3, nil)
        result = demand(m)(subj)
        self.assertIsNotNone(result)
        head = result["head"]
        self.assertIsInstance(head, Datum)
        self.assertEqual(head.datum, 1)
        tail = result["tail"]
        self.assertIsInstance(tail, Datum)
        self.assertEqual(tail.datum, cons(2, 3, nil))


    def test_oneof_sparse_captures(self):
        """
        A ONEOF with two branches does not populate keys for the
        non-matching branch
        """

        m = MATCH(ONEOF(
            MATCH(CAPTURE("xs", LITERAL("X"))),
            MATCH(CAPTURE("ys", LITERAL("Y"))))).build()

        r1 = demand(m)(["X"])
        self.assertIsNotNone(r1)
        self.assertEqual(r1["xs"], "X")
        self.assertNotIn("ys", r1)

        r2 = demand(m)(["Y"])
        self.assertIsNotNone(r2)
        self.assertNotIn("xs", r2)
        self.assertEqual(r2["ys"], "Y")


    def test_named_group_nested_dict(self):
        """
        GROUP nodes bind a nested dict under the group slot name
        """

        m = MATCH(GROUP("outer", CAPTURE("inner"))).build()
        result = demand(m)([42])
        self.assertIsNotNone(result)
        self.assertEqual(list(result.keys()), ["outer"])
        inner = result["outer"]
        self.assertIsInstance(inner, dict)
        self.assertEqual(inner["inner"], 42)


# The end.
