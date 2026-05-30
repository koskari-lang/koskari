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
koskari.lexer.tokens

Token type, tokenizer, and name sets for lexical analysis.

Provides Token, tokenize, SPECIAL_NAMES, LAYOUT_NAMES, LAYOUT_HEAD_NAMES,
LAYOUT_PEER_NAMES, and LAYOUT_BLOCK_NAMES for syntax highlighting and completion
without compilation.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


from collections import namedtuple
from re import compile as regex
from re import UNICODE


__all__ = (
    'Token',
    'tokenize',
    'SPECIAL_NAMES',
    'LAYOUT_NAMES',
    'LAYOUT_HEAD_NAMES',
    'LAYOUT_PEER_NAMES',
    'LAYOUT_BLOCK_NAMES',
)

Token = namedtuple(
    'Token',
    ('type', 'text', 'start_line', 'start_col', 'end_line', 'end_col'),
)


def _make_special_names():
    import koskari.builtins as builtins
    from ..compile.compiler import Compiled
    return frozenset(n for n, v in vars(builtins).items()
                     if isinstance(v, Compiled))


def _make_layout_names():
    import koskari.builtins as builtins
    from ..reader.layouts.helpers import LayoutHandler
    return frozenset(n for n, v in vars(builtins).items()
                     if isinstance(v, LayoutHandler))


def _make_layout_head_names():
    col = []
    for name in _make_layout_names():
        if name.startswith('layout/'):
            col.append(name[7:])
    return frozenset(col)


def _make_layout_peer_names():
    import koskari.builtins as builtins
    from koskari.reader.layouts.helpers import LayoutHandler
    result = []
    for v in vars(builtins).values():
        if not isinstance(v, LayoutHandler):
            continue
        for peer in v.peers:
            result.append(str(peer))
    return frozenset(result)


SPECIAL_NAMES = _make_special_names()
LAYOUT_NAMES = _make_layout_names()
LAYOUT_HEAD_NAMES = _make_layout_head_names()
LAYOUT_PEER_NAMES = _make_layout_peer_names()
LAYOUT_BLOCK_NAMES = frozenset(LAYOUT_HEAD_NAMES | LAYOUT_PEER_NAMES)


_ATOM_TERMINATORS = ' \t\n\r()[]{}"\';`,'
_ATOM_TERMINATOR_SET = frozenset(_ATOM_TERMINATORS)
_BRACKET_TOKENS = {
    '(': 'paren_open',
    ')': 'paren_close',
    '[': 'bracket_open',
    ']': 'bracket_close',
    '{': 'brace_open',
    '}': 'brace_close',
}

_integer_re = regex(r"^-?\d+$").match
_bin_re = regex(r"^0b[01]+$").match
_oct_re = regex(r"^0o[0-7]+$").match
_hex_re = regex(r"^0x[\da-fA-F]+$").match
_float_re = regex(
    r"^-?("
    r"(\d*\.\d+|\d+\.\d*)(e-?\d+)?f?"
    r"|(\d+e-?\d+)f?"
    r"|\d+f"
    ")$").match
_decimal_re = regex(r"^-?((\d*\.\d+)|(\d+\.\d*)|\d+)d$").match
_fraction_re = regex(r"-?\d+/\d+").match
_complex_re = regex(r"^-?\d*\.?\d+\+\d*\.?\d*[ij]$").match
_keyword_re = regex(r"^(:.+|.+:)$", UNICODE).match
_NUMBER_MATCHERS = (
    _integer_re,
    _bin_re,
    _oct_re,
    _hex_re,
    _float_re,
    _decimal_re,
    _fraction_re,
    _complex_re,
)


def _atom_type(text, special_names):
    """
    Classify an atom string as number, keyword, ellipsis, py_singleton,
    special, layout, or symbol.

    :param text: the atom string
    :param special_names: set of names to treat as compiler specials
    :return: token type string
    """

    for matcher in _NUMBER_MATCHERS:
        if matcher(text):
            return 'number'
    if _keyword_re(text):
        return 'keyword'
    if text == '...':
        return 'ellipsis'
    if text in ("None", "True", "False"):
        return 'py_singleton'
    if text in special_names:
        return 'special'
    if text in LAYOUT_BLOCK_NAMES:
        return 'layout'
    return 'symbol'


def _advance_pos(line, col, text):
    """
    Advance (line, col) by the length of text, counting newlines.

    :param line: current line (1-based)
    :param col: current column (1-based)
    :param text: consumed text
    :return: (new_line, new_col)
    """

    for ch in text:
        if ch == '\n':
            line += 1
            col = 1
        else:
            col += 1
    return line, col


def _scan_atom_end(source, start, n):
    """Return index one-past the end of an atom token."""

    i = start
    while i < n and source[i] not in _ATOM_TERMINATOR_SET:
        i += 1
    return i


def _try_layout_block_split(text, start_line, start_col, sn):
    """
    If text is ``prefix:`` with prefix in ``LAYOUT_BLOCK_NAMES``, return a
    pair of Tokens (prefix, colon). Prefix type comes from ``_atom_type`` so
    specials win over layout. Otherwise return None.
    """

    if len(text) <= 1 or not text.endswith(':'):
        return None
    prefix = text[:-1]
    if prefix not in LAYOUT_BLOCK_NAMES:
        return None
    tok_type = _atom_type(prefix, sn)
    lp, cp = _advance_pos(start_line, start_col, prefix)
    first = Token(tok_type, prefix, start_line, start_col, lp, cp)
    lc, cc = _advance_pos(lp, cp, ':')
    second = Token('colon', ':', lp, cp, lc, cc)
    return (first, second)


def tokenize(source, filename='<string>', special_names=None):
    """
    Yield Token instances for the given source.

    Position fields are 1-based. On malformed input (e.g. unclosed string),
    yields tokens up to the error and stops.

    :param source: source string
    :param filename: filename for error context (unused for now)
    :param special_names: optional set of special-form names; defaults to
        ``SPECIAL_NAMES`` from the compiler registry
    :yield: Token instances
    """

    # TODO: Look at this with human eyes, it seems like a lot of code
    # that is probably duplicating the original reader, but slower

    sn = special_names if special_names is not None else SPECIAL_NAMES

    line = 1
    col = 1
    i = 0
    n = len(source)
    sl = source

    while i < n:
        start_line = line
        start_col = col
        ch = sl[i]
        i += 1
        line, col = _advance_pos(line, col, ch)

        if ch in ' \t\n\r':
            continue

        if ch == ';':
            end = sl.find('\n', i)
            if end < 0:
                end = n
            text = sl[i - 1:end]
            line, col = _advance_pos(line, col, text[1:])
            i = end
            yield Token('comment', text, start_line, start_col, line, col)
            continue

        if ch in '()[]{}':
            yield Token(_BRACKET_TOKENS[ch], ch, start_line, start_col, line, col)
            continue

        if ch == '.':
            if i + 1 < n and sl[i] == '.' and sl[i + 1] == '.':
                i += 2
                line, col = _advance_pos(line, col, '..')
                yield Token(
                    'ellipsis',
                    '...',
                    start_line,
                    start_col,
                    line, col)
                continue
            yield Token('dot', ch, start_line, start_col, line, col)
            continue

        if ch == ',':
            if i < n and sl[i] == '@':
                i += 1
                line, col = _advance_pos(line, col, '@')
                yield Token(
                    'unquote_splicing',
                    ',@',
                    start_line,
                    start_col,
                    line, col)
            else:
                yield Token('comma', ch, start_line, start_col, line, col)
            continue

        if ch == "'":
            yield Token('quote', ch, start_line, start_col, line, col)
            continue

        if ch == '`':
            yield Token('quasiquote', ch, start_line, start_col, line, col)
            continue

        if ch == '"':
            text = ch
            triple = i + 2 <= n and sl[i:i + 2] == '""'
            if triple:
                text += sl[i:i + 2]
                i += 2
            while i < n:
                c = sl[i]
                i += 1
                text += c
                if c == '\\' and i < n:
                    text += sl[i]
                    i += 1
                elif c == '"':
                    if triple and i + 1 < n and sl[i:i + 2] == '""':
                        text += sl[i:i + 2]
                        i += 2
                        break
                    elif not triple:
                        break
            line, col = _advance_pos(line, col, text[1:])
            yield Token('string', text, start_line, start_col, line, col)
            continue

        if ch in 'fFrR' and i < n and sl[i] == '"':
            text = ch + sl[i]
            i += 1
            if i + 2 <= n and sl[i:i + 2] == '""':
                text += sl[i:i + 2]
                i += 2
                while i + 2 <= n and sl[i:i + 3] != '"""':
                    c = sl[i]
                    i += 1
                    text += c
                    if c == '\\' and i < n:
                        text += sl[i]
                        i += 1
                if i + 2 <= n:
                    text += sl[i:i + 3]
                    i += 3
            else:
                while i < n:
                    c = sl[i]
                    i += 1
                    text += c
                    if c == '\\' and i < n:
                        text += sl[i]
                        i += 1
                    elif c == '"':
                        break
            line, col = _advance_pos(line, col, text[1:])
            yield Token('string', text, start_line, start_col, line, col)
            continue

        if ch == '#':
            if i < n and sl[i] in '({[':
                text = ch + sl[i]
                i += 1
                line, col = _advance_pos(line, col, text[1:])
                yield Token(
                    'collection_open',
                    text,
                    start_line,
                    start_col,
                    line, col)
            else:
                end = _scan_atom_end(sl, i, n)
                text = sl[i - 1:end]
                i = end
                split = _try_layout_block_split(text, start_line, start_col, sn)
                if split is not None:
                    first, second = split
                    yield first
                    yield second
                    line, col = _advance_pos(start_line, start_col, text)
                    continue
                line, col = _advance_pos(line, col, text[1:])
                tok_type = (
                    _atom_type(text, sn) if text != '#' else 'symbol')
                yield Token(tok_type, text, start_line, start_col, line, col)
            continue

        end = _scan_atom_end(sl, i, n)
        text = sl[i - 1:end]
        i = end
        split = _try_layout_block_split(text, start_line, start_col, sn)
        if split is not None:
            first, second = split
            yield first
            yield second
            line, col = _advance_pos(start_line, start_col, text)
            continue
        line, col = _advance_pos(line, col, text[1:])
        tok_type = _atom_type(text, sn)
        yield Token(tok_type, text, start_line, start_col, line, col)


# The end.
