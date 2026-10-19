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
tests.matcher.test_build_refusals

The patterns Pattern.build refuses, shapes the matcher core does not
lower and LITERAL values the constant pool does not hold, and shapes
near those refusals that it builds

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import cons, demand, keyword, nil, symbol
from koskari.matcher import (
    APP, CAPTURE, CLASS, FIELD, FORM, GROUP, LITERAL, MAP, MAP_ENTRY, MATCH,
    NOT, OPTIONAL, REPEAT, REST, TEST_ANY)


pytestmark = pytest.mark.phase_infra


class TestRefusedAtBuild(unittest.TestCase):

    def test_capture_over_a_zero_width_node(self):
        """
        A CAPTURE over a node that may consume nothing builds; an empty
        span leaves the slot unbound
        """

        m = demand(MATCH(CAPTURE("x", OPTIONAL(TEST_ANY()))).build())
        self.assertEqual(m(()), {})
        self.assertEqual(m((1,)), {"x": 1})
        self.assertIsNone(m((1, 2)))

        m = demand(MATCH(CAPTURE("x", NOT(TEST_ANY()))).build())
        self.assertEqual(m(()), {})
        self.assertIsNone(m((1,)))


    def test_spine_shapes_over_a_single_value(self):
        """
        The sub-pattern of an APP or a map entry tests one value: a
        REST or a REPEAT there builds when its width range includes 1,
        and is refused when its minimum width is above 1
        """

        for inner in (REST(), REPEAT(TEST_ANY())):
            m = demand(MATCH(APP("f", inner)).build({"f": abs}))
            self.assertEqual(m((-3,)), {})
            m = demand(MATCH(MAP(MAP_ENTRY("k", inner))).build())
            self.assertEqual(m(({"k": 1},)), {})
            self.assertIsNone(m(({"j": 1},)))

        m = demand(MATCH(APP("f", REST("r"))).build({"f": abs}))
        self.assertEqual(m((-3,)), {"r": (3,)})
        m = demand(MATCH(MAP(MAP_ENTRY(
            "k", CAPTURE("r", REPEAT(TEST_ANY()))))).build())
        self.assertEqual(m(({"k": 1},)), {"r": [1]})

        for inner in (REST(min_tail=2), REPEAT(TEST_ANY(), min_count=2)):
            with self.assertRaisesRegex(ValueError, "does not include 1"):
                MATCH(APP("f", inner)).build({"f": abs})
            with self.assertRaisesRegex(ValueError, "does not include 1"):
                MATCH(MAP(MAP_ENTRY("k", inner))).build()


    def test_a_group_over_a_single_value(self):
        """
        The sub-pattern of a map entry, an APP or a FIELD is a single
        value walked as a one-element subject, so a GROUP there,
        directly or under a CAPTURE, builds and captures that value
        """

        class P:
            __match_args__ = ("a",)

            def __init__(self, a):
                self.a = a

        shapes = (
            (MATCH(MAP(MAP_ENTRY("k", GROUP("g", CAPTURE("x"))))), {},
             ({"k": 1},), {"g": {"x": 1}}, ({"j": 1},)),
            (MATCH(APP("f", GROUP("g", CAPTURE("x")))), {"f": abs},
             (-3,), {"g": {"x": 3}}, (-3, 4)),
            (MATCH(CLASS("P", FIELD(GROUP("g", CAPTURE("x"))))), {"P": P},
             (P(4),), {"g": {"x": 4}}, (4,)),
            (MATCH(MAP(MAP_ENTRY(
                "k", CAPTURE("y", GROUP("g", CAPTURE("x")))))), {},
             ({"k": 1},), {"y": 1, "g": {"x": 1}}, ({"j": 1},)),
        )
        for pattern, externs, subject, expected, missed in shapes:
            m = demand(pattern.build(externs))
            self.assertEqual(m(subject), expected)
            self.assertIsNone(m(missed))


    def test_rest_counts_past_the_field_limit(self):
        """
        A REST's minimum tail and the fixed suffix after it are fields
        of their own: a minimum tail of 300 and 256 fixed elements
        after a rest build and match, and a minimum tail past 65534 is
        refused when the pattern is built
        """

        m = demand(MATCH(REST(min_tail=300)).build())
        self.assertEqual(m((1,) * 300), {})
        self.assertIsNone(m((1,) * 299))

        anys = [TEST_ANY() for _ in range(256)]
        m = demand(MATCH(REST(), *anys).build())
        self.assertEqual(m((1,) * 256), {})
        self.assertEqual(m((1,) * 300), {})
        self.assertIsNone(m((1,) * 255))

        with self.assertRaises(ValueError) as caught:
            MATCH(REST(min_tail=70000)).build()
        self.assertIn("65534", str(caught.exception))


    def test_optional_over_a_form_holding_a_rest_is_not_the_branch_rest(self):
        """
        A REST inside a form under an OPTIONAL belongs to the form's own
        spine, so two such optionals on one branch build; a REST on the
        optional's own spine beside another REST does not
        """

        MATCH(
            OPTIONAL(FORM(MATCH(LITERAL("else"), REST()))),
            OPTIONAL(FORM(MATCH(LITERAL("finally"), REST())))).build()

        with self.assertRaises(ValueError) as caught:
            MATCH(OPTIONAL(REST()), REST()).build()
        self.assertIn("2 REST", str(caught.exception))


    def test_repeat_under_a_field(self):
        """
        A FIELD tests one attribute value, and a REPEAT has no upper
        bound, so a FIELD over a REPEAT is refused whatever its minimum
        """

        for repeat in (REPEAT(TEST_ANY()), REPEAT(TEST_ANY(), min_count=1)):
            with self.assertRaisesRegex(ValueError, "FIELD at node"):
                MATCH(CLASS("Pt", FIELD(repeat))).build()


class TestLiteralConstants(unittest.TestCase):
    """
    A LITERAL value is a constant of the matcher function, so it must
    be one the constant pool holds
    """

    def test_a_dict_literal_is_refused(self):
        with self.assertRaises(TypeError) as caught:
            MATCH(LITERAL({"a": 1})).build()
        self.assertIn("dict", str(caught.exception))


    def test_a_pair_literal_is_refused(self):
        with self.assertRaises(TypeError) as caught:
            MATCH(LITERAL(cons(1, 2, nil))).build()
        self.assertIn("pair", str(caught.exception))


    def test_constant_literals_match(self):
        for value, other in (
                (7, 8),
                ("s", "t"),
                (symbol("a"), symbol("b")),
                (keyword("k"), keyword("j")),
                ((1, 2), (1, 3))):
            call = demand(MATCH(LITERAL(value)).build())
            self.assertIsNotNone(call((value,)), value)
            self.assertIsNone(call((other,)), value)


# The end.
