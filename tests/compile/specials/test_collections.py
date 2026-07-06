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
Tests for koskari.compile.specials.collections compile/#list and
compile/#list-append.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import OpCode

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def _opcodes(code):
    return [code.oparg(i)[0] for i in range(code.length())]


def _find_op(code, opcode):
    for i in range(code.length()):
        op, arg = code.oparg(i)
        if op == opcode:
            return arg
    return None


class TestBuildListSpecial(SpecialsTestCase):

    async def test_list_empty(self):
        expr, mod = self.compile_expr("(#list)")
        self.assertEqual([], await expr())


    async def test_list_values(self):
        expr, mod = self.compile_expr("(#list 1 2 3)")
        self.assertEqual([1, 2, 3], await expr())


    async def test_list_emits_build_list(self):
        code = self.compile_to_code("(#list 1 2)")
        self.assertIn(OpCode.KOS_OP_BUILD_LIST, _opcodes(code))
        self.assertEqual(2, _find_op(code, OpCode.KOS_OP_BUILD_LIST))
        self.assertEqual([], self.call_ops(code))


class TestListAppendSpecial(SpecialsTestCase):

    async def test_list_append_symbol(self):
        expr, mod = self.compile_expr(
            "(begin (setq col (#list)) (#list-append col 9) col)")
        self.assertEqual([9], await expr())


    async def test_list_append_symbol_emits_opcode(self):
        code = self.compile_to_code(
            "(begin (setq col (#list)) (#list-append col 1))")
        ops = _opcodes(code)
        self.assertTrue(
            OpCode.KOS_OP_VAR_LIST_APPEND in ops
            or OpCode.KOS_OP_FAST_LIST_APPEND in ops)
        self.assertEqual([], self.call_ops(code))


    async def test_list_append_expr(self):
        expr, mod = self.compile_expr("(#list-append (#list 1) 2)")
        self.assertEqual([1, 2], await expr())


    async def test_list_append_expr_emits_build_list_append(self):
        code = self.compile_to_code("(#list-append (#list 1) 2)")
        self.assertIn(OpCode.KOS_OP_BUILD_LIST, _opcodes(code))
        self.assertIn(OpCode.KOS_OP_BUILD_LIST_APPEND, _opcodes(code))


class TestListEachPattern(SpecialsTestCase):

    async def test_let_foreach_list_append_pattern(self):
        src = """
        (compile/let [[col (#list)]]
          (compile/for-each [i x]
            (#list-append col i))
          col)
        """
        expr, mod = self.compile_expr(src, x=range(3))
        self.assertEqual([0, 1, 2], await expr())


    async def test_let_foreach_list_append_no_calls(self):
        src = """
        (compile/let [[col (#list)]]
          (compile/for-each [i x]
            (#list-append col i))
          col)
        """
        code = self.compile_to_code(src, x=range(2))
        self.assertEqual([], self.call_ops(code))


# The end.
