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

The patterns Pattern.build refuses: shapes the lowering cannot emit,
and LITERAL values the constant pool does not hold

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
        A CAPTURE over a node that may consume nothing has no span to
        bind: OPTIONAL and NOT under a CAPTURE are refused
        """

        for inner in (OPTIONAL(TEST_ANY()), NOT(TEST_ANY())):
            with self.assertRaises(ValueError):
                MATCH(CAPTURE("x", inner)).build()


    def test_spine_shapes_over_a_single_value(self):
        """
        The sub-pattern of an APP or a map entry tests one value, so a
        REST or a REPEAT there is refused
        """

        for inner in (REST(), REPEAT(TEST_ANY())):
            with self.assertRaises(ValueError):
                MATCH(APP("f", inner)).build({"f": abs})
            with self.assertRaises(ValueError):
                MATCH(MAP(MAP_ENTRY("k", inner))).build()


    def test_a_group_over_a_single_value(self):
        """
        The sub-pattern of a map entry, an APP or a FIELD tests one
        value with no spine under it, so a GROUP there, directly or
        under a CAPTURE, is refused
        """

        shapes = (
            (MATCH(MAP(MAP_ENTRY("k", GROUP("g", CAPTURE("x"))))), {}),
            (MATCH(APP("f", GROUP("g", CAPTURE("x")))), {"f": abs}),
            (MATCH(CLASS("P", FIELD(GROUP("g", CAPTURE("x"))))),
             {"P": object}),
            (MATCH(MAP(MAP_ENTRY(
                "k", CAPTURE("y", GROUP("g", CAPTURE("x")))))), {}),
        )
        for pattern, externs in shapes:
            with self.assertRaisesRegex(ValueError, "holds a GROUP"):
                pattern.build(externs)


    def test_rest_counts_past_the_packed_limit(self):
        """
        A REST row packs its minimum tail and the width of the fixed
        suffix after it into eight bits each, so a minimum tail past
        255, or more than 255 fixed elements after a rest, is refused
        when the pattern is built
        """

        with self.assertRaises(ValueError) as caught:
            MATCH(REST(min_tail=300)).build()
        self.assertIn("255", str(caught.exception))

        anys = [TEST_ANY() for _ in range(256)]
        with self.assertRaises(ValueError) as caught:
            MATCH(REST(), *anys).build()
        self.assertIn("255", str(caught.exception))

        anys = [TEST_ANY() for _ in range(255)]
        MATCH(REST(min_tail=255), *anys).build()


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
