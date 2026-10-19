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

from koskari.exceptions import SpecialError
from koskari.lib import cons, nil, symbol
from koskari.matcher import CALLEDBY, MATCH
from koskari.syntax import special

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestQuoteSpecial(SpecialsTestCase):

    async def test_quote_atom_number(self):
        src_str = "(quote 42)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(42, result)


    async def test_quote_atom_symbol(self):
        src_str = "(quote x)"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, symbol("x"))


    async def test_quote_list_expands_to_compile_cons(self):
        """
        (quote (1 2 3)) is rewritten to (cons (quote 1)
        ...) and compiles to CONST + CONS opcodes. Result is (1 . (2 . (3 .
        nil))); unpack to get [1, 2, 3].
        """

        src_str = "(quote (1 2 3))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsNotNone(result)
        self.assertEqual([1, 2, 3], list(result.unpack()))


    async def test_quote_improper_list(self):
        """
        (quote (1 2 . 3)) yields an improper list.
        """

        src_str = "(quote (1 2 . 3))"
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
        (quote (1 . 2)) yields an improper pair.
        """

        src_str = "(quote (1 . 2))"
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        head, tail = result
        self.assertEqual(1, head)
        self.assertEqual(2, tail)
        self.assertFalse(result.is_proper())


def inserting(name, form):
    """
    A special named compile/NAME that expands to form, as a macro
    expansion places a value the reader never produces
    """

    return special(symbol(f"compile/{name}"), patterns=MATCH(CALLEDBY()))(
        lambda compiler, source: form)


class TestQuoteRefusesNonPoolData(SpecialsTestCase):

    def test_quote_refuses_a_list(self):
        syn = inserting("ql", cons(symbol("quote"), [1, 2], nil))
        with self.assertRaises(SpecialError):
            self.compile_expr("(ql)", **{"compile/ql": syn})


    def test_quote_syntax_refuses_a_list(self):
        syn = inserting("qsl", cons(symbol("quote-syntax"), [1, 2], nil))
        with self.assertRaises(SpecialError):
            self.compile_expr("(qsl)", **{"compile/qsl": syn})


# The end.
