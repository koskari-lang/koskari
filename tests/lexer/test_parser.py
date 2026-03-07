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
tests.lexer.test_parser

Unit tests for koskari.lexer.parser.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.types import symbol, car, cdr, is_pair
from koskari.reader import LayoutReader

from koskari.lexer import default_layout_reader, parse, parse_all


class TestDefaultLayoutReader(IsolatedAsyncioTestCase):

    async def test_returns_layout_reader(self):
        reader = default_layout_reader()
        self.assertIsInstance(reader, LayoutReader)


    async def test_cached_same_instance(self):
        r1 = default_layout_reader()
        r2 = default_layout_reader()
        self.assertIs(r1, r2)


class TestParse(IsolatedAsyncioTestCase):

    async def test_parse_atom_returns_symbol(self):
        result = parse('foo')
        self.assertEqual(result, symbol('foo'))


    async def test_parse_s_expr_returns_pair(self):
        result = parse('(a b c)')
        self.assertTrue(is_pair(result))
        self.assertEqual(car(result), symbol('a'))
        self.assertEqual(car(cdr(result)), symbol('b'))
        self.assertEqual(car(cdr(cdr(result))), symbol('c'))


    async def test_parse_eof_returns_none(self):
        result = parse('')
        self.assertIsNone(result)


    async def test_parse_whitespace_only_returns_none(self):
        result = parse('   \n\t  ')
        self.assertIsNone(result)


    async def test_parse_layout_block(self):
        src = """
        if True:
          x
        """
        result = parse(src)
        self.assertTrue(is_pair(result))
        self.assertEqual(car(result), symbol('compile/cond'))


class TestParseAll(IsolatedAsyncioTestCase):

    async def test_parse_all_empty_yields_nothing(self):
        forms = list(parse_all(''))
        self.assertEqual(forms, [])


    async def test_parse_all_single_form(self):
        forms = list(parse_all('42'))
        self.assertEqual(forms, [42])


    async def test_parse_all_multiple_forms(self):
        forms = list(parse_all('(define x 1) (define y 2) x'))
        self.assertEqual(len(forms), 3)
        self.assertTrue(is_pair(forms[0]))
        self.assertTrue(is_pair(forms[1]))
        self.assertEqual(forms[2], symbol('x'))


# The end.
