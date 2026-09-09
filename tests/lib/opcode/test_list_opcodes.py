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
Tests for list-building opcodes: BUILD_LIST, BUILD_LIST_CONST,
VAR_LIST_APPEND, FAST_LIST_APPEND, BUILD_LIST_APPEND,
BUILD_LIST_APPEND_CONST, BUILD_LIST_EXTEND, BUILD_LIST_EXTEND_CONST.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

import koskari.lib as lib
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestBuildList(CevalTest):

    async def test_build_list_empty(self):
        code, ns = build_expr([
            ["BUILD_LIST", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [])


    async def test_build_list_three_elements(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", 1],
            ["CONST", 2],
            ["BUILD_LIST", 3],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [0, 1, 2])


    async def test_build_list_underrun_raises(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["BUILD_LIST", 3],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(RuntimeError):
            await fn()


class TestVarListAppend(CevalTest):

    async def test_var_list_append(self):
        code, ns = build_expr(
            [
                ["CONST", []],
                ["VAR_STORE", "col"],
                ["CONST", 42],
                ["VAR_LIST_APPEND", "col"],
                ["VAR", "col"],
                ["RETURN", 1],
            ],
            local_syms=("col",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [42])


    async def test_var_list_append_type_error(self):
        code, ns = build_expr(
            [
                ["CONST", 99],
                ["VAR_STORE", "col"],
                ["CONST", 1],
                ["VAR_LIST_APPEND", "col"],
                ["RETURN", 0],
            ],
            local_syms=("col",))
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()


class TestFastListAppend(CevalTest):

    async def test_fast_list_append(self):
        code, ns = build_expr(
            [
                ["CONST", []],
                ["FAST_STORE", 0],
                ["CONST", 7],
                ["FAST_LIST_APPEND", 0],
                ["FAST", 0],
                ["RETURN", 1],
            ],
            local_syms=("col",))
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [7])


class TestBuildListAppend(CevalTest):

    async def test_build_list_append_one(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", 3],
            ["BUILD_LIST_APPEND", 1],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [3])


    async def test_build_list_append_many(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", 3],
            ["CONST", 4],
            ["CONST", 5],
            ["BUILD_LIST_APPEND", 3],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [3, 4, 5])


    async def test_build_list_append_zero_noop(self):
        code, ns = build_expr([
            ["CONST", []],
            ["BUILD_LIST_APPEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [])


    async def test_build_list_append_type_error(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", 1],
            ["BUILD_LIST_APPEND", 1],
            ["RETURN", 0],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()


class TestBuildListConst(CevalTest):

    async def test_build_list_const_tuple(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1, 2, 3)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1, 2, 3])
        self.assertIsInstance(result, list)


    async def test_build_list_const_empty(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", ()],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [])


    async def test_build_list_const_fresh_each_eval(self):
        # each evaluation must mint a new list, never alias the pool tuple
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1, 2)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        first = await fn()
        first.append(99)
        second = await fn()
        self.assertEqual(second, [1, 2])


class TestBuildListExtendConst(CevalTest):

    async def test_extend_const_appends_run(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1, 2)],
            ["BUILD_LIST_EXTEND_CONST", (3, 4, 5)],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1, 2, 3, 4, 5])


    async def test_extend_const_empty(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1,)],
            ["BUILD_LIST_EXTEND_CONST", ()],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1])


    async def test_extend_const_type_error(self):
        code, ns = build_expr([
            ["CONST", 7],
            ["BUILD_LIST_EXTEND_CONST", (1, 2)],
            ["RETURN", 0],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()


class TestBuildListExtend(CevalTest):

    async def test_extend_with_tuple(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1, 2)],
            ["CONST", (3, 4, 5)],
            ["BUILD_LIST_EXTEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1, 2, 3, 4, 5])
        self.assertIsInstance(result, list)


    async def test_extend_with_list(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1,)],
            ["CONST", [2, 3]],
            ["BUILD_LIST_EXTEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1, 2, 3])


    async def test_extend_with_arbitrary_iterable(self):
        # any iterable the list API accepts, not just sequences
        code, ns = build_expr([
            ["BUILD_LIST", 0],
            ["CONST", range(2, 5)],
            ["BUILD_LIST_EXTEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [2, 3, 4])


    async def test_extend_empty(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1,)],
            ["CONST", ()],
            ["BUILD_LIST_EXTEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1])


    async def test_extend_type_error(self):
        code, ns = build_expr([
            ["CONST", 7],
            ["CONST", (1, 2)],
            ["BUILD_LIST_EXTEND", 0],
            ["RETURN", 0],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()


class TestBuildListAppendConst(CevalTest):

    async def test_append_const_one(self):
        code, ns = build_expr([
            ["BUILD_LIST_CONST", (1, 2)],
            ["BUILD_LIST_APPEND_CONST", 3],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [1, 2, 3])


    async def test_append_const_type_error(self):
        code, ns = build_expr([
            ["CONST", 7],
            ["BUILD_LIST_APPEND_CONST", 3],
            ["RETURN", 0],
        ])
        fn = make_callable(code, ns)
        with self.assertRaises(TypeError):
            await fn()


class TestListAppendRuntime(CevalTest):

    def test_list_append_runtime(self):
        col = []
        lib.list_append(col, 5)
        self.assertEqual(col, [5])


    def test_list_append_runtime_type_error(self):
        with self.assertRaises(TypeError):
            lib.list_append(42, 1)


# The end.
