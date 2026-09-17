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
Tests for koskari.compile item, del-item, and set-item
special forms.
"""


import pytest

from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestItemSpecial(SpecialsTestCase):

    async def test_item_constant_key(self):
        lst = [10, 20, 30]
        src_str = "(item lst 0)"
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(10, result)


    async def test_item_variable_key(self):
        lst = [10, 20, 30]
        src_str = """
        (let [[i 2]]
          (item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(30, result)


class TestSetItemSpecial(SpecialsTestCase):

    async def test_set_item_then_read(self):
        lst = [1, 2, 3]
        src_str = """
        (begin
          (set-item lst 1 99)
          (item lst 1))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        result = await expr()
        self.assertEqual(99, result)
        self.assertEqual(99, lst[1])


class TestDelItemSpecial(SpecialsTestCase):

    async def test_del_item_returns_none(self):
        d = {"remove_me": 1}
        src_str = '(del-item d "remove_me")'
        expr, mod = self.compile_expr(src_str, d=d)
        result = await expr()
        self.assertIsNone(result)
        self.assertNotIn("remove_me", d)


class TestSetItemFusion(SpecialsTestCase):


    async def test_set_item_symbol_obj_const_key_emits_fused_store(self):
        code = self.compile_to_code("(set-item d \"k\" (f))", d={}, f=None)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_CONST_ITEM_STORE, ops)
        self.assertNotIn(OpCode.CONST_ITEM_STORE, ops)
        self.assertNotIn(OpCode.VAR, ops)


    async def test_set_item_symbol_obj_const_key_const_value_is_stack_free(self):
        code = self.compile_to_code("(set-item d \"k\" 1)", d={})
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_CONST_ITEM_STORE_CONST, ops)
        self.assertNotIn(OpCode.VAR_CONST_ITEM_STORE, ops)
        self.assertNotIn(OpCode.CONST, ops[:ops.index(OpCode.VAR_CONST_ITEM_STORE_CONST)])


    async def test_set_item_symbol_obj_symbol_key_emits_fused_store(self):
        code = self.compile_to_code("(set-item lst i (f))", lst=[], i=0, f=None)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_ITEM_STORE, ops)
        self.assertNotIn(OpCode.ITEM_STORE, ops)
        self.assertNotIn(OpCode.VAR, ops)


    async def test_set_item_symbol_obj_symbol_key_const_value_is_stack_free(self):
        code = self.compile_to_code("(set-item lst i 1)", lst=[], i=0)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_ITEM_STORE_CONST, ops)
        self.assertNotIn(OpCode.VAR_ITEM_STORE, ops)


    async def test_set_item_const_value_in_function_body_lowers_to_fast(self):
        code = self.compile_to_code("(lambda [lst i] (set-item lst i 1))")
        body = self.lambda_body_code(code)
        ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.FAST_ITEM_STORE_CONST, ops)


    async def test_set_item_computed_key_keeps_stack_store(self):
        code = self.compile_to_code("(set-item lst (+ i 1) 1)", lst=[], i=0)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.ITEM_STORE, ops)
        self.assertNotIn(OpCode.VAR_ITEM_STORE, ops)


    async def test_set_item_expression_obj_keeps_stack_store(self):
        code = self.compile_to_code("(set-item (f) 0 1)", f=None)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.CONST_ITEM_STORE, ops)


    async def test_set_item_fused_symbol_key(self):
        lst = [1, 2, 3]
        src_str = """
        (let [[i 2]]
          (set-item lst i 99)
          (item lst i))
        """
        expr, mod = self.compile_expr(src_str, lst=lst)
        self.assertEqual(99, await expr())
        self.assertEqual([1, 2, 99], lst)


    async def test_set_item_fused_returns_none(self):
        d = {}
        expr, mod = self.compile_expr("(set-item d \"k\" 5)", d=d)
        self.assertIsNone(await expr())
        self.assertEqual({"k": 5}, d)


    async def test_set_item_fused_evaluates_value_before_object(self):
        # the fused row resolves the object after the value expression
        # has run: a value expression that rebinds the name stores into
        # the new binding
        old, new = [0], [0]
        src_str = """
        (let [[d old]]
          (set-item d 0 (begin (setq d new) 7)))
        """
        expr, mod = self.compile_expr(src_str, old=old, new=new)
        await expr()
        self.assertEqual([0], old)
        self.assertEqual([7], new)


    async def test_set_item_fused_unbound_object_raises_after_value(self):
        seen = []
        src_str = "(set-item missing 0 (note 1))"
        expr, mod = self.compile_expr(src_str, note=seen.append)
        with self.assertRaises(NameError):
            await expr()
        self.assertEqual([1], seen)


class TestItemSpecialFusion(SpecialsTestCase):

    async def test_item_symbol_obj_symbol_key_emits_var_item(self):
        code = self.compile_to_code("(item lst i)", lst=[], i=0)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_ITEM, ops)
        self.assertNotIn(OpCode.ITEM, ops)


    async def test_item_symbol_obj_const_key_emits_var_const_item(self):
        code = self.compile_to_code("(item lst 0)", lst=[])
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.VAR_CONST_ITEM, ops)
        self.assertNotIn(OpCode.CONST_ITEM, ops)


    async def test_item_in_function_body_lowers_to_fast(self):
        src_str = """
        (lambda [lst i] (item lst i))
        """
        code = self.compile_to_code(src_str)
        body = self.lambda_body_code(code)
        ops = [body.oparg(i)[0] for i in range(body.length())]
        self.assertIn(OpCode.FAST_ITEM, ops)


# The end.
