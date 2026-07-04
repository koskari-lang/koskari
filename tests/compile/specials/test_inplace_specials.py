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
Tests for koskari.compile compile/inplace-item and compile/inplace-attr
special forms.
"""


from types import SimpleNamespace

from koskari.exceptions import SpecialError
from koskari.lib import OpCode

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


def _ops(code):
    return [code.oparg(i) for i in range(code.length())]


def _opcodes(code):
    return [op for op, _ in _ops(code)]


def _count_op(code, opcode):
    return sum(1 for op, _ in _ops(code) if op == opcode)


class TestInplaceItemSpecial(SpecialsTestCase):

    async def test_inplace_item_constant_key(self):
        d = {"k": 10}
        src_str = """
        (compile/begin
          (compile/inplace-item + d "k" 1)
          (compile/item d "k"))
        """
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, d["k"])


    async def test_inplace_item_variable_key(self):
        lst = [1, 2, 3]
        src_str = """
        (compile/let [[i 1]]
          (compile/begin
            (compile/inplace-item + lst i 10)
            (compile/item lst i)))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(12, result)
        self.assertEqual(12, lst[1])


    async def test_inplace_item_returns_none(self):
        d = {"k": 0}
        src_str = '(compile/inplace-item + d "k" 1)'
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(1, d["k"])


    async def test_inplace_item_subtract(self):
        d = {"k": 10}
        src_str = """
        (compile/begin
          (compile/inplace-item - d "k" 3)
          (compile/item d "k"))
        """
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertEqual(7, result)
        self.assertEqual(7, d["k"])


    async def test_inplace_item_evaluates_obj_once(self):
        calls = {"n": 0}

        def make_d():
            calls["n"] += 1
            return {"k": 10}

        src_str = '(compile/inplace-item + (make_d) "k" 1)'
        expr, mod = self.compile_expr(src_str, make_d=make_d)
        await expr()
        self.assertEqual(1, calls["n"])


    async def test_inplace_item_evaluates_key_once(self):
        calls = {"n": 0}
        lst = [0, 10, 0]

        def keyfn():
            calls["n"] += 1
            return 1

        src_str = "(compile/inplace-item + lst (keyfn) 5)"
        expr, mod = self.compile_expr(src_str, lst=lst, keyfn=keyfn)
        await expr()
        self.assertEqual(1, calls["n"])
        self.assertEqual(15, lst[1])


    async def test_inplace_item_const_key_emits_dup_and_const_item_store(self):
        code = self.compile_to_code('(compile/inplace-item + d "k" 1)', d={})
        self.assertEqual(1, _count_op(code, OpCode.KOS_OP_DUP))
        self.assertIn(OpCode.KOS_OP_CONST_ITEM, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_CONST_ITEM_STORE, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_OBJECT_INPLACE_ADD, _opcodes(code))


    async def test_inplace_item_var_key_emits_dup_and_item_store(self):
        code = self.compile_to_code("(compile/inplace-item + lst i 1)", lst=[], i=0)
        self.assertEqual(1, _count_op(code, OpCode.KOS_OP_DUP))
        self.assertIn(OpCode.KOS_OP_ITEM, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_ITEM_STORE, _opcodes(code))
        self.assertNotIn(OpCode.KOS_OP_CONST_ITEM, _opcodes(code))


    async def test_inplace_item_mutates_in_place(self):
        """
        inplace-item on a list-valued slot extends the stored list
        rather than replacing it with a copy
        """

        inner = [1, 2]
        d = {"k": inner}
        src_str = '(compile/inplace-item + d "k" ext)'
        expr, mod = self.compile_expr(src_str, d=d, ext=[3])
        await expr()
        self.assertIs(inner, d["k"])
        self.assertEqual([1, 2, 3], inner)


    async def test_inplace_item_unknown_operator(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(compile/inplace-item ?? d "k" 1)', d={})


    async def test_inplace_item_wrong_arg_count(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(compile/inplace-item + d "k")', d={})


class TestInplaceAttrSpecial(SpecialsTestCase):

    async def test_inplace_attr_symbol_obj(self):
        obj = SimpleNamespace(x=10)
        src_str = """
        (compile/begin
          (compile/inplace-attr + obj x 1)
          (compile/attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, obj.x)


    async def test_inplace_attr_expression_obj(self):
        obj = SimpleNamespace(x=10)
        src_str = """
        (compile/begin
          (compile/inplace-attr + (identity obj) x 1)
          (compile/attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj, identity=lambda o: o)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, obj.x)


    async def test_inplace_attr_returns_none(self):
        obj = SimpleNamespace(x=0)
        src_str = "(compile/inplace-attr + obj x 1)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(1, obj.x)


    async def test_inplace_attr_evaluates_obj_once(self):
        calls = {"n": 0}

        def make_obj():
            calls["n"] += 1
            return SimpleNamespace(x=10)

        src_str = "(compile/inplace-attr + (make_obj) x 1)"
        expr, mod = self.compile_expr(src_str, make_obj=make_obj)
        await expr()
        self.assertEqual(1, calls["n"])


    async def test_inplace_attr_symbol_obj_emits_var_attr_store(self):
        code = self.compile_to_code(
            "(compile/inplace-attr + obj x 1)",
            obj=SimpleNamespace(x=0))
        self.assertIn(OpCode.KOS_OP_VAR_ATTR, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_VAR_ATTR_STORE, _opcodes(code))
        self.assertNotIn(OpCode.KOS_OP_DUP, _opcodes(code))
        self.assertNotIn(OpCode.KOS_OP_ATTR, _opcodes(code))


    async def test_inplace_attr_expression_obj_emits_dup_and_attr_store(self):
        code = self.compile_to_code(
            "(compile/inplace-attr + (identity obj) x 1)",
            obj=SimpleNamespace(x=0),
            identity=lambda o: o)
        self.assertEqual(1, _count_op(code, OpCode.KOS_OP_DUP))
        self.assertIn(OpCode.KOS_OP_ATTR, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_ATTR_STORE, _opcodes(code))
        self.assertNotIn(OpCode.KOS_OP_VAR_ATTR, _opcodes(code))


    async def test_inplace_attr_mutates_in_place(self):
        """
        inplace-attr on a set-valued attribute updates the stored set
        rather than replacing it with a copy
        """

        inner = {1, 2}
        obj = SimpleNamespace(x=inner)
        src_str = "(compile/inplace-attr | obj x ext)"
        expr, mod = self.compile_expr(src_str, obj=obj, ext={3})
        await expr()
        self.assertIs(inner, obj.x)
        self.assertEqual({1, 2, 3}, inner)


    async def test_inplace_attr_attr_must_be_symbol(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(compile/inplace-attr + obj (sym) 1)",
                obj=SimpleNamespace(x=0),
                sym=lambda: "x")


    async def test_inplace_attr_unknown_operator(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(compile/inplace-attr ?? obj x 1)",
                obj=SimpleNamespace(x=0))


    async def test_inplace_attr_wrong_arg_count(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(compile/inplace-attr + obj x)",
                obj=SimpleNamespace(x=0))


# The end.
