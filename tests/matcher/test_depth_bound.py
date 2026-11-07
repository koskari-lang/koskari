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
tests.matcher.test_depth_bound

A pattern is lowered and its function compiled when it is built, and
the lowering recurses in Python once per level of nesting under the
interpreter's recursion limit: a pattern nested past it raises
RecursionError at build, as nested Python containers do, rather than
overflowing the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import demand
from koskari.matcher import FORM, MATCH, NOT, TEST_ANY
from tests import needs_native_stack


pytestmark = pytest.mark.phase_infra


def nested_forms(depth, under_not=False):
    """
    A FORM chain depth deep over TEST ANY; under_not wraps the chain
    in a NOT
    """

    pattern = TEST_ANY()
    for _ in range(depth):
        pattern = FORM(MATCH(pattern))
    if under_not:
        pattern = NOT(pattern)
    return MATCH(pattern)


def nested_lists(depth):
    subj = [0]
    for _ in range(depth):
        subj = [subj]
    return subj


class TestMatcherDepthBound(unittest.TestCase):

    @needs_native_stack
    def test_a_pattern_nested_past_the_limit_is_refused_at_build(self):
        """
        A pattern nested far past the recursion limit is refused with
        RecursionError when it is built; it never overflows the stack
        """

        pattern = nested_forms(60000)
        with self.assertRaises(RecursionError):
            pattern.build()


    def test_a_pattern_within_the_limit_matches(self):
        call = demand(nested_forms(50).build())
        self.assertIsNotNone(call(nested_lists(50)))
        self.assertIsNone(call(nested_lists(49)))
        nested_forms(50, under_not=True).build()


# The end.
