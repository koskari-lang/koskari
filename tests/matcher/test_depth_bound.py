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

The matcher walks a pattern's nesting on the C stack, in validation
and in matching, under the interpreter's recursion guard: a pattern
nested past the limit raises RecursionError, as nested Python
containers do, rather than overflowing the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.matcher import Matcher


pytestmark = pytest.mark.phase_infra


def nested_forms(depth, under_not=False):
    """
    A FORM chain depth deep over TEST ANY; under_not wraps the chain
    in a NOT, whose validation walks the chain
    """

    nodes = [("TEST", "ANY", -1)] + [("FORM", i) for i in range(depth)]
    root = depth
    if under_not:
        nodes.append(("NOT", depth))
        root = depth + 1
    return ("MATCHER", (), (), (), tuple(nodes), root)


def nested_lists(depth):
    subj = [0]
    for _ in range(depth):
        subj = [subj]
    return subj


class TestMatcherDepthBound(unittest.TestCase):

    def test_matching_a_pattern_nested_past_the_limit_never_overflows(self):
        """
        The match walk recurses once per level of pattern nesting under
        the interpreter's recursion guard, so a pattern nested far past
        any counted limit is refused with RecursionError where the
        interpreter counts C recursion (3.12, 3.13) and completes, or is
        refused at the stack margin, where it guards the stack itself
        (3.14); it never overflows the stack
        """

        m = Matcher.from_tuple(nested_forms(60000))
        try:
            result = m.match(nested_lists(60000))
        except RecursionError:
            return
        self.assertTrue(result.matched)

    def test_validating_a_pattern_nested_past_the_limit_never_overflows(self):
        """
        The validation walks are guarded the same way, so a deep
        pattern under a NOT is refused or validated, never a crash
        """

        try:
            m = Matcher.from_tuple(nested_forms(60000, under_not=True))
        except RecursionError:
            return
        self.assertIsInstance(m, Matcher)

    def test_a_pattern_within_the_limit_matches(self):
        m = Matcher.from_tuple(nested_forms(50))
        self.assertTrue(m.match(nested_lists(50)).matched)
        self.assertFalse(m.match(nested_lists(49)).matched)
        Matcher.from_tuple(nested_forms(50, under_not=True))


# The end.
