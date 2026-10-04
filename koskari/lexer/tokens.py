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
without compilation. The name sets are built from the builtins namespace on
first use, so importing this module does not load the bootstrap.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from collections import namedtuple

from ..reader.patterns import (
    BIN_RE, COMPLEX_RE, DECIMAL_RE, FLOAT_RE, FRACTION_RE, HEX_RE, INTEGER_RE,
    KEYWORD_RE, OCT_RE, STRING_PREFIXES)


Token = namedtuple(
    'Token',
    ('type', 'text', 'start_line', 'start_col', 'end_line', 'end_col'),
)


LexerNames = namedtuple(
    'LexerNames', ('special', 'layout', 'head', 'peer', 'block'))

# module attribute -> LexerNames field, served by __getattr__
LAZY_NAMES = {
    'SPECIAL_NAMES': 'special',
    'LAYOUT_NAMES': 'layout',
    'LAYOUT_HEAD_NAMES': 'head',
    'LAYOUT_PEER_NAMES': 'peer',
    'LAYOUT_BLOCK_NAMES': 'block',
}

__all__ = ('Token', 'tokenize') + tuple(LAZY_NAMES)

names = None


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


def _make_layout_head_names(layout_names):
    col = []
    for name in layout_names:
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


def lexer_names():
    """
    The special and layout name sets, built from the builtins namespace
    on first use and cached. Importing this module therefore does not
    load the bootstrap; the first tokenize or attribute access does

    :return: a LexerNames of frozensets
    """

    global names

    if names is None:
        special = _make_special_names()
        layout = _make_layout_names()
        head = _make_layout_head_names(layout)
        peer = _make_layout_peer_names()
        names = LexerNames(special, layout, head, peer, frozenset(head | peer))
    return names


def __getattr__(name):
    field = LAZY_NAMES.get(name)
    if field is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    return getattr(lexer_names(), field)


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

_keyword_re = KEYWORD_RE.match
_NUMBER_MATCHERS = (
    INTEGER_RE.match,
    BIN_RE.match,
    OCT_RE.match,
    HEX_RE.match,
    FLOAT_RE.match,
    DECIMAL_RE.match,
    FRACTION_RE.match,
    COMPLEX_RE.match,
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
    if text in lexer_names().block:
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


def _scan_string_end(source, quote, n):
    """
    Return index one-past the end of a string literal opening at ``quote``.

    Handles triple-quoted strings and backslash escapes. An unterminated
    literal ends at end of source (tokenize yields what it has and stops).

    :param source: the full source string
    :param quote: index of the opening double-quote
    :param n: length of source
    :return: index one past the closing quote (or n if unterminated)
    """

    if source[quote:quote + 3] == '"""':
        i = quote + 3
        while i < n:
            c = source[i]
            if c == '\\':
                i += 2
            elif c == '"' and source[i:i + 3] == '"""':
                return i + 3
            else:
                i += 1
        return n

    i = quote + 1
    while i < n:
        c = source[i]
        if c == '\\':
            i += 2
        elif c == '"':
            return i + 1
        else:
            i += 1
    return n


def _rest_of_line_blank(source, start, n):
    """
    True when nothing but whitespace or a comment follows position start
    on its line
    """

    i = start
    while i < n and source[i] in ' \t\r':
        i += 1
    return i >= n or source[i] in '\n;'


def _try_layout_block_split(text, start_line, start_col, sn, opens_block):
    """
    If text is ``prefix:`` and the colon opens a layout block, return a
    pair of Tokens (prefix, colon); otherwise return None. A prefix in
    ``LAYOUT_BLOCK_NAMES`` opens a block anywhere; any other prefix opens
    one when opens_block says the colon ends a line outside all
    delimiters. Prefix type comes from ``_atom_type`` so specials win over
    layout.
    """

    if len(text) <= 1 or not text.endswith(':'):
        return None
    prefix = text[:-1]
    if not opens_block and prefix not in lexer_names().block:
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

    sn = special_names if special_names is not None else lexer_names().special

    line = 1
    col = 1
    i = 0
    n = len(source)
    sl = source
    depth = 0

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
            if ch in '([{':
                depth += 1
            elif depth:
                depth -= 1
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
            # the reader makes DOT only of a lone '.', or of the '.'
            # attached to a closed form or string as the attribute
            # suffix; any other leading dot begins an atom, .5 a float
            # and .name a symbol, which the atom path below classifies
            attached = i >= 2 and sl[i - 2] in ')]}"'
            if attached or _scan_atom_end(sl, i, n) == i:
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

        if ch == '-' and i < n and sl[i] == "'":
            # -' is the datumq prefix, highlighted as a quote
            i += 1
            line, col = _advance_pos(line, col, "'")
            yield Token('quote', "-'", start_line, start_col, line, col)
            continue

        if ch == '`':
            yield Token('quasiquote', ch, start_line, start_col, line, col)
            continue

        if ch == '"':
            end = _scan_string_end(sl, i - 1, n)
            text = sl[i - 1:end]
            i = end
            line, col = _advance_pos(line, col, text[1:])
            yield Token('string', text, start_line, start_col, line, col)
            continue

        if ch in STRING_PREFIXES and i < n and sl[i] == '"':
            end = _scan_string_end(sl, i, n)
            text = sl[i - 1:end]
            i = end
            line, col = _advance_pos(line, col, text[1:])
            yield Token('string', text, start_line, start_col, line, col)
            continue

        if ch == '#':
            if i < n and sl[i] in "'`":
                # #' and #` are the quote-syntax and quasiquote-syntax
                # prefixes, highlighted as their plain counterparts
                text = ch + sl[i]
                i += 1
                line, col = _advance_pos(line, col, text[1:])
                tok_type = 'quote' if text[1] == "'" else 'quasiquote'
                yield Token(tok_type, text, start_line, start_col, line, col)
                continue
            if i < n and sl[i] in '({[':
                text = ch + sl[i]
                i += 1
                depth += 1
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
                split = _try_layout_block_split(
                    text, start_line, start_col, sn,
                    not depth and _rest_of_line_blank(sl, end, n))
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
        split = _try_layout_block_split(
            text, start_line, start_col, sn,
            not depth and _rest_of_line_blank(sl, end, n))
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
