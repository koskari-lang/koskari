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
koskari.lexer.prompt_toolkit.lexer

Koskari syntax highlighting for prompt_toolkit.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


from collections import defaultdict

from prompt_toolkit.lexers import Lexer

from ..tokens import tokenize


_TOKEN_TYPE_TO_CLASS = {
    'symbol': 'class:koskari.symbol',
    'keyword': 'class:koskari.keyword',
    'string': 'class:koskari.string',
    'number': 'class:koskari.number',
    'comment': 'class:koskari.comment',
    'special': 'class:koskari.special',
    'layout': 'class:koskari.layout',
    'paren_open': 'class:koskari.punctuation',
    'paren_close': 'class:koskari.punctuation',
    'bracket_open': 'class:koskari.punctuation',
    'bracket_close': 'class:koskari.punctuation',
    'brace_open': 'class:koskari.punctuation',
    'brace_close': 'class:koskari.punctuation',
    'collection_open': 'class:koskari.punctuation',
    'dot': 'class:koskari.punctuation',
    'comma': 'class:koskari.punctuation',
    'quote': 'class:koskari.punctuation',
    'quasiquote': 'class:koskari.punctuation',
    'unquote_splicing': 'class:koskari.punctuation',
}


def _style_class(tok_type):
    return _TOKEN_TYPE_TO_CLASS.get(tok_type, 'class:koskari.symbol')


class KoskariLexer(Lexer):
    """
    prompt_toolkit Lexer for Koskari source.

    Provides syntax highlighting via tokenize. Handles partial input
    by returning highlighting for tokens produced before any error.
    """

    def lex_document(self, document):
        """
        Return a callable that maps line numbers to style tuples.

        :param document: prompt_toolkit Document
        :return: callable(lineno) -> list of (style_class, text) tuples
        """

        try:
            tokens = list(tokenize(document.text, '<stdin>'))
        except Exception:
            tokens = []

        line_to_tokens = defaultdict(list)
        for tok in tokens:
            line_to_tokens[tok.start_line].append(tok)

        def get_line(lineno):
            lines = document.lines
            if lineno < 0 or lineno >= len(lines):
                return []
            line_no = lineno + 1
            raw = lines[lineno]
            toks = sorted(line_to_tokens.get(line_no, []), key=lambda t: t.start_col)
            default_style = ''
            if not toks:
                return [(default_style, raw)]
            parts = []
            col = 1
            for tok in toks:
                if tok.start_col > col:
                    parts.append((default_style, raw[col - 1:tok.start_col - 1]))
                parts.append((_style_class(tok.type), tok.text))
                col = tok.end_col
            if col <= len(raw):
                parts.append((default_style, raw[col - 1:]))
            return parts

        return get_line


def _make_style():
    from prompt_toolkit.styles import Style
    return Style.from_dict({
        'koskari.keyword': '#859900',
        'koskari.string': '#b58900',
        'koskari.number': '#2aa198',
        'koskari.comment': '#93a1a1',
        'koskari.special': '#268bd2',
        'koskari.layout': '#6c71c4',
        'koskari.symbol': '#839496',
        'koskari.punctuation': '#586e75',
    })


koskari_style = _make_style()


__all__ = (
    'KoskariLexer',
    'koskari_style',
)


# The end.
