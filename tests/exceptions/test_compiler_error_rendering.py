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
Tests for rich CompilerError rendering.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest
from tempfile import NamedTemporaryFile

import pytest

from koskari.exceptions import CompilerError, KoskariSyntaxError


pytestmark = pytest.mark.phase_infra


class _SourceWithPosition:

    def __init__(self, pos, text):
        self._pos = pos
        self._text = text

    def get_position(self):
        return self._pos

    def __repr__(self):
        return self._text


class TestCompilerErrorRendering(unittest.IsolatedAsyncioTestCase):

    async def test_str_includes_line_col_and_source(self):
        src = _SourceWithPosition((12, 7), "(bad form)")
        err = CompilerError("no matching define-syntax rule", src)
        text = str(err)

        self.assertIn("no matching define-syntax rule", text)
        self.assertIn("line 12, col 7", text)
        self.assertIn("source (bad form)", text)


    async def test_explicit_position_overrides_source_position(self):
        src = _SourceWithPosition((2, 3), "(source)")
        err = CompilerError("mismatch", src, position=(90, 4))
        text = str(err)

        self.assertIn("line 90, col 4", text)
        self.assertNotIn("line 2, col 3", text)


    async def test_exception_message_is_type_prefixed(self):
        src = _SourceWithPosition((1, 1), "(x)")
        err = CompilerError(ValueError("boom"), src)
        text = str(err)

        self.assertIn("ValueError: boom", text)
        self.assertIn("line 1, col 1", text)


    async def test_str_without_source_or_position(self):
        err = CompilerError("plain message")
        text = str(err)

        self.assertEqual("plain message", text)


    async def test_str_includes_source_line_and_caret(self):
        with NamedTemporaryFile("w+", suffix=".ki", delete=True) as src:
            src.write("(define x 1)\n(+ x 2)\n")
            src.flush()

            err = CompilerError("bad form", position=(2, 3), filename=src.name)
            text = str(err)

        self.assertIn("line 2, col 3", text)
        self.assertIn("-> %s:2" % src.name, text)
        self.assertIn("(+ x 2)", text)
        self.assertIn("\n      ^", text)


class TestKoskariSyntaxErrorOffset(unittest.TestCase):
    """
    The reader's 0-based column becomes SyntaxError's 1-based offset,
    so the traceback caret lands under the column the reader reported
    """

    def test_offset_is_the_column_plus_one(self):
        err = KoskariSyntaxError(
            "mismatched", location=(1, 8), filename="<t>", text="(foo bar]")
        self.assertEqual(err.lineno, 1)
        self.assertEqual(err.offset, 9)
        self.assertEqual(err.location, (1, 8))

    def test_caret_sits_under_the_reported_column(self):
        err = KoskariSyntaxError(
            "mismatched", location=(1, 8), filename="<t>", text="(foo bar]")
        source_line, caret_line = [
            line for line in err.explain().splitlines()
            if line.strip() in ("(foo bar]", "^")]
        self.assertEqual(
            caret_line.index("^") - source_line.index("("), 8)


# The end.
