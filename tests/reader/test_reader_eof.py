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
:ai-assistant: Claude Opus 4.7 via Cursor
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


# The end.
