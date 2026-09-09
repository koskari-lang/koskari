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
tests.reader.test_reader_eof

ReaderEOF vs READ_EOF and ReaderSyntaxError during parse.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from unittest import TestCase

from koskari.exceptions import ReaderEOF, ReaderSyntaxError
from koskari.lib import ModuleNamespace
from koskari.reader import READ_EOF, LayoutReader, default_reader, source_str

import pytest

pytestmark = pytest.mark.phase_infra


class TestReaderEOF(TestCase):

    def test_empty_stream_is_read_eof(self):
        result = default_reader.read(source_str("", "<t>"))
        self.assertIs(result, READ_EOF)


    def test_complete_form_parses(self):
        result = default_reader.read(source_str("(+ 1 2)", "<t>"))
        self.assertTrue(result is not None)
        self.assertIsNot(result, READ_EOF)


    def test_unclosed_list_raises_reader_eof(self):
        with self.assertRaises(ReaderEOF) as ctx:
            default_reader.read(source_str("(+ 1", "<t>"))
        self.assertNotIsInstance(ctx.exception, ReaderSyntaxError)


    def test_unclosed_string_raises_reader_eof(self):
        with self.assertRaises(ReaderEOF):
            default_reader.read(source_str('"hello', "<t>"))


    def test_mismatched_bracket_stays_syntax_error(self):
        with self.assertRaises(ReaderSyntaxError):
            default_reader.read(source_str("( 1 2 ]", "<t>"))


class TestLayoutReaderEOF(TestCase):

    def _reader(self):
        ns = ModuleNamespace("test", {})
        return LayoutReader(ns)


    def test_unclosed_list_in_layout_line_raises_reader_eof(self):
        reader = self._reader()
        with self.assertRaises(ReaderEOF):
            reader.read(source_str("(+ 1", "<t>"))


    def test_empty_layout_stream_is_read_eof(self):
        reader = self._reader()
        self.assertIs(reader.read(source_str("", "<t>")), READ_EOF)


class TestReaderEOFNamesOpener(TestCase):
    """
    An unfinished form's EOF error is located at the form's opener and
    names it, wherever the input actually ran out
    """

    def read(self, text):
        return default_reader.read(source_str(text, "<t>"))

    def test_unclosed_list_points_at_its_paren(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read("(+ 1 2")
        self.assertEqual(ctx.exception.location, (1, 0))
        self.assertIn("'(' form opened at line 1, column 0",
                      ctx.exception.message)

    def test_innermost_unclosed_list_is_the_one_named(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read("(a\n  (b c")
        self.assertEqual(ctx.exception.location, (2, 2))

    def test_unclosed_string_points_at_its_quote(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read('(a "hello')
        self.assertEqual(ctx.exception.location, (1, 3))
        self.assertIn("string opened at line 1, column 3",
                      ctx.exception.message)

    def test_unclosed_prefixed_string_points_at_its_prefix(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read(' b"abc')
        self.assertEqual(ctx.exception.location, (1, 1))

    def test_unclosed_triple_string_is_named(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read('"""abc\n"')
        self.assertEqual(ctx.exception.location, (1, 0))
        self.assertIn("triple-quoted string", ctx.exception.message)

    def test_quote_with_nothing_after_it(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read("(a '")
        self.assertEqual(ctx.exception.location, (1, 3))
        self.assertIn("quote opened at", ctx.exception.message)

    def test_unterminated_fstring_is_still_incomplete_input(self):
        with self.assertRaises(ReaderEOF) as ctx:
            self.read('f"abc {x}')
        self.assertEqual(ctx.exception.location, (1, 0))


class TestCompleteFStringWithOpenField(TestCase):
    """
    A field never closed inside a complete f-string is a syntax error at
    the field's brace, never a request for more input
    """

    def read(self, text):
        return default_reader.read(source_str(text, "<t>"))

    def test_unclosed_field_is_a_syntax_error_at_the_brace(self):
        with self.assertRaises(ReaderSyntaxError) as ctx:
            self.read('f"abc {"')
        self.assertNotIsInstance(ctx.exception, ReaderEOF)
        self.assertEqual(ctx.exception.location, (1, 6))
        self.assertIn("f-string field opened at line 1, column 6",
                      ctx.exception.message)

    def test_incomplete_field_expression_is_a_syntax_error(self):
        with self.assertRaises(ReaderSyntaxError) as ctx:
            self.read('f"{(foo}"')
        self.assertNotIsInstance(ctx.exception, ReaderEOF)

    def test_unclosed_spec_is_a_syntax_error(self):
        with self.assertRaises(ReaderSyntaxError) as ctx:
            self.read('f"{x:"')
        self.assertNotIsInstance(ctx.exception, ReaderEOF)


# The end.
