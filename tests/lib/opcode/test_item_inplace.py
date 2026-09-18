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
Tests for the in-place item rows: ITEM_INPLACE and ITEM_CONST_INPLACE
with the object on the stack, the VAR rows resolving the object (and
key) by name, and the FAST rows reading them from fast slots. Each
reduces obj[key] in place over its values through the object protocol
and pushes nothing.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import FormalsSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr, opcount


pytestmark = pytest.mark.phase_lib


ADD = Operand.ADD
SUB = Operand.SUBTRACT


def fast_fn(instr, syms):
    code = build_code(instr, local_syms=syms, name="fn")
    base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
    return make_callable(code, base_ns, FormalsSpec(syms))


class TestStackItemInplace(CevalTest):

    async def test_item_inplace_reduces_and_drops(self):
        lst = [1, 2, 3]
        code, ns = build_expr([
            ["CONST", "below"],
            ["CONST", lst],
            ["CONST", 1],
            ["CONST", 10],
            ["CONST", 100],
            ["ITEM_INPLACE", opcount(ADD, 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(lst, [1, 112, 3])


    async def test_item_inplace_mutable_value_in_place(self):
        inner = [1]
        d = {"k": inner}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST", "k"],
            ["CONST", [2, 3]],
            ["ITEM_INPLACE", opcount(ADD, 1)],
            ["CONST", None],
            ["RETURN", 1],
        ])
        await make_callable(code, ns)()
        self.assertIs(d["k"], inner)
        self.assertEqual(inner, [1, 2, 3])


    async def test_item_inplace_errors_propagate(self):
        code, ns = build_expr([
            ["CONST", [1]],
            ["CONST", 5],
            ["CONST", 1],
            ["ITEM_INPLACE", opcount(ADD, 1)],
            ["CONST", None],
            ["RETURN", 1],
        ])
        with self.assertRaises(IndexError):
            await make_callable(code, ns)()
        code, ns = build_expr([
            ["CONST", {"k": 1}],
            ["CONST", "k"],
            ["CONST", "s"],
            ["ITEM_INPLACE", opcount(ADD, 1)],
            ["CONST", None],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


    async def test_const_item_inplace(self):
        d = {"k": 5}
        code, ns = build_expr([
            ["CONST", "below"],
            ["CONST", d],
            ["CONST", 2],
            ["ITEM_CONST_INPLACE", (opcount(SUB, 1), "k", 0, 0)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(d, {"k": 3})
        with self.assertRaises(KeyError):
            code, ns = build_expr([
                ["CONST", {}],
                ["CONST", 2],
                ["ITEM_CONST_INPLACE", (opcount(SUB, 1), "k", 0, 0)],
                ["CONST", None],
                ["RETURN", 1],
            ])
            await make_callable(code, ns)()


class TestVarItemInplace(CevalTest):

    async def test_var_item_inplace(self):
        lst = [1, 2, 3]
        code, ns = build_expr([
            ["CONST", "below"],
            ["CONST", 10],
            ["CONST", 100],
            ["VAR_ITEM_VAR_INPLACE", (opcount(ADD, 2), "obj", "key", 0)],
            ["RETURN", 1],
        ], values={"obj": lst, "key": 2})
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(lst, [1, 2, 113])


    async def test_var_item_const_inplace(self):
        d = {"k": 5}
        code, ns = build_expr([
            ["CONST", 2],
            ["VAR_ITEM_CONST_INPLACE", (opcount(SUB, 1), "obj", "k", 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": d})
        self.assertIsNone(await make_callable(code, ns)())
        self.assertEqual(d, {"k": 3})


    async def test_var_item_inplace_const(self):
        lst = [1, 2, 3]
        code, ns = build_expr([
            ["CONST", "below"],
            ["VAR_ITEM_VAR_INPLACE_CONST", (int(ADD), "obj", "key", 10)],
            ["RETURN", 1],
        ], values={"obj": lst, "key": 0})
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(lst, [11, 2, 3])


    async def test_var_item_const_inplace_const(self):
        d = {"k": 5}
        code, ns = build_expr([
            ["VAR_ITEM_CONST_INPLACE_CONST", (int(SUB), "obj", "k", 2)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": d})
        await make_callable(code, ns)()
        self.assertEqual(d, {"k": 3})


    async def test_var_rows_unbound_and_protocol_errors(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["VAR_ITEM_VAR_INPLACE", (opcount(ADD, 1), "missing", "key", 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"key": 0})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()
        code, ns = build_expr([
            ["VAR_ITEM_CONST_INPLACE_CONST", (int(ADD), "obj", 4, 1)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": [0]})
        with self.assertRaises(IndexError):
            await make_callable(code, ns)()


class TestFastItemInplace(CevalTest):

    async def test_fast_item_inplace(self):
        fn = fast_fn([
            ["CONST", "below"],
            ["FAST", 2],
            ["FAST_ITEM_FAST_INPLACE", (opcount(ADD, 1), 0, 1, 0)],
            ["RETURN", 1],
        ], ("obj", "key", "v"))
        lst = [1, 2, 3]
        self.assertEqual(await fn(lst, 1, 10), "below")
        self.assertEqual(lst, [1, 12, 3])
        d = {"k": 1.5}
        await fn(d, "k", 0.5)
        self.assertEqual(d, {"k": 2.0})
        d = {"k": "a"}
        await fn(d, "k", "b")
        self.assertEqual(d, {"k": "ab"})
        with self.assertRaises(IndexError):
            await fn(lst, 3, 1)
        with self.assertRaises(KeyError):
            await fn({}, "k", 1)
        with self.assertRaises(TypeError):
            await fn((1,), 0, 1)


    async def test_fast_item_inplace_two_values(self):
        fn = fast_fn([
            ["FAST", 2],
            ["FAST", 3],
            ["FAST_ITEM_FAST_INPLACE", (opcount(ADD, 2), 0, 1, 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], ("obj", "key", "a", "b"))
        self.assertEqual(await fn([1], 0, 10, 100), [111])


    async def test_fast_item_const_inplace(self):
        fn = fast_fn([
            ["FAST", 1],
            ["FAST_ITEM_CONST_INPLACE", (opcount(SUB, 1), 0, 1, 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], ("obj", "v"))
        self.assertEqual(await fn([5, 5], 2), [5, 3])
        self.assertEqual(await fn({1: 5}, 2), {1: 3})


    async def test_fast_item_inplace_const(self):
        fn = fast_fn([
            ["CONST", "below"],
            ["FAST_ITEM_FAST_INPLACE_CONST", (int(ADD), 0, 1, 10)],
            ["RETURN", 1],
        ], ("obj", "key"))
        lst = [1, 2]
        self.assertEqual(await fn(lst, -1), "below")
        self.assertEqual(lst, [1, 12])
        d = {"k": 1}
        await fn(d, "k")
        self.assertEqual(d, {"k": 11})


    async def test_fast_item_const_inplace_const(self):
        fn = fast_fn([
            ["FAST_ITEM_CONST_INPLACE_CONST", (int(SUB), 0, "k", 2)],
            ["FAST", 0],
            ["RETURN", 1],
        ], ("obj",))
        self.assertEqual(await fn({"k": 5}), {"k": 3})
        with self.assertRaises(KeyError):
            await fn({})


    async def test_fast_rows_unbound_slot_raises(self):
        fn = fast_fn([
            ["FAST_CLEAR", 1],
            ["FAST_ITEM_FAST_INPLACE_CONST", (int(ADD), 0, 1, 1)],
            ["CONST", None],
            ["RETURN", 1],
        ], ("obj", "key"))
        with self.assertRaises(NameError):
            await fn([1], 0)


# The end.
