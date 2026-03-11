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
tests.lexer.test_tokens

Unit tests for koskari.lexer.tokens.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.lexer import (
    Token,
    tokenize,
    SPECIAL_NAMES,
    LAYOUT_NAMES,
    LAYOUT_HEAD_NAMES,
)

import pytest

pytestmark = pytest.mark.phase_infra


class TestToken(IsolatedAsyncioTestCase):

    async def test_token_named_tuple(self):
        t = Token('symbol', 'foo', 1, 0, 1, 3)
        self.assertEqual(t.type, 'symbol')
        self.assertEqual(t.text, 'foo')
        self.assertEqual(t.start_line, 1)
        self.assertEqual(t.start_col, 0)
        self.assertEqual(t.end_line, 1)
        self.assertEqual(t.end_col, 3)


class TestTokenize(IsolatedAsyncioTestCase):

    async def test_tokenize_symbol(self):
        toks = list(tokenize('foo'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'symbol')
        self.assertEqual(toks[0].text, 'foo')
        self.assertEqual(toks[0].start_line, 1)
        self.assertEqual(toks[0].start_col, 1)


    async def test_tokenize_string(self):
        toks = list(tokenize('"hello"'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'string')
        self.assertEqual(toks[0].text, '"hello"')


    async def test_tokenize_number(self):
        toks = list(tokenize('42'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'number')
        self.assertEqual(toks[0].text, '42')


    async def test_tokenize_keyword(self):
        toks = list(tokenize(':foo'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'keyword')
        self.assertEqual(toks[0].text, ':foo')


    async def test_tokenize_parens(self):
        toks = list(tokenize('( )'))
        self.assertEqual(len(toks), 2)
        self.assertEqual(toks[0].type, 'paren_open')
        self.assertEqual(toks[0].text, '(')
        self.assertEqual(toks[1].type, 'paren_close')
        self.assertEqual(toks[1].text, ')')


    async def test_tokenize_comment(self):
        toks = list(tokenize('; comment\n'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'comment')
        self.assertEqual(toks[0].text, '; comment')


    async def test_tokenize_s_expr(self):
        toks = list(tokenize('(foo bar)'))
        self.assertEqual(toks[0].type, 'paren_open')
        self.assertEqual(toks[1].type, 'symbol')
        self.assertEqual(toks[1].text, 'foo')
        self.assertEqual(toks[2].type, 'symbol')
        self.assertEqual(toks[2].text, 'bar')
        self.assertEqual(toks[3].type, 'paren_close')


    async def test_tokenize_special_form(self):
        toks = list(tokenize('(compile/define x 1)'))
        sym = next(t for t in toks if t.text == 'compile/define')
        self.assertEqual(sym.type, 'special')


    async def test_tokenize_layout_head(self):
        toks = list(tokenize('if'))
        self.assertEqual(len(toks), 1)
        self.assertEqual(toks[0].type, 'layout')
        self.assertEqual(toks[0].text, 'if')


    async def test_tokenize_quote(self):
        toks = list(tokenize("'x"))
        self.assertEqual(toks[0].type, 'quote')
        self.assertEqual(toks[0].text, "'")
        self.assertEqual(toks[1].type, 'symbol')


class TestNameSets(IsolatedAsyncioTestCase):

    async def test_special_names_non_empty(self):
        self.assertIsInstance(SPECIAL_NAMES, frozenset)
        self.assertGreater(len(SPECIAL_NAMES), 0)
        self.assertIn('compile/define', SPECIAL_NAMES)


    async def test_layout_names_non_empty(self):
        self.assertIsInstance(LAYOUT_NAMES, frozenset)
        self.assertGreater(len(LAYOUT_NAMES), 0)
        self.assertIn('layout/if', LAYOUT_NAMES)


    async def test_layout_head_names_contains_if(self):
        self.assertIsInstance(LAYOUT_HEAD_NAMES, frozenset)
        self.assertIn('if', LAYOUT_HEAD_NAMES)


# The end.
