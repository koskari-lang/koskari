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
formal, the subject; the captures on a match and None on a miss; a
missing extern refused at build

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import Function, demand
from koskari.matcher import (
    APP, CAPTURE, GROUP, MATCH, PREDICATE, TYPE, VALUE, make_matcher)


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


    def test_one_missing_extern_among_given_ones_is_refused(self):
        pattern = MATCH(PREDICATE("p"), TYPE("T"), APP("f", CAPTURE("x")))
        with self.assertRaises(TypeError) as caught:
            pattern.build({"p": bool, "f": abs})
        self.assertIn("T", str(caught.exception))
        m = demand(pattern.build({"p": bool, "f": abs, "T": int}))
        self.assertEqual(m((1, 2, -3)), {"x": 3})


class TestBuiltCaptures(unittest.TestCase):

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
