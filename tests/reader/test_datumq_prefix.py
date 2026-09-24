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
tests.reader.test_datumq_prefix

The -' prefix: -'FORM reads as (datumq FORM), nesting as it is
repeated; every other - reads as it did before, whether or not the
unary prefixes are enabled

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

from textwrap import dedent
from unittest import TestCase

import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderError
from koskari.lib import symbol
from koskari.pragma import ModulePragma
from koskari.reader import (
    IndentAwareStream, LayoutReader, Reader, source_str)
from koskari.types import get_position, unpack


pytestmark = pytest.mark.phase_infra


def read_sexpr(src, unary=True):
    reader = Reader()
    reader.reload_pragma(ModulePragma())
    reader.unary = unary
    return reader.read(source_str(src, '<unittest>'))


def read_layout(src):
    reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY), ModulePragma())
    stream = IndentAwareStream.from_source(
        source_str(dedent(src), '<unittest>'))
    return reader.read(stream)


DATUMQ = symbol('datumq')


class TestDatumqPrefix(TestCase):


    def test_symbol(self):
        self.assertEqual(
            read_sexpr("-'foo"), pycons((DATUMQ, symbol('foo'))))


    def test_list(self):
        self.assertEqual(
            read_sexpr("-'(a b)"),
            pycons((DATUMQ, (symbol('a'), symbol('b')))))


    def test_nested(self):
        self.assertEqual(
            read_sexpr("-'-'foo"),
            pycons((DATUMQ, (DATUMQ, symbol('foo')))))


    def test_inside_a_list(self):
        self.assertEqual(
            read_sexpr("(a -'foo b)"),
            pycons((symbol('a'), (DATUMQ, symbol('foo')), symbol('b'))))


    def test_without_unary_prefixes(self):
        self.assertEqual(
            read_sexpr("-'foo", unary=False),
            pycons((DATUMQ, symbol('foo'))))


    def test_position_is_the_prefix(self):
        form = read_sexpr("(a\n   -'foo)")
        self.assertEqual(get_position(form.cdr.car), (2, 3))


    def test_eof_after_prefix(self):
        with self.assertRaises(ReaderError):
            read_sexpr("-'")


    def test_layout_reader(self):
        self.assertEqual(
            read_layout("define x -'foo\n"),
            pycons((symbol('define'), symbol('x'),
                    (DATUMQ, symbol('foo')))))


    def test_layout_header_keeps_its_block_colon(self):
        form = read_layout("frob &-'x:\n    bar\n")
        self.assertEqual(form.car, symbol('frob'))
        self.assertEqual(
            form.cdr.car, pycons((symbol('ref'), (DATUMQ, symbol('x')))))
        self.assertEqual(len(list(unpack(form))), 3)


class TestPrefixesInsideSexprInLayout(TestCase):
    """
    Inside parens and backticks the layout reader hands off to its
    s-expression reader, whose own prefix handlers see the temporary
    macros that reader installs, such as a backtick's unquote
    """

    QQ = symbol('quasiquote')
    UNQUOTE = symbol('unquote')
    REF = symbol('ref')


    def test_ref_of_unquote_in_backtick(self):
        self.assertEqual(
            read_layout("`(a &,b)\n"),
            pycons((self.QQ, (symbol('a'), (self.REF, (self.UNQUOTE,
                                                       symbol('b')))))))


    def test_ref_of_unquoted_datumq_in_backtick(self):
        self.assertEqual(
            read_layout("`(a &,-'x)\n"),
            pycons((self.QQ, (symbol('a'), (self.REF, (
                self.UNQUOTE, (DATUMQ, symbol('x'))))))))


    def test_ref_and_unary_inside_parens(self):
        self.assertEqual(
            read_layout("(f &x -y)\n"),
            pycons((symbol('f'), (self.REF, symbol('x')),
                    (symbol('-'), symbol('y')))))


    def test_ref_and_unary_in_a_header(self):
        form = read_layout("frob &x -y:\n    bar\n")
        self.assertEqual(form.cdr.car, pycons((self.REF, symbol('x'))))
        self.assertEqual(
            form.cdr.cdr.car, pycons((symbol('-'), symbol('y'))))


class TestOtherMinusUnchanged(TestCase):


    def test_negative_number(self):
        self.assertEqual(read_sexpr("-1"), -1)


    def test_arrow_symbol(self):
        self.assertEqual(read_sexpr("->"), symbol('->'))


    def test_unary_name(self):
        self.assertEqual(
            read_sexpr("-x"), pycons((symbol('-'), symbol('x'))))


    def test_lone_minus_call(self):
        self.assertEqual(
            read_sexpr("(- 'x)"),
            pycons((symbol('-'), (symbol('quote'), symbol('x')))))


    def test_minus_inside_an_atom(self):
        self.assertEqual(
            read_sexpr("(a-'b)"),
            pycons((symbol('a-'), (symbol('quote'), symbol('b')))))


# The end.
