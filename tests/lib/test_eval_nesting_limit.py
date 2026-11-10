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
tests.lib.test_eval_nesting_limit

Every entry into the evaluator from Python nests on the C stack, and
the thread's count of active contexts is bounded by a process-wide
limit, KOS_EVAL_NESTING_MAX until lowered through
koskari.lib.set_eval_nesting_limit. An entry past the limit raises
RecursionError naming the limit in force, into the caller, where an
ordinary handler takes it. koskari.lib.build_flags reports the cap.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import (
    build_flags, get_eval_nesting_limit, set_eval_nesting_limit)
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


EVAL_NESTING_MAX = build_flags["KOS_EVAL_NESTING_MAX"]


# each round trip leaves the evaluator through bounce, a Python frame,
# and re-enters it through the demand-wrapped step, so one round trip
# is one more nested entry
SRC = """
(import-from koskari.lib demand)

(define [step n]
  (if (<= n 0) 0 (+ 1 (bounce (demand step) (- n 1)))))

(define [run n] (step n))
"""


class TestEvalNestingLimitAccess(unittest.TestCase):

    def setUp(self):
        self.saved = get_eval_nesting_limit()


    def tearDown(self):
        set_eval_nesting_limit(self.saved)


    def test_default(self):
        self.assertEqual(get_eval_nesting_limit(), EVAL_NESTING_MAX)


    def test_round_trip(self):
        for n in (1, 80, EVAL_NESTING_MAX):
            with self.subTest(n=n):
                self.assertIsNone(set_eval_nesting_limit(n))
                self.assertEqual(get_eval_nesting_limit(), n)


    def test_out_of_range_refused(self):
        set_eval_nesting_limit(50)
        for n in (0, EVAL_NESTING_MAX + 1, -1, 2 ** 80):
            with self.subTest(n=n):
                with self.assertRaises(ValueError):
                    set_eval_nesting_limit(n)
                self.assertEqual(get_eval_nesting_limit(), 50)


    def test_non_int_refused(self):
        set_eval_nesting_limit(50)
        for n in (50.0, "50", None):
            with self.subTest(n=n):
                with self.assertRaises(TypeError):
                    set_eval_nesting_limit(n)
                self.assertEqual(get_eval_nesting_limit(), 50)


class TestEvalNestingLimitInForce(unittest.IsolatedAsyncioTestCase):

    async def load(self, seen):
        module = new_module("eval_nesting_limit")
        vars(module)["__file__"] = "<eval_nesting_limit>"

        def bounce(f, n):
            seen.append(n)
            return f(n)

        vars(module)["bounce"] = bounce
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_round_trips_under_the_limit_complete(self):
        seen = []
        mod = await self.load(seen)
        saved = get_eval_nesting_limit()
        try:
            set_eval_nesting_limit(40)
            self.assertEqual(await mod["run"](30), 30)
        finally:
            set_eval_nesting_limit(saved)
        self.assertEqual(len(seen), 30)


    async def test_lowered_limit_in_message(self):
        seen = []
        mod = await self.load(seen)
        saved = get_eval_nesting_limit()
        try:
            set_eval_nesting_limit(40)
            with self.assertRaisesRegex(
                    RecursionError,
                    r"^Koskari eval nesting exceeded \(40\)$"):
                await mod["run"](200)
        finally:
            set_eval_nesting_limit(saved)
        self.assertGreater(len(seen), 1)
        self.assertLessEqual(len(seen), 40)


    async def test_refused_entry_is_catchable_by_the_caller(self):
        """
        the error raises into the Python frame that made the refused
        entry; a Python handler around that call takes it and the
        thread evaluates again afterwards
        """

        seen = []
        mod = await self.load(seen)
        caught = []

        def guarded(f, n):
            try:
                return f(n)
            except RecursionError as e:
                caught.append(str(e))
                return 0

        mod["bounce"] = guarded
        saved = get_eval_nesting_limit()
        try:
            set_eval_nesting_limit(10)
            result = await mod["run"](200)
        finally:
            set_eval_nesting_limit(saved)
        self.assertEqual(caught, ["Koskari eval nesting exceeded (10)"])
        self.assertLess(result, 200)
        self.assertEqual(await mod["run"](3), 3)


# The end.
