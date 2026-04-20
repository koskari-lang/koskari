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


from koskari.lib import symbol

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestQuoteSpecial(SpecialsTestCase):

    async def test_quote_atom_number(self):
        src_str = "(compile/quote 42)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_quote_atom_symbol(self):
        src_str = "(compile/quote x)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, symbol("x"))


    async def test_quote_list_expands_to_compile_cons(self):
        """
        (compile/quote (1 2 3)) is rewritten to (compile/cons (compile/quote 1)
        ...) and compiles to CONST + CONS opcodes. Result is (1 . (2 . (3 .
        nil))); unpack to get [1, 2, 3].
        """

        src_str = "(compile/quote (1 2 3))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertEqual([1, 2, 3], list(result.unpack()))


    async def test_quote_improper_list(self):
        """
        (compile/quote (1 2 . 3)) yields an improper list.
        """

        src_str = "(compile/quote (1 2 . 3))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        head, tail = result
        second, tail = tail
        self.assertEqual(1, head)
        self.assertEqual(2, second)
        self.assertEqual(3, tail)
        self.assertFalse(result.is_proper())


    async def test_quote_improper_pair(self):
        """
        (compile/quote (1 . 2)) yields an improper pair.
        """

        src_str = "(compile/quote (1 . 2))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        head, tail = result
        self.assertEqual(1, head)
        self.assertEqual(2, tail)
        self.assertFalse(result.is_proper())


# The end.
