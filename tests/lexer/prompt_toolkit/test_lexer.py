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
tests.lexer.prompt_toolkit.test_lexer

Unit tests for koskari.lexer.prompt_toolkit.
"""

from unittest import IsolatedAsyncioTestCase
from prompt_toolkit.document import Document
from koskari.lexer.prompt_toolkit import KoskariLexer, koskari_style

import pytest

pytestmark = pytest.mark.phase_infra


class TestKoskariLexer(IsolatedAsyncioTestCase):

    async def test_lexer_returns_callable(self):
        lexer = KoskariLexer()
        doc = Document('(foo 1)')
        get_line = lexer.lex_document(doc)
        self.assertTrue(callable(get_line))


    async def test_lexer_line_zero_has_tokens(self):
        lexer = KoskariLexer()
        doc = Document('(foo 42)')
        get_line = lexer.lex_document(doc)
        parts = get_line(0)
        self.assertGreater(len(parts), 0)
        styles_and_texts = [(s, t) for s, t in parts]
        text_joined = ''.join(t for _, t in parts)
        self.assertEqual(text_joined, '(foo 42)')


    async def test_lexer_string_has_style(self):
        lexer = KoskariLexer()
        doc = Document('"hello"')
        get_line = lexer.lex_document(doc)
        parts = get_line(0)
        has_string_style = any(
            s and 'koskari.string' in str(s) for s, _ in parts)
        self.assertTrue(has_string_style)


    async def test_lexer_keyword_has_style(self):
        lexer = KoskariLexer()
        doc = Document(':foo')
        get_line = lexer.lex_document(doc)
        parts = get_line(0)
        has_keyword_style = any(
            s and 'koskari.keyword' in str(s) for s, _ in parts)
        self.assertTrue(has_keyword_style)

    async def test_lexer_multiline_triple_string_reconstructs_lines(self):
        """
        Multi-line string tokens must be sliced per line; using full tok.text
        on the first line only corrupts the display (phantom characters).
        """

        text = (
            'define foobar []:\n'
            '    """ this is a docstring\n'
            '    """\n')
        lexer = KoskariLexer()
        doc = Document(text)
        get_line = lexer.lex_document(doc)
        for lineno in range(len(doc.lines)):
            parts = get_line(lineno)
            joined = ''.join(t for _, t in parts)
            self.assertEqual(joined, doc.lines[lineno])


class TestKoskariStyle(IsolatedAsyncioTestCase):

    async def test_style_is_not_none(self):
        self.assertIsNotNone(koskari_style)


# The end.
