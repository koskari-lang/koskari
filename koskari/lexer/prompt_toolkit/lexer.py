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

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


from prompt_toolkit.lexers import Lexer

from ..tokens import SPECIAL_NAMES, tokenize


_TOKEN_TYPE_TO_CLASS = {
    'symbol': 'class:koskari.symbol',
    'keyword': 'class:koskari.keyword',
    'ellipsis': 'class:koskari.ellipsis',
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


def _slice_token_on_line(tok, line_no, raw_line):
    """
    Return (start_idx, end_idx) 0-based half-open indices into raw_line for
    the portion of tok that lies on line_no. end_idx is exclusive.

    :param tok: Token from tokenize()
    :param line_no: 1-based line number
    :param raw_line: text of that line only
    :return: (start_idx, end_idx) or None if tok does not intersect line_no
    """

    if not (tok.start_line <= line_no <= tok.end_line):
        return None
    n = len(raw_line)
    if tok.start_line == tok.end_line:
        return (tok.start_col - 1, tok.end_col - 1)
    if line_no == tok.start_line:
        return (tok.start_col - 1, n)
    if line_no == tok.end_line:
        return (0, tok.end_col - 1)
    return (0, n)


def _sort_key_token_on_line(tok, line_no):
    """
    Column order for tokens on a line (1-based). Continuation of a multi-line
    token starts at column 1.
    """

    if tok.start_line == line_no:
        return tok.start_col
    return 1


class KoskariLexer(Lexer):
    """
    prompt_toolkit Lexer for Koskari source.

    Provides syntax highlighting via tokenize. Handles partial input
    by returning highlighting for tokens produced before any error.
    """

    def __init__(self, special_names=None):
        """
        :param special_names: optional set of compiler special names for
            atom classification; defaults to ``SPECIAL_NAMES``
        """

        self._special_names = (
            special_names if special_names is not None else SPECIAL_NAMES)

    def lex_document(self, document):
        """
        Return a callable that maps line numbers to style tuples.

        :param document: prompt_toolkit Document
        :return: callable(lineno) -> list of (style_class, text) tuples
        """

        try:
            tokens = list(tokenize(
                document.text, '<stdin>', special_names=self._special_names))
        except Exception:
            tokens = []

        def get_line(lineno):
            lines = document.lines
            if lineno < 0 or lineno >= len(lines):
                return []
            line_no = lineno + 1
            raw = lines[lineno]
            default_style = ''
            line_toks = [
                t for t in tokens
                if t.start_line <= line_no <= t.end_line]
            line_toks.sort(key=lambda t: _sort_key_token_on_line(t, line_no))
            if not line_toks:
                return [(default_style, raw)]
            parts = []
            col = 1
            for tok in line_toks:
                bounds = _slice_token_on_line(tok, line_no, raw)
                if bounds is None:
                    continue
                start_idx, end_idx = bounds
                line_start_col = start_idx + 1
                if line_start_col > col:
                    parts.append((default_style, raw[col - 1:line_start_col - 1]))
                parts.append((
                    _style_class(tok.type),
                    raw[start_idx:end_idx]))
                col = end_idx + 1
            if col <= len(raw):
                parts.append((default_style, raw[col - 1:]))
            return parts

        return get_line


def _make_style():
    from prompt_toolkit.styles import Style
    return Style.from_dict({
        'koskari.keyword': '#859900',
        'koskari.ellipsis': '#2aa198',
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
