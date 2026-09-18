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
Tests for take opcodes: VAR_TAKE, FAST_TAKE, ATTR_TAKE, ITEM_TAKE,
ITEM_CONST_TAKE, VAR_ATTR_TAKE.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from types import SimpleNamespace

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestFastTake(CevalTest):

    async def test_fast_take_returns_and_clears(self):
        code, ns = build_expr(
            [
                ["CONST", 42],
                ["FAST_STORE", 0],
                ["FAST_TAKE", 0],
                ["RETURN", 1],
            ],
            local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_fast_take_empty_slot_raises(self):
        code, ns = build_expr(
            [
                ["FAST_TAKE", 0],
                ["RETURN", 1],
            ],
            local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_fast_clear_on_empty_slot_succeeds(self):
        code, ns = build_expr(
            [
                ["FAST_CLEAR", 0],
                ["CONST", None],
                ["RETURN", 1],
            ],
            local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertIsNone(result)


    async def test_second_fast_take_raises(self):
        code, ns = build_expr(
            [
                ["CONST", 1],
                ["FAST_STORE", 0],
                ["FAST_TAKE", 0],
                ["POP", 1],
                ["FAST_TAKE", 0],
                ["RETURN", 1],
            ],
            local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestVarTake(CevalTest):

    async def test_var_take_returns_and_clears(self):
        code, ns = build_expr(
            [
                ["VAR_TAKE", "x"],
                ["RETURN", 1],
            ],
            local_syms=("x",),
            values={"x": 99})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


class TestAttrTake(CevalTest):

    async def test_attr_take_returns_and_removes(self):
        obj = SimpleNamespace(temp=10)
        code, ns = build_expr([
            ["CONST", obj],
            ["ATTR_TAKE", "temp"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)
        self.assertFalse(hasattr(obj, "temp"))


    async def test_var_attr_take(self):
        obj = SimpleNamespace(temp=20)
        code, ns = build_expr(
            [
                ["VAR_ATTR_TAKE", ("obj", "temp", 0, 0)],
                ["RETURN", 1],
            ],
            local_syms=("obj",),
            values={"obj": obj})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 20)
        self.assertFalse(hasattr(obj, "temp"))


class TestItemTake(CevalTest):

    async def test_const_item_take(self):
        data = {"k": 7}
        code, ns = build_expr([
            ["CONST", data],
            ["ITEM_CONST_TAKE", "k"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 7)
        self.assertNotIn("k", data)


    async def test_item_take(self):
        data = {"a": 3}
        code, ns = build_expr([
            ["CONST", data],
            ["CONST", "a"],
            ["ITEM_TAKE", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 3)
        self.assertNotIn("a", data)


# The end.
