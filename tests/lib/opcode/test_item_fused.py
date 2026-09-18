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
Tests for the variable-tier item rows: VAR_ITEM, VAR_CONST_ITEM,
VAR_ITEM_STORE, VAR_CONST_ITEM_STORE and their FAST twins. The VAR rows
resolve the object (and key) by name in the frame namespace; the FAST
rows read them from fast slots. Loads push the item and pop nothing;
stores pop the value and push nothing.

EXTRA schema (VAR, VAR|CONST, NOARG, NOARG) or (FASTVAR, FASTVAR|CONST,
NOARG, NOARG).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import FormalsSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


pytestmark = pytest.mark.phase_lib


def fast_fn(instr, syms):
    """
    A function over the fast locals syms running instr
    """

    code = build_code(instr, local_syms=syms, name="fn")
    base_ns = build_expr([["CONST", None], ["RETURN", 1]])[1]
    return make_callable(code, base_ns, FormalsSpec(syms))


class TestVarItemOpcodes(CevalTest):

    async def test_var_item_loads_by_name(self):
        code, ns = build_expr([
            ["VAR_ITEM", ("obj", "key", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": {"a": 1, "b": 2}, "key": "b"})
        self.assertEqual(await make_callable(code, ns)(), 2)


    async def test_var_item_leaves_stack_alone(self):
        code, ns = build_expr([
            ["CONST", "below"],
            ["VAR_ITEM", ("obj", "key", 0, 0)],
            ["POP", 1],
            ["RETURN", 1],
        ], values={"obj": [10, 20, 30], "key": 1})
        self.assertEqual(await make_callable(code, ns)(), "below")


    async def test_var_item_unbound_object_raises(self):
        code, ns = build_expr([
            ["VAR_ITEM", ("missing", "key", 0, 0)],
            ["RETURN", 1],
        ], values={"key": 0})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_var_item_unbound_key_raises(self):
        code, ns = build_expr([
            ["VAR_ITEM", ("obj", "missing", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": [1]})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_var_item_index_error_propagates(self):
        code, ns = build_expr([
            ["VAR_ITEM", ("obj", "key", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": [1, 2], "key": 5})
        with self.assertRaises(IndexError):
            await make_callable(code, ns)()


    async def test_var_const_item_loads(self):
        code, ns = build_expr([
            ["VAR_CONST_ITEM", ("obj", "k", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": {"k": "v"}})
        self.assertEqual(await make_callable(code, ns)(), "v")


    async def test_var_const_item_key_error_propagates(self):
        code, ns = build_expr([
            ["VAR_CONST_ITEM", ("obj", "nope", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": {"k": "v"}})
        with self.assertRaises(KeyError):
            await make_callable(code, ns)()


    async def test_var_item_store_pops_value_pushes_nothing(self):
        lst = [0, 0, 0]
        code, ns = build_expr([
            ["CONST", "below"],
            ["CONST", 99],
            ["VAR_ITEM_STORE", ("obj", "key", 0, 0)],
            ["RETURN", 1],
        ], values={"obj": lst, "key": 2})
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(lst, [0, 0, 99])


    async def test_var_const_item_store(self):
        d = {}
        code, ns = build_expr([
            ["CONST", 7],
            ["VAR_CONST_ITEM_STORE", ("obj", "k", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": d})
        self.assertIsNone(await make_callable(code, ns)())
        self.assertEqual(d, {"k": 7})


    async def test_var_item_store_unbound_object_raises(self):
        code, ns = build_expr([
            ["CONST", 7],
            ["VAR_ITEM_STORE", ("missing", "key", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"key": 0})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_var_item_store_type_error_propagates(self):
        code, ns = build_expr([
            ["CONST", 7],
            ["VAR_ITEM_STORE", ("obj", "key", 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": (1, 2), "key": 0})
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


class TestFastItemOpcodes(CevalTest):

    async def test_fast_item_loads_from_slots(self):
        fn = fast_fn([
            ["FAST_ITEM", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj", "key"))
        self.assertEqual(await fn([10, 20, 30], 1), 20)
        self.assertEqual(await fn((10, 20, 30), -1), 30)
        self.assertEqual(await fn({"a": 1}, "a"), 1)


    async def test_fast_item_leaves_stack_alone(self):
        fn = fast_fn([
            ["CONST", "below"],
            ["FAST_ITEM", (0, 1, 0, 0)],
            ["POP", 1],
            ["RETURN", 1],
        ], ("obj", "key"))
        self.assertEqual(await fn([1], 0), "below")


    async def test_fast_item_unbound_slot_raises(self):
        fn = fast_fn([
            ["FAST_CLEAR", 1],
            ["FAST_ITEM", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj", "key"))
        with self.assertRaises(NameError):
            await fn([1], 0)


    async def test_fast_item_errors_propagate(self):
        fn = fast_fn([
            ["FAST_ITEM", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj", "key"))
        with self.assertRaises(IndexError):
            await fn([1, 2], 2)
        with self.assertRaises(KeyError):
            await fn({}, "x")
        with self.assertRaises(TypeError):
            await fn(42, 0)


    async def test_fast_const_item_loads(self):
        fn = fast_fn([
            ["FAST_CONST_ITEM", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj",))
        self.assertEqual(await fn([10, 20, 30]), 20)
        self.assertEqual(await fn("abc"), "b")


    async def test_fast_item_store_pops_value_pushes_nothing(self):
        fn = fast_fn([
            ["CONST", "below"],
            ["FAST", 2],
            ["FAST_ITEM_STORE", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj", "key", "val"))
        lst = [0, 0]
        self.assertEqual(await fn(lst, 1, 5), "below")
        self.assertEqual(lst, [0, 5])
        d = {}
        await fn(d, "k", 5)
        self.assertEqual(d, {"k": 5})


    async def test_fast_const_item_store(self):
        fn = fast_fn([
            ["FAST", 1],
            ["FAST_CONST_ITEM_STORE", (0, "k", 0, 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], ("obj", "val"))
        self.assertEqual(await fn({}, 1), {"k": 1})


    async def test_fast_item_store_errors_propagate(self):
        fn = fast_fn([
            ["FAST", 2],
            ["FAST_ITEM_STORE", (0, 1, 0, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], ("obj", "key", "val"))
        with self.assertRaises(IndexError):
            await fn([1], 3, 0)
        with self.assertRaises(TypeError):
            await fn((1,), 0, 0)
        with self.assertRaises(TypeError):
            await fn({}, [], 0)


class TestItemStoreConstOpcodes(CevalTest):
    """
    The stack-free stores: the value is the row's pool constant
    """

    async def test_var_item_store_const(self):
        lst = [0, 0, 0]
        code, ns = build_expr([
            ["CONST", "below"],
            ["VAR_ITEM_STORE_CONST", ("obj", "key", 99, 0)],
            ["RETURN", 1],
        ], values={"obj": lst, "key": 1})
        self.assertEqual(await make_callable(code, ns)(), "below")
        self.assertEqual(lst, [0, 99, 0])


    async def test_var_const_item_store_const(self):
        d = {}
        code, ns = build_expr([
            ["VAR_CONST_ITEM_STORE_CONST", ("obj", "k", 7, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": d})
        self.assertIsNone(await make_callable(code, ns)())
        self.assertEqual(d, {"k": 7})


    async def test_var_item_store_const_errors(self):
        code, ns = build_expr([
            ["VAR_ITEM_STORE_CONST", ("missing", "key", 1, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"key": 0})
        with self.assertRaises(NameError):
            await make_callable(code, ns)()
        code, ns = build_expr([
            ["VAR_CONST_ITEM_STORE_CONST", ("obj", 5, 1, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], values={"obj": [0]})
        with self.assertRaises(IndexError):
            await make_callable(code, ns)()


    async def test_fast_item_store_const(self):
        fn = fast_fn([
            ["CONST", "below"],
            ["FAST_ITEM_STORE_CONST", (0, 1, "v", 0)],
            ["RETURN", 1],
        ], ("obj", "key"))
        lst = [0, 0]
        self.assertEqual(await fn(lst, -1), "below")
        self.assertEqual(lst, [0, "v"])
        d = {}
        await fn(d, "k")
        self.assertEqual(d, {"k": "v"})
        with self.assertRaises(IndexError):
            await fn([0], 4)
        with self.assertRaises(TypeError):
            await fn((0,), 0)


    async def test_fast_const_item_store_const(self):
        fn = fast_fn([
            ["FAST_CONST_ITEM_STORE_CONST", (0, 1, "v", 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], ("obj",))
        self.assertEqual(await fn([0, 0, 0]), [0, "v", 0])
        self.assertEqual(await fn({}), {1: "v"})
        with self.assertRaises(IndexError):
            await fn([0])


# The end.
