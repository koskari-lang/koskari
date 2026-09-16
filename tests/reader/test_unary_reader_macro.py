# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reader.test_unary_reader_macro

The unary prefixes ``-``, ``+``, and ``~``: attached to a name, a digit,
or an opening delimiter each reads as the one-argument call ``(OP FORM)``;
a ``-`` before a digit is a signed number; before anything else, or
standing alone, each is its ordinary symbol. Gated on the reader/unary
pragma.
"""

from unittest import TestCase

import pytest

from textwrap import dedent

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.lib import symbol
from koskari.pragma import ModulePragma
from koskari.reader import (
    IndentAwareStream, LayoutReader, Reader, source_str)


pytestmark = pytest.mark.phase_infra


def read_layout(src):
    reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY), ModulePragma())
    stream = IndentAwareStream.from_source(
        source_str(dedent(src), '<unittest>'))
    return reader.read(stream)


def read_sexpr(src, unary=True):
    reader = Reader()
    reader.reload_pragma(ModulePragma())
    reader.unary = unary
    return reader.read(source_str(src, '<unittest>'))


class TestUnaryPrefix(TestCase):

    def test_name(self):
        self.assertEqual(read_sexpr("-x"), pycons((symbol('-'), symbol('x'))))


    def test_dotted_name(self):
        self.assertEqual(
            read_sexpr("-foo.bar"),
            pycons((symbol('-'), (symbol('attr'), symbol('foo'), symbol('bar')))))


    def test_delimited_form(self):
        self.assertEqual(
            read_sexpr("-(f y)"),
            pycons((symbol('-'), (symbol('f'), symbol('y')))))


    def test_postfix_binds_tighter(self):
        self.assertEqual(
            read_sexpr('-foo["a"]'),
            pycons((symbol('-'), (symbol('item'), symbol('foo'), 'a'))))


    def test_inside_a_call(self):
        self.assertEqual(
            read_sexpr("(* 2 -x)"),
            pycons((symbol('*'), 2, (symbol('-'), symbol('x')))))


    def test_double_dash_is_a_symbol(self):
        self.assertEqual(read_sexpr("--x"), symbol('--x'))


    def test_plus(self):
        self.assertEqual(read_sexpr("+x"), pycons((symbol('+'), symbol('x'))))
        self.assertEqual(read_sexpr("+1"), pycons((symbol('+'), 1)))
        self.assertEqual(
            read_sexpr("+(f y)"),
            pycons((symbol('+'), (symbol('f'), symbol('y')))))


    def test_invert(self):
        self.assertEqual(read_sexpr("~x"), pycons((symbol('~'), symbol('x'))))
        self.assertEqual(read_sexpr("~5"), pycons((symbol('~'), 5)))
        self.assertEqual(
            read_sexpr("~foo.bar"),
            pycons((symbol('~'), (symbol('attr'), symbol('foo'), symbol('bar')))))


class TestUnaryDefers(TestCase):

    def test_numbers_keep_their_sign(self):
        self.assertEqual(read_sexpr("-1"), -1)
        self.assertEqual(read_sexpr("-1.5"), -1.5)
        self.assertEqual(read_sexpr("-.5"), -0.5)


    def test_bare_operator(self):
        self.assertEqual(
            read_sexpr("(- 8 1)"), pycons((symbol('-'), 8, 1)))
        self.assertEqual(read_sexpr("-"), symbol('-'))
        self.assertEqual(
            read_sexpr("(+ 8 1)"), pycons((symbol('+'), 8, 1)))
        self.assertEqual(read_sexpr("(~ 5)"), pycons((symbol('~'), 5)))


    def test_other_punctuation_stays_a_symbol(self):
        self.assertEqual(read_sexpr("->"), symbol('->'))
        self.assertEqual(read_sexpr("-="), symbol('-='))
        self.assertEqual(read_sexpr("+="), symbol('+='))
        self.assertEqual(read_sexpr("~>"), symbol('~>'))


    def test_dash_inside_a_name_is_not_a_prefix(self):
        self.assertEqual(read_sexpr("foo-bar"), symbol('foo-bar'))


    def test_pragma_off_reads_the_symbol(self):
        self.assertEqual(read_sexpr("-x", unary=False), symbol('-x'))
        self.assertEqual(read_sexpr("+x", unary=False), symbol('+x'))
        self.assertEqual(read_sexpr("~x", unary=False), symbol('~x'))


class TestUnaryInLayout(TestCase):

    def test_layout_line(self):
        src = """
        print:
          -x
          (+ 1 -y)
        """
        expected = pycons(
            (symbol('print'),
             (symbol('-'), symbol('x')),
             (symbol('+'), 1, (symbol('-'), symbol('y')))))
        self.assertEqual(read_layout(src), expected)


    def test_pragma_reaches_both_readers(self):
        reader = LayoutReader.from_pragma({}, ModulePragma())
        self.assertTrue(reader.unary)
        self.assertTrue(reader._inner.unary)


# The end.
