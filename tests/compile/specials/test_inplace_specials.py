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
Tests for koskari.compile inplace-item and inplace-attr
special forms.
"""


from types import SimpleNamespace

import pytest

from koskari.exceptions import SpecialError
from koskari.lib import OpCode, Operand

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _ops(code):
    return [code.oparg(i) for i in range(code.length())]


def _opcodes(code):
    return [op for op, _ in _ops(code)]


def _count_op(code, opcode):
    return sum(1 for op, _ in _ops(code) if op == opcode)


def _row(code, opcode):
    """
    The operand row of the first instruction with opcode, with its
    pool values restored, or None
    """

    for op, arg in code.soften():
        if op == opcode:
            return tuple(arg)
    return None


class TestInplaceItemSpecial(SpecialsTestCase):

    async def test_inplace_item_constant_key(self):
        d = {"k": 10}
        src_str = """
        (begin
          (inplace-item + d "k" 1)
          (item d "k"))
        """
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, d["k"])


    async def test_inplace_item_variable_key(self):
        lst = [1, 2, 3]
        src_str = """
        (let [[i 1]]
          (begin
            (inplace-item + lst i 10)
            (item lst i)))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(12, result)
        self.assertEqual(12, lst[1])


    async def test_inplace_item_returns_none(self):
        d = {"k": 0}
        src_str = '(inplace-item + d "k" 1)'
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(1, d["k"])


    async def test_inplace_item_subtract(self):
        d = {"k": 10}
        src_str = """
        (begin
          (inplace-item - d "k" 3)
          (item d "k"))
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

        src_str = '(inplace-item + (make_d) "k" 1)'
        expr, mod = self.compile_expr(src_str, make_d=make_d)
        await expr()
        self.assertEqual(1, calls["n"])


    async def test_inplace_item_evaluates_key_once(self):
        calls = {"n": 0}
        lst = [0, 10, 0]

        def keyfn():
            calls["n"] += 1
            return 1

        src_str = "(inplace-item + lst (keyfn) 5)"
        expr, mod = self.compile_expr(src_str, lst=lst, keyfn=keyfn)
        await expr()
        self.assertEqual(1, calls["n"])
        self.assertEqual(15, lst[1])


    async def test_inplace_item_symbol_obj_const_key_const_value(self):
        code = self.compile_to_code('(inplace-item + d "k" 1)', d={})
        self.assertEqual(0, _count_op(code, OpCode.DUP))
        self.assertNotIn(OpCode.CONST, _opcodes(code)[:1])
        # the row carries the selector, the object, the key and the
        # folded operand
        self.assertEqual(
            (int(Operand.ADD), "d", "k", 1),
            _row(code, OpCode.VAR_CONST_ITEM_INPLACE_CONST))


    async def test_inplace_item_symbol_obj_symbol_key_const_value(self):
        code = self.compile_to_code("(inplace-item + lst i 1)", lst=[], i=0)
        self.assertEqual(0, _count_op(code, OpCode.DUP))
        self.assertEqual(
            (int(Operand.ADD), "lst", "i", 1),
            _row(code, OpCode.VAR_ITEM_INPLACE_CONST))


    async def test_inplace_item_symbol_obj_computed_value(self):
        code = self.compile_to_code("(inplace-item + lst i (f))", lst=[], i=0, f=None)
        self.assertIn(OpCode.VAR_ITEM_INPLACE, _opcodes(code))
        code = self.compile_to_code('(inplace-item + d "k" (f))', d={}, f=None)
        self.assertIn(OpCode.VAR_CONST_ITEM_INPLACE, _opcodes(code))


    async def test_inplace_item_symbol_obj_two_values_keeps_count(self):
        code = self.compile_to_code("(inplace-item + lst i 1 2)", lst=[], i=0)
        self.assertIn(OpCode.VAR_ITEM_INPLACE, _opcodes(code))
        self.assertNotIn(OpCode.VAR_ITEM_INPLACE_CONST, _opcodes(code))


    async def test_inplace_item_expression_obj_emits_stack_rows(self):
        code = self.compile_to_code('(inplace-item + (f) "k" 1)', f=None)
        self.assertEqual(0, _count_op(code, OpCode.DUP))
        self.assertIn(OpCode.CONST_ITEM_INPLACE, _opcodes(code))
        code = self.compile_to_code("(inplace-item + (f) i 1)", f=None, i=0)
        self.assertIn(OpCode.ITEM_INPLACE, _opcodes(code))


    async def test_inplace_item_computed_key_emits_stack_row(self):
        code = self.compile_to_code("(inplace-item + lst (+ i 1) 1)", lst=[], i=0)
        self.assertIn(OpCode.ITEM_INPLACE, _opcodes(code))
        self.assertNotIn(OpCode.VAR_ITEM_INPLACE, _opcodes(code))


    async def test_inplace_item_in_function_body_lowers_to_fast(self):
        code = self.compile_to_code("(lambda [lst i] (inplace-item + lst i 1))")
        body = self.lambda_body_code(code)
        ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.FAST_ITEM_INPLACE_CONST, ops)


    async def test_inplace_item_mutates_in_place(self):
        """
        inplace-item on a list-valued slot extends the stored list
        rather than replacing it with a copy
        """

        inner = [1, 2]
        d = {"k": inner}
        src_str = '(inplace-item + d "k" ext)'
        expr, mod = self.compile_expr(src_str, d=d, ext=[3])
        await expr()
        self.assertIs(inner, d["k"])
        self.assertEqual([1, 2, 3], inner)


    async def test_inplace_item_multiple_values(self):
        """
        extra values reduce left-to-right through the single fused
        row, whose OPCOUNT carries the value count
        """

        d = {"k": 10}
        src_str = '(inplace-item + d "k" 1 2 3)'
        expr, mod = self.compile_expr(src_str, d=d)
        await expr()
        self.assertEqual(16, d["k"])

        code = self.compile_to_code('(inplace-item + d "k" 1 2 3)',
                                    d={})
        rows = [code.extra_tuple(arg) for op, arg in _ops(code)
                if op == OpCode.VAR_CONST_ITEM_INPLACE]
        self.assertEqual(1, len(rows))
        self.assertEqual(int(Operand.ADD), rows[0][0] >> 8)
        self.assertEqual(3, rows[0][0] & 0xff)


    async def test_inplace_item_power_takes_one_value(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(inplace-item ** d "k" 2 3)', d={})


    async def test_inplace_item_unknown_operator(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(inplace-item ?? d "k" 1)', d={})


    async def test_inplace_item_wrong_arg_count(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code('(inplace-item + d "k")', d={})


class TestInplaceAttrSpecial(SpecialsTestCase):

    async def test_inplace_attr_symbol_obj(self):
        obj = SimpleNamespace(x=10)
        src_str = """
        (begin
          (inplace-attr + obj x 1)
          (attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, obj.x)


    async def test_inplace_attr_expression_obj(self):
        obj = SimpleNamespace(x=10)
        src_str = """
        (begin
          (inplace-attr + (identity obj) x 1)
          (attr obj x))
        """
        expr, mod = self.compile_expr(src_str, obj=obj, identity=lambda o: o)
        result = await expr()
        self.assertEqual(11, result)
        self.assertEqual(11, obj.x)


    async def test_inplace_attr_returns_none(self):
        obj = SimpleNamespace(x=0)
        src_str = "(inplace-attr + obj x 1)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(1, obj.x)


    async def test_inplace_attr_evaluates_obj_once(self):
        calls = {"n": 0}

        def make_obj():
            calls["n"] += 1
            return SimpleNamespace(x=10)

        src_str = "(inplace-attr + (make_obj) x 1)"
        expr, mod = self.compile_expr(src_str, make_obj=make_obj)
        await expr()
        self.assertEqual(1, calls["n"])


    async def test_inplace_attr_symbol_obj_emits_var_attr_inplace(self):
        code = self.compile_to_code(
            "(inplace-attr + obj x 1)",
            obj=SimpleNamespace(x=0))
        ops = _opcodes(code)
        # a single fused op replaces load + OBJECT_INPLACE + store
        self.assertIn(OpCode.VAR_ATTR_INPLACE, ops)
        self.assertNotIn(OpCode.VAR_ATTR, ops)
        self.assertNotIn(OpCode.VAR_ATTR_STORE, ops)
        self.assertNotIn(OpCode.OBJECT_INPLACE, ops)
        self.assertNotIn(OpCode.DUP, ops)


    async def test_inplace_attr_expression_obj_emits_attr_inplace(self):
        code = self.compile_to_code(
            "(inplace-attr + (identity obj) x 1)",
            obj=SimpleNamespace(x=0),
            identity=lambda o: o)
        ops = _opcodes(code)
        # obj rides the stack; the single fused op replaces DUP + ATTR +
        # OBJECT_INPLACE + ATTR_STORE
        self.assertIn(OpCode.ATTR_INPLACE, ops)
        self.assertNotIn(OpCode.ATTR, ops)
        self.assertNotIn(OpCode.ATTR_STORE, ops)
        self.assertNotIn(OpCode.OBJECT_INPLACE, ops)
        self.assertNotIn(OpCode.DUP, ops)


    async def test_inplace_attr_mutates_in_place(self):
        """
        inplace-attr on a set-valued attribute updates the stored set
        rather than replacing it with a copy
        """

        inner = {1, 2}
        obj = SimpleNamespace(x=inner)
        src_str = "(inplace-attr | obj x ext)"
        expr, mod = self.compile_expr(src_str, obj=obj, ext={3})
        await expr()
        self.assertIs(inner, obj.x)
        self.assertEqual({1, 2, 3}, inner)


    async def test_inplace_attr_multiple_values(self):
        obj = SimpleNamespace(x=10)
        src_str = "(inplace-attr + obj x 1 2 3)"
        expr, mod = self.compile_expr(src_str, obj=obj)
        await expr()
        self.assertEqual(16, obj.x)


    async def test_inplace_attr_power_takes_one_value(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(inplace-attr ** obj x 2 3)",
                obj=SimpleNamespace(x=2))


    async def test_inplace_attr_attr_must_be_symbol(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(inplace-attr + obj (sym) 1)",
                obj=SimpleNamespace(x=0),
                sym=lambda: "x")


    async def test_inplace_attr_unknown_operator(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(inplace-attr ?? obj x 1)",
                obj=SimpleNamespace(x=0))


    async def test_inplace_attr_wrong_arg_count(self):
        with self.assertRaises(SpecialError):
            self.compile_to_code(
                "(inplace-attr + obj x)",
                obj=SimpleNamespace(x=0))


# The end.
