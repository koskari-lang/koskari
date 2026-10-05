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
Tests for the REPL's Tab and Shift+Tab indent key bindings

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.document import Document

from koskari.lexer.prompt_toolkit.key_bindings import (
    _replace_line_leading_indent)


pytestmark = pytest.mark.phase_infra


def _buffer(text, cursor):
    buf = Buffer()
    buf.set_document(Document(text, cursor), bypass_readonly=True)
    return buf


class TestReplaceLineLeadingIndent(unittest.TestCase):

    def test_cursor_past_the_indent_keeps_its_distance_from_content(self):
        buf = _buffer('        x', 9)
        _replace_line_leading_indent(buf, 4)
        self.assertEqual(buf.text, '    x')
        self.assertEqual(buf.cursor_position, 5)


    def test_cursor_inside_a_removed_indent_stays_on_the_line(self):
        buf = _buffer('        x', 2)
        _replace_line_leading_indent(buf, 4)
        self.assertEqual(buf.text, '    x')
        self.assertEqual(buf.cursor_position, 2)


    def test_cursor_inside_the_indent_is_capped_at_the_new_indent(self):
        buf = _buffer('        x', 6)
        _replace_line_leading_indent(buf, 4)
        self.assertEqual(buf.text, '    x')
        self.assertEqual(buf.cursor_position, 4)


    def test_cursor_inside_the_indent_on_a_later_line_stays_on_it(self):
        text = 'first\n        x'
        line_start = len('first\n')
        buf = _buffer(text, line_start + 2)
        _replace_line_leading_indent(buf, 4)
        self.assertEqual(buf.text, 'first\n    x')
        self.assertEqual(buf.cursor_position, line_start + 2)
        self.assertEqual(buf.document.cursor_position_row, 1)


    def test_tab_from_the_indent_end_follows_the_content(self):
        buf = _buffer('  x', 2)
        _replace_line_leading_indent(buf, 4)
        self.assertEqual(buf.text, '    x')
        self.assertEqual(buf.cursor_position, 4)


# The end.
