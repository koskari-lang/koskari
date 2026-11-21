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
Tests for koskari.solve: amb, require, fail, solve, solve-first.

The search shapes live in data/solve_cases.ki and run at import time;
these tests import that module through the koskari import hook and
assert the collected results. Because koskari.solve is written in
Koskari, the full stack (reader, compiler, importer, bootstrap) must be
available, so these run in the reproducers phase.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import importlib
import os
import sys
import unittest

import pytest

from koskari.importer import install


pytestmark = pytest.mark.phase_reproducers

_DATA_DIR = os.path.join(os.path.dirname(__file__), "data")


class TestSolveModule(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        install()
        sys.path.insert(0, _DATA_DIR)
        sys.modules.pop("solve_cases", None)
        cls.cases = importlib.import_module("solve_cases")

    @classmethod
    def tearDownClass(cls):
        sys.modules.pop("solve_cases", None)
        sys.path.remove(_DATA_DIR)

    def test_solve_collects_all_solutions_in_choice_order(self):
        self.assertEqual(self.cases.triples,
                         [(3, 4, 5), (5, 12, 13), (6, 8, 10)])

    def test_solve_first_returns_the_first_solution(self):
        self.assertEqual(getattr(self.cases, "first-triple"), (3, 4, 5))

    def test_solve_first_returns_none_when_exhausted(self):
        self.assertIsNone(getattr(self.cases, "no-solution"))

    def test_fail_abandons_every_branch(self):
        self.assertEqual(getattr(self.cases, "all-failing"), [])

    def test_nested_solve_is_independent_of_outer_stop(self):
        self.assertEqual(self.cases.nested, (2, [12, 22]))

    def test_solve_first_skips_choices_after_the_solution(self):
        self.assertEqual(getattr(self.cases, "first-passing"), 2)
        self.assertEqual(self.cases.visits, [1, 2])

    def test_amb_outside_a_search_raises_no_active_search(self):
        import koskari.solve as solve
        from koskari.exceptions import KoskariContinuationError

        err = getattr(self.cases, "outside-amb")
        self.assertIsInstance(err, solve.NoActiveSearch)
        self.assertIsInstance(err, solve.SearchError)
        self.assertIsInstance(err, KoskariContinuationError)
        self.assertIn("solve", str(err))

    def test_fail_outside_a_search_raises_no_active_search(self):
        import koskari.solve as solve

        err = getattr(self.cases, "outside-fail")
        self.assertIsInstance(err, solve.NoActiveSearch)
        self.assertIn("solve", str(err))

    def test_reset_in_the_thunk_does_not_delimit_the_search(self):
        self.assertEqual(getattr(self.cases, "reset-in-thunk"), [101, 102])

    def test_reset_alone_is_not_a_search(self):
        import koskari.solve as solve

        err = getattr(self.cases, "reset-without-search")
        self.assertIsInstance(err, solve.NoActiveSearch)

    def test_plain_shift_in_the_thunk_captures_past_the_search(self):
        self.assertEqual(getattr(self.cases, "shift-past-search"),
                         ("handler", [15]))

    def test_plain_shift_with_no_reset_is_not_a_search_error(self):
        import koskari.solve as solve
        from koskari.exceptions import KoskariPromptNotFound

        err = getattr(self.cases, "shift-without-reset")
        self.assertIsInstance(err, KoskariPromptNotFound)
        self.assertNotIsInstance(err, solve.NoActiveSearch)
        self.assertIsNone(err.tag)

# The end.
