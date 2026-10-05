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
Tests for koskari.compile quote special form.
"""

import pytest

from koskari.lib import OpCode, bracket_nil, bracket_pair, cons, nil

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestCompileConsCarCdr(SpecialsTestCase):

    async def test_compile_cons_two_elements(self):
        """
        (cons 1 2 nil) yields (1 . (2 . nil)), unpack to [1, 2]
        """

        src_str = "(cons 1 2 nil)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertEqual(cons(1, 2, nil), result)


    async def test_compile_cons_three_elements(self):
        """
        (cons 1 2 3 nil) yields (1 . (2 . (3 . nil)))
        """

        src_str = "(cons 1 2 3 nil)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(cons(1, 2, 3, nil), result)


    async def test_compile_cons_star_arg_rewrites_to_call_not_cons_opcode(self):
        """
        *: cannot lower to CONS; handler falls back to call.
        """

        code = self.compile_to_code("(cons 1 *: xs nil)")
        opcodes = [code.oparg(i)[0] for i in range(code.length())]
        self.assertNotIn(OpCode.CONS, opcodes)
        self.assertTrue(self.call_ops(code))


    async def test_compile_car(self):
        """
        (car (cons 1 nil)) yields 1
        """

        src_str = "(car (cons 1 nil))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(1, result)


    async def test_compile_cdr(self):
        """
        (cdr (cons 1 (cons 2 nil))) yields (2 . nil)
        """

        src_str = "(cdr (cons 1 (cons 2 nil)))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual([2], list(result.unpack()))


class TestCompileBracketCons(SpecialsTestCase):
    """
    bracket-cons and bracket-proper build bracket pairs in the VM, so a
    binding of either name in scope does not reach the form
    """

    async def test_bracket_cons_with_tail(self):
        expr, mod = self.compile_expr("(bracket-cons 1 2)")
        result = await expr()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual((result.car, result.cdr), (1, 2))

    async def test_bracket_cons_proper(self):
        expr, mod = self.compile_expr(
            "(bracket-cons 1 2 bracket-nil)", **{"bracket-nil": bracket_nil})
        result = await expr()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([1, 2], list(result.unpack()))

    async def test_bracket_cons_emits_the_opcode(self):
        expr, mod = self.compile_expr("(bracket-cons 1 2)")
        code = expr.code
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.BRACKET_CONS, ops)

    async def test_bracket_cons_with_a_keyword_calls_the_builtin(self):
        expr, mod = self.compile_expr(
            "(bracket-cons 1 *: rest)", rest=(2, bracket_nil))
        result = await expr()
        self.assertEqual([1, 2], list(result.unpack()))

    async def test_bracket_proper(self):
        expr, mod = self.compile_expr("(bracket-proper 1 (+ 1 1) 3)")
        result = await expr()
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([1, 2, 3], list(result.unpack()))

    async def test_bracket_proper_empty(self):
        expr, mod = self.compile_expr("(bracket-proper)")
        self.assertIs(await expr(), bracket_nil)


# The end.
