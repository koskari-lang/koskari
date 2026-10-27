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
tests.reader.test_layout_indent

The reader/indent policy: the first line of a block body sets the body's
indent, every later content line must start at that column, and a
top-level form starts at column 0. The pragma chooses between an error,
a warning, and reading the line as a sibling form.
"""

import warnings
from textwrap import dedent
from unittest import TestCase

import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderSyntaxError
from koskari.lib import symbol
from koskari.reader import (
    IndentAwareStream, LayoutReader, READ_EOF, source_str)
from koskari.reader.pragma import IndentMismatch

from . import parse_layout


pytestmark = pytest.mark.phase_infra


def read_all(src, layout_indent=None):
    reader = LayoutReader(dict(_BUILTIN_REGISTRY))
    if layout_indent is not None:
        reader.set_layout_modes(layout_indent=layout_indent)
    stream = IndentAwareStream.from_source(
        source_str(dedent(src), '<unittest>'))
    forms = []
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            return forms
        forms.append(form)


class TestIndentErrors(TestCase):

    def test_deeper_line_in_a_body_is_an_error(self):
        src = """
        def run []:
          compile_file src dest
              source_filename: name
        """
        with self.assertRaisesRegex(ReaderSyntaxError, 'unexpected indent'):
            parse_layout(src)


    def test_shallower_line_still_inside_the_body_is_an_error(self):
        src = """
        def run []:
            first
          second
        """
        with self.assertRaisesRegex(
                ReaderSyntaxError, 'unindent does not match'):
            parse_layout(src)


    def test_indented_top_level_line_is_an_error(self):
        src = """
        define x
            (+ 1 2)
        """
        with self.assertRaisesRegex(ReaderSyntaxError, 'unexpected indent'):
            read_all(src)


    def test_error_names_the_offending_line(self):
        src = """
        print:
          a
             b
        """
        with self.assertRaises(ReaderSyntaxError) as caught:
            parse_layout(src)
        self.assertEqual(caught.exception.location[0], 4)


class TestIndentAllowed(TestCase):

    def test_consistent_body_reads(self):
        src = """
        print:
          a
          b
        """
        expected = pycons((symbol('print'), symbol('a'), symbol('b')))
        self.assertEqual(parse_layout(src), expected)


    def test_nested_blocks_each_set_their_own_indent(self):
        src = """
        print:
          when x:
                a
                b
          c
        """
        expected = pycons(
            (symbol('print'),
             (symbol('when'), symbol('x'), symbol('a'), symbol('b')),
             symbol('c')))
        self.assertEqual(parse_layout(src), expected)


    def test_continuation_inside_delimiters_is_free(self):
        src = """
        print:
          (foo a
                b
            c)
          d
        """
        expected = pycons(
            (symbol('print'),
             (symbol('foo'), symbol('a'), symbol('b'), symbol('c')),
             symbol('d')))
        self.assertEqual(parse_layout(src), expected)


    def test_comment_lines_are_not_measured(self):
        src = """
        print:
          a
              ;; a deeper comment
        ;; a shallower comment
          b
        """
        expected = pycons((symbol('print'), symbol('a'), symbol('b')))
        self.assertEqual(parse_layout(src), expected)


    def test_peers_read_at_the_header_column(self):
        src = """
        if x:
          a
        else:
          b
        """
        result = parse_layout(src)
        self.assertEqual(result.car, symbol('cond'))
        self.assertEqual(len(list(result.unpack())), 3)


class TestIndentPolicy(TestCase):

    def test_ignore_reads_the_line_as_a_sibling(self):
        src = """
        define x
            (+ 1 2)
        """
        forms = read_all(src, IndentMismatch.IGNORE)
        self.assertEqual(forms, [
            pycons((symbol('define'), symbol('x'))),
            pycons((symbol('+'), 1, 2))])


    def test_warn_reads_the_line_and_warns(self):
        src = """
        define x
            (+ 1 2)
        """
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            forms = read_all(src, 'warn')
        self.assertEqual(len(forms), 2)
        self.assertEqual(len(caught), 1)
        self.assertIs(caught[0].category, SyntaxWarning)
        self.assertIn('unexpected indent', str(caught[0].message))


    def test_pragma_key_drives_the_reader(self):
        from koskari.pragma import ModulePragma

        pragma = ModulePragma()
        reader = LayoutReader.from_pragma(dict(_BUILTIN_REGISTRY), pragma)
        self.assertIs(reader.layout_indent, IndentMismatch.ERROR)


# The end.
