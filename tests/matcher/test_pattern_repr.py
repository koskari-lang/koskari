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
Tests for matcher Pattern __repr__ and __kos_repr__

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import kos_repr, nil
from koskari.matcher import (
    CAPTURE, LITERAL, MATCH, REPEAT, TEST_ANY, TYPE,
    parse_explicit_rule_pattern)
from koskari.matcher.heads import lookup_matcher, parse_matcher
from koskari.reader.parser import default_reader, source_str
from koskari.syntax import parse_match_pattern
from koskari.types import is_pair, is_proper, is_symbol


pytestmark = pytest.mark.phase_infra


def read_pattern_src(src):
    """
    Read a matcher pattern source string to a koskari form
    """

    return default_reader.read(source_str(src, '<test>'))


def parse_pattern_form(form):
    """
    Parse a read form to a Pattern using meta or explicit rule lowering
    """

    if is_pair(form) and is_proper(form):
        head, tail = form
        if tail is not nil and is_pair(tail):
            if lookup_matcher(str(head)) is not None and is_symbol(tail.car):
                return parse_match_pattern(form)
    return parse_matcher(form)


class TestPatternKosReprRoundTrip(unittest.TestCase):
    """
    kos_repr(pattern) round-trips reader source for supported forms
    """


    def assert_kos_roundtrip(self, src):
        pat = parse_pattern_form(read_pattern_src(src))
        self.assertEqual(kos_repr(pat), src)


    def test_bare_capture(self):
        self.assert_kos_roundtrip('x')


    def test_named_capture_type_constraint(self):
        self.assert_kos_roundtrip('(name :: symbol)')


    def test_named_capture_literal_constraint(self):
        self.assert_kos_roundtrip('(x :: (LITERAL 100))')


    def test_form_match(self):
        self.assert_kos_roundtrip('(FORM (MATCH a b))')


    def test_anon_type_constraint(self):
        self.assert_kos_roundtrip('(:: symbol)')


    def test_not_literal_kw(self):
        self.assert_kos_roundtrip('(NOT *:)')


    def test_explicit_spine(self):
        self.assert_kos_roundtrip('(MATCH foo bar)')


    def test_symbol_decl_lowers_to_capture_type(self):
        """
        (SYMBOL v) registry decl lowers to CAPTURE with TYPE symbol
        """

        pat = parse_pattern_form(read_pattern_src('(SYMBOL v)'))
        self.assertEqual(kos_repr(pat), '(v :: symbol)')


    def test_repeat_slotted(self):
        self.assert_kos_roundtrip('(REPEAT xs e min: 2)')


    def test_rest_slotted(self):
        self.assert_kos_roundtrip('(REST tail)')


    def test_value_decl(self):
        self.assert_kos_roundtrip('(VALUE slot)')


    def test_binding_decl(self):
        self.assert_kos_roundtrip('(BINDING slot)')


class TestPatternReprSpotCheck(unittest.TestCase):
    """
    Spot checks for repr and kos_repr on constructed patterns
    """


    def test_capture_repr(self):
        pat = CAPTURE('x', LITERAL(100))
        self.assertEqual(repr(pat), 'CAPTURE(\'x\', LITERAL(100))')


    def test_capture_kos_literal(self):
        pat = CAPTURE('x', LITERAL(100))
        self.assertEqual(kos_repr(pat), '(x :: (LITERAL 100))')


    def test_repeat_capture_kos(self):
        pat = CAPTURE('xs', REPEAT(TEST_ANY()))
        self.assertEqual(kos_repr(pat), '(REPEAT xs (TEST_ANY))')


    def test_type_kos(self):
        pat = TYPE('symbol')
        self.assertEqual(kos_repr(pat), '(:: symbol)')


    def test_match_explicit_kos(self):
        pat = parse_explicit_rule_pattern(read_pattern_src('(a b)'))
        self.assertEqual(kos_repr(pat), '(MATCH a b)')


# The end.
