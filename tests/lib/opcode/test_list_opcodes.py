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
Tests for list-building opcodes: BUILD_LIST, VAR_LIST_APPEND,
FAST_LIST_APPEND, BUILD_LIST_APPEND.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import koskari.lib as lib


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

    async def test_build_list_append(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", 3],
            ["BUILD_LIST_APPEND", 0],
            ["RETURN", 1],
        ])
        fn = make_callable(code, ns)
        result = await fn()
        self.assertEqual(result, [3])


    async def test_build_list_append_type_error(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", 1],
            ["BUILD_LIST_APPEND", 0],
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
