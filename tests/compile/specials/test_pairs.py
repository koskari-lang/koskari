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
Tests for koskari.compile compile/quote special form.
"""

from koskari.lib import OpCode, cons, nil

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestCompileConsCarCdr(SpecialsTestCase):

    async def test_compile_cons_two_elements(self):
        """
        (compile/cons 1 2 nil) yields (1 . (2 . nil)), unpack to [1, 2]
        """

        src_str = "(compile/cons 1 2 nil)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertEqual(cons(1, 2, nil), result)


    async def test_compile_cons_three_elements(self):
        """
        (compile/cons 1 2 3 nil) yields (1 . (2 . (3 . nil)))
        """

        src_str = "(compile/cons 1 2 3 nil)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(cons(1, 2, 3, nil), result)


    async def test_compile_cons_star_arg_rewrites_to_call_not_cons_opcode(self):
        """
        *: cannot lower to KOS_OP_CONS; handler falls back to compile/call.
        """

        code = self.compile_to_code("(compile/cons 1 *: xs nil)")
        opcodes = [code.oparg(i)[0] for i in range(code.length())]
        self.assertNotIn(OpCode.KOS_OP_CONS, opcodes)
        self.assertTrue(self.call_ops(code))


    async def test_compile_car(self):
        """
        (compile/car (compile/cons 1 nil)) yields 1
        """

        src_str = "(compile/car (compile/cons 1 nil))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(1, result)


    async def test_compile_cdr(self):
        """
        (compile/cdr (compile/cons 1 (compile/cons 2 nil))) yields (2 . nil)
        """

        src_str = "(compile/cdr (compile/cons 1 (compile/cons 2 nil)))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual([2], list(result.unpack()))


# The end.
