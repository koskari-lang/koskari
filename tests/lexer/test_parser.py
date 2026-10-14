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

import pytest

from koskari.lexer import READ_EOF, default_layout_reader, parse, parse_all
from koskari.lib import car, cdr, symbol
from koskari.reader import LayoutReader
from koskari.types import is_pair


pytestmark = pytest.mark.phase_infra


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


    async def test_parse_eof_returns_read_eof(self):
        result = parse('')
        self.assertIs(result, READ_EOF)


    async def test_parse_whitespace_only_returns_read_eof(self):
        result = parse('   \n\t  ')
        self.assertIs(result, READ_EOF)


    async def test_parse_layout_block(self):
        src = "if True:\n  x\n"
        result = parse(src)
        self.assertTrue(is_pair(result))
        self.assertEqual(car(result), symbol('cond'))


class TestParseAll(IsolatedAsyncioTestCase):

    async def test_parse_all_empty_yields_nothing(self):
        forms = list(parse_all(''))
        self.assertEqual(forms, [])


    async def test_parse_all_single_form(self):
        forms = list(parse_all('42'))
        self.assertEqual(forms, [42])


    async def test_parse_all_reports_position_of_later_forms(self):
        """
        an error in the third form is reported on the third form's
        line; positions keep counting across the forms of one source
        """

        from koskari.reader.parser import ReaderSyntaxError

        src = '(define x 1)\n(define y 2)\n(define z (1 2)]\n'
        with self.assertRaises(ReaderSyntaxError) as caught:
            list(parse_all(src, '<later>'))
        self.assertEqual(caught.exception.location[0], 3)


    async def test_parse_all_multiple_forms(self):
        src = '(define x 1)\n(define y 2)\nx'
        forms = list(parse_all(src))
        self.assertEqual(len(forms), 3)
        self.assertTrue(is_pair(forms[0]))
        self.assertTrue(is_pair(forms[1]))


class TestParseAllPragma(IsolatedAsyncioTestCase):

    async def test_layout_off_reads_later_forms_as_sexprs(self):
        """
        a top-level pragma turning reader/layout off applies to the forms
        after it, so an indented s-expression is not an unexpected indent
        """

        src = (
            '(pragma [reader/layout False])\n'
            '(define f (lambda (x)\n'
            '  (+ x 1)))\n'
            '  (print "ok")\n')
        forms = list(parse_all(src, '<layout-off>'))
        self.assertEqual(len(forms), 3)
        self.assertEqual(car(forms[2]), symbol('print'))


    async def test_implicit_calls_off_rejects_bare_call_line(self):
        """
        a top-level pragma turning reader/implicit-calls off applies to
        the forms after it, so a line of several forms is an error
        """

        from koskari.exceptions import LayoutError

        src = (
            '(pragma [reader/implicit-calls False])\n'
            'print 1 2\n')
        with self.assertRaises(LayoutError) as caught:
            list(parse_all(src, '<implicit-off>'))
        self.assertEqual(caught.exception.location[0], 2)


    async def test_non_literal_value_skips_the_form(self):
        """
        a pragma whose reader setting is not a literal is skipped, and
        layout reading continues
        """

        src = (
            '(pragma [reader/layout (f)])\n'
            'if x:\n'
            '  1\n'
            'else:\n'
            '  2\n')
        forms = list(parse_all(src, '<non-literal>'))
        self.assertEqual(len(forms), 2)


    async def test_unknown_key_skips_the_form(self):
        """
        a pragma naming an unregistered reader setting is skipped, and
        layout reading continues
        """

        src = (
            '(pragma [reader/bogus 1])\n'
            'if x:\n'
            '  1\n'
            'else:\n'
            '  2\n')
        forms = list(parse_all(src, '<unknown-key>'))
        self.assertEqual(len(forms), 2)


# The end.
