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
Tests for argument-building opcodes: BUILD_ARGS, BUILD_KWDS, BUILD_KWDS_NAMES,
COMBINE_ARGS, COMBINE_KWDS.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestBuildArgs(CevalTest):

    async def test_build_args_empty(self):
        code, ns = build_expr([
            ["BUILD_ARGS", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, ())

    async def test_build_args_five_elements(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["CONST", 4],
            ["BUILD_ARGS", 5],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, (0, 1, 2, 3, 4))

    async def test_build_args_underrun_raises(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["BUILD_ARGS", 5],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(RuntimeError):
            run_to_completion(fn())


class TestBuildKwds(CevalTest):

    async def test_build_kwds_empty(self):
        code, ns = build_expr([
            ["BUILD_KWDS", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {})

    async def test_build_kwds_two_pairs(self):
        code, ns = build_expr([
            ["CONST", "foo"],
            ["CONST", 1],
            ["CONST", "bar"],
            ["CONST", 2],
            ["BUILD_KWDS", 2],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {"foo": 1, "bar": 2})


class TestBuildKwdsNames(CevalTest):

    async def test_build_kwds_names_empty(self):
        code, ns = build_expr([
            ["CONST", ()],
            ["BUILD_KWDS_NAMES", ()],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {})

    async def test_build_kwds_names_two_names(self):
        code, ns = build_expr([
            ["CONST", ("foo", "bar")],
            ["CONST", 1],
            ["CONST", 2],
            ["BUILD_KWDS_NAMES", ("foo", "bar")],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {"foo": 1, "bar": 2})


class TestCombineArgs(CevalTest):

    async def test_combine_args_empty(self):
        code, ns = build_expr([
            ["COMBINE_ARGS", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, ())

    async def test_combine_args_single_tuple(self):
        code, ns = build_expr([
            ["CONST", (0, 1, 2, 3, 4)],
            ["COMBINE_ARGS", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, (0, 1, 2, 3, 4))

    async def test_combine_args_single_range(self):
        code, ns = build_expr([
            ["CONST", range(5)],
            ["COMBINE_ARGS", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, (0, 1, 2, 3, 4))

    async def test_combine_args_two_tuples(self):
        code, ns = build_expr([
            ["CONST", (0, 1)],
            ["CONST", (2, 3)],
            ["COMBINE_ARGS", 2],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, (0, 1, 2, 3))

    async def test_combine_args_tuple_and_range(self):
        code, ns = build_expr([
            ["CONST", range(5)],
            ["CONST", (9, 8)],
            ["COMBINE_ARGS", 2],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, (0, 1, 2, 3, 4, 9, 8))


class TestCombineKwds(CevalTest):

    async def test_combine_kwds_empty(self):
        code, ns = build_expr([
            ["COMBINE_KWDS", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {})

    async def test_combine_kwds_single_dict(self):
        code, ns = build_expr([
            ["CONST", {"a": 1, "b": 2}],
            ["COMBINE_KWDS", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {"a": 1, "b": 2})

    async def test_combine_kwds_two_dicts_later_overwrites(self):
        code, ns = build_expr([
            ["CONST", {"a": 1, "c": 2}],
            ["CONST", {"b": 2, "c": 3}],
            ["COMBINE_KWDS", 2],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, {"a": 1, "b": 2, "c": 3})


# The end.
