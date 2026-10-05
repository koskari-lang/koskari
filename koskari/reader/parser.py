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
koskari.reader.parser

Koskari's s-expression reader. The Reader reads any SourceStream (see
koskari.reader.stream for the stream layers) with configurable reader
macros and atom patterns; FormatStringReader is its sub-reader for the
interior of format strings. LayoutReader, the layout coordinator built
on Reader, lives in koskari.reader.layout. No reader method inspects
private attributes of streams it receives; the stream interface is
uniform and well-defined.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from codecs import decode
from contextlib import contextmanager
from decimal import Decimal as decimal
from fractions import Fraction as fraction
from functools import partial
from re import UNICODE, VERBOSE
from re import compile as regex

from ..exceptions import FormatStringSyntaxError, ReaderEOF, ReaderSyntaxError
from ..lib import (
    bracket_nil, bracket_pair, cons, keyword, nil, pair, setcdr, symbol)
from ..matcher import (
    BIND, KEYWORD, LITERAL_KW, LITERAL_SYM, MATCH, ONEOF, repeat_capture)
from ..types import fill_position, is_keyword, is_nil, is_pair
from . import patterns as _patterns
from .stream import source_str


__all__ = (
    "ReaderSyntaxError",
    "ReaderEOF",
    "FormatStringSyntaxError",
    "Reader",
    "FormatStringReader",
    "default_reader",
    "READ_EOF",
    "DEFER",
    "LINE_END",
    "LINE",
    "BLOCK",
    "BLOCKS",
)


class ReadEOFType(object):
    """
    Sentinel returned by Reader.read at end of stream.

    Distinct from the Python None literal, which may appear as a parsed value.
    """

    def __repr__(self):
        return '<read EOF>'


READ_EOF = ReadEOFType()

_symbol__str = symbol("#str")
_symbol_gensym_thunk = symbol("gensym-thunk")
_symbol_decimal = symbol("decimal")
_symbol_format = symbol("format")
_symbol_fraction = symbol("fraction")
_symbol_quasiquote = symbol("quasiquote")
_symbol_quasiquote_syntax = symbol("quasiquote-syntax")
_symbol_attr = symbol("attr")
_symbol_quote = symbol("quote")
_symbol_datumq = symbol("datumq")
_symbol_quote_syntax = symbol("quote-syntax")
_symbol_decorate = symbol("decorate")
_symbol_ref = symbol("ref")
_unary_symbols = {c: symbol(c) for c in "-+~"}
_symbol_refq = symbol("refq")
_symbol_splice = symbol("splice")
_symbol_unquote = symbol("unquote")
_symbol_unquote_splicing = symbol("unquote-splicing")
_symbol_apply_fence = symbol("apply-fence")
_symbol_colon = symbol(":")
_symbol_tuple = symbol("#tuple")
_keyword_starstar = keyword("**")

# Splits a #dict spine into its key/value pairs. Each repeat is one of:
# a ``**: EXPR`` splat, an explicit ``KEY : VALUE`` triple, or a
# ``keyword: VALUE`` pair whose keyword key is normalised away.
_DICT_SPLIT_MATCHER = repeat_capture("pairs", ONEOF(
    MATCH(LITERAL_KW("**"), BIND("splat")),
    MATCH(BIND("key"), LITERAL_SYM(":"), BIND("val")),
    MATCH(KEYWORD("kw"), BIND("val")),
)).build()

_keyword_block = keyword(":block:")
_keyword_splice = keyword(":splice:")
_keyword_fence_filename = keyword("filename")
_keyword_fence_line = keyword("line")
_keyword_fence_col = keyword("col")
_keyword_fence_options = keyword("options")

VALUE = keyword("value")
BLOCK = keyword("block")
BLOCKS = keyword("blocks")
ATOM = keyword("atom")
DOT = keyword("dot")
SKIP = keyword("skip")
CLOSE_PAIR = keyword("close-pair")
EOF = keyword("eof")
COLON = keyword("colon")
LINE_END = keyword("line-end")
LINE = keyword("line")
DEFER = keyword("defer")

_BASE_TERMINATORS = frozenset("\n\r\t ")

_integer_re = _patterns.INTEGER_RE.match
_bin_re = _patterns.BIN_RE.match
_oct_re = _patterns.OCT_RE.match
_hex_re = _patterns.HEX_RE.match
_float_re = _patterns.FLOAT_RE.match
_decimal_re = _patterns.DECIMAL_RE.match
_fraction_re = _patterns.FRACTION_RE.match
_complex_re = _patterns.COMPLEX_RE.match
_keyword_re = _patterns.KEYWORD_RE.match
# deliberately wider than the well-formed shape: a trailing segment that
# is empty or not an identifier still matches, so _as_dotted_attr_atom can
# reject it by name rather than letting `foo.` fall through to a symbol.
# The leading identifier anchor is what keeps floats and ellipsis out.
_dotted_attr_atom_re = regex(
    r"^[a-zA-Z_][a-zA-Z0-9/_-]*(\.[a-zA-Z0-9/_-]*)+$").match
_attr_name_re = regex(r"^[a-zA-Z_][a-zA-Z0-9/_-]*$").match

# the same name shape as _attr_name_re, as character sets, for the reader's
# `.` suffix macro to scan with
_ATTR_NAME_HEAD = frozenset(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_")
_ATTR_NAME_REST = _ATTR_NAME_HEAD | frozenset("0123456789/-")
_ellipsis_atom_re = regex(r"^\.\.\.$").match
_py_singleton_atom_re = regex(r"^(None|True|False)$").match


_as_integer = int
_as_bin = partial(int, base=2)
_as_oct = partial(int, base=8)
_as_hex = partial(int, base=16)


def _dedent_fence_line(line, base):
    """
    Strip up to ``base`` leading whitespace characters from a fence body line

    Lines indented less than ``base`` lose only the whitespace they have,
    so under-indented content is preserved rather than truncated.

    :param line: one raw fence body line (with its newline)
    :param base: the closing fence line's indentation width
    :return: the dedented line
    """

    i = 0
    limit = min(base, len(line))
    while i < limit and line[i] in " \t":
        i += 1
    return line[i:]


def _as_ellipsis(_s):
    """
    Reader literal for the Python Ellipsis singleton
    """

    return ...


def _as_py_singleton(s):
    """
    Reader literals for Python None and bool singletons
    """

    if s == "None":
        return None
    if s == "True":
        return True
    return False


def _as_float(s):
    """
    Convert a string to a float, stripping any trailing 'f' suffix
    """

    if s[-1] == "f":
        s = s[:-1]
    return float(s)


def _as_fraction(s):
    """
    Convert a string to a fraction source form

    The fraction type cannot be stored in a const pool, so we convert
    it into a source form of (fraction NUMERATOR DENOMINATOR) instead.

    :param s: the fraction string (e.g., '3/4')
    :return: a cons list representing the fraction form
    """

    frac = fraction(s)
    return cons(_symbol_fraction, frac.numerator, frac.denominator, nil)


def _as_decimal(s):
    """
    Convert a string to a decimal source form

    The decimal type cannot be stored in a const pool, so we convert
    it into a source form of (decimal TUPLE) instead.

    :param s: the decimal string with 'd' suffix (e.g., '123.45d')
    :return: a cons list representing the decimal form
    """

    s = s[:-1]
    ct = decimal(s).as_tuple()
    return cons(_symbol_decimal, tuple(ct), nil)


def _attr_link(obj, name):
    """
    Build one ``(compile/attr OBJ NAME)`` link

    The shared rule behind both spellings of attribute access: the atom
    pattern below chains it over a dotted atom, and the reader's ``.``
    suffix macro applies it once per pass.

    :param obj: the value the attribute is read from
    :param name: the attribute name, as a string
    :return: the (compile/attr ...) form
    """

    return cons(_symbol_attr, obj, symbol(name), nil)


def _string_segment_parts(value):
    """
    The parts a value contributes to a joined string form

    A ``(compile/#str ...)`` form contributes its arguments, so chains stay
    flat and an f-string segment splices rather than nesting. Anything else
    contributes itself.

    :param value: a string literal, or a compile/#str form
    :return: a list of forms
    """

    if is_pair(value) and not is_nil(value):
        head, tail = value
        if head is _symbol__str:
            parts = []
            node = tail
            while is_pair(node) and not is_nil(node):
                item, node = node
                parts.append(item)
            return parts

    return [value]


def _as_dotted_attr_atom(s):
    """
    Convert dotted identifier atom (e.g. foo.bar) to (compile/attr obj name).
    Left-associative: foo.bar.baz becomes (compile/attr (compile/attr foo bar)
    baz).

    The matching pattern is wider than the well-formed shape, so a dot with
    no name after it (``foo.``, ``foo..bar``, ``foo.9x``) arrives here and
    is rejected by name. ``ValueError`` is what the read loop converts into
    a positioned reader error.

    :param s: the raw dotted atom text
    :return: the nested (compile/attr ...) form
    :raises ValueError: when any segment after a dot is not an identifier
    """

    parts = s.split(".")

    obj = symbol(parts[0])
    for name in parts[1:]:
        if not name:
            raise ValueError(f"attribute name missing in {s!r}")
        if not _attr_name_re(name):
            raise ValueError(f"invalid attribute name {name!r} in {s!r}")
        obj = _attr_link(obj, name)

    return obj


def _as_complex(s):
    """
    Convert a string to a complex number

    Supports both 'i' and 'j' suffixes for the imaginary part.

    :param s: the complex string (e.g., '1+2i' or '3+4j')
    :return: a complex number
    """

    if s[-1] == "i":
        return complex(s[:-1] + "j")
    else:
        return complex(s)


ESCAPE_SEQUENCE_RE = regex(r'''
(\\U........
| \\u....
| \\x..
| \\[0-7]{1,3}
| \\N\{[^}]+\}
| \\[\\'"abfnrtv]
)''', UNICODE | VERBOSE)

# Escape sequences are bounded in length; the longest is ``\N{NAME}`` whose
# Unicode names top out well under this window. Used to match a single escape
# from a peeked window in the format-string splitter.
_FSTRING_ESCAPE_PEEK = 128


def _decode_sub(match):
    """
    Decode a matched escape sequence

    :raises ValueError: naming the escape and the codec's reason when
        the characters after the escape prefix do not decode
    """

    text = match.group(0)
    try:
        return decode(text, 'unicode-escape')
    except UnicodeDecodeError as ude:
        raise ValueError(
            f"invalid escape sequence {text}: {ude.reason}") from None


def _as_unicode(s, pattern=ESCAPE_SEQUENCE_RE):
    """
    Process escape sequences in a string

    :param s: the string containing escape sequences
    :param pattern: the regex pattern for escape sequences
    :return: the string with escape sequences decoded
    """

    return pattern.sub(_decode_sub, s)


# ===========================================================================
# Atom helpers
# ===========================================================================


class Atom(str):
    """
    A raw atom string that has not yet been processed
    """

    pass


# ===========================================================================
# Reader
# ===========================================================================


class Reader:
    """
    S-expression reader with configurable reader macros and atom patterns

    The reader parses textual input into Koskari data structures. It
    supports reader macros for special character handling and atom
    patterns for converting atomic tokens into typed values.

    Each character maps to a newest-first stack of
    ``(macro_fn, terminating, predicate_fn)`` triplets (see
    :meth:`add_event_macro`); ``predicate_fn`` may be None. Optional predicates
    gate macro dispatch (early DEFER) and soft atom boundaries for
    terminating registrations. Event macros may return DEFER so the next
    handler in ``_event_macro_chain_for`` runs; the leading-character chain
    ends with ``_read_default``.

    Suffix macros are a second, parallel registry keyed the same way (see
    :meth:`add_suffix_macro`). Where an event macro dispatches on the
    character that *opens* a token, a suffix macro dispatches on the
    character following a completed value, and wraps that value. Each
    consumes exactly one suffix and the driver re-enters, so chains across
    differing suffix kinds compose without any one macro knowing about the
    others.
    """

    def __init__(self, nodefaults=False, bracket_pairs=True):
        """
        Initialize a new reader

        :param nodefaults: if True, do not add default macros and patterns
        :param bracket_pairs: if True (default), ``[ ]`` reads into distinct
            bracket-pair / bracket-nil values; if False, ``[ ]`` reads as plain
            pairs terminated by nil, identical to ``( )``
        """

        self.reader_macros = {}
        self.atom_patterns = []
        self.suffix_macros = {}
        self.terminating = set(_BASE_TERMINATORS)
        self._terms = "".join(sorted(self.terminating))
        self.bracket_pairs = bracket_pairs
        # raw reader defaults stay bare; the pragma is what turns switches
        # on, so a baseline-on setting still reads off here until a pragma
        # is applied. dot_attrs is the exception: the atom spelling
        # (foo.bar) predates the pragma and bare readers have always done
        # it, so it stays on to keep that contract. Retiring that exception
        # is deferred, not rejected -- see design/archive/suffix-macros.md.
        self.index_brackets = False
        self.dot_attrs = True
        self.string_segments = False
        self.unary = False
        self.fences_enabled = True

        if not nodefaults:
            self._add_default_macros()
            self._add_default_atoms()

        # suffix macros are registered for every reader (including bare
        # ones); each gates on its own feature switch via its predicate
        self.add_suffix_macro(
            "[", self._apply_index_brackets,
            predicate=self._index_brackets_enabled)
        self.add_suffix_macro(
            ".", self._apply_dot_attr,
            predicate=self._dot_attrs_enabled)
        self.add_suffix_macro(
            "-", self._apply_string_segment,
            predicate=self._string_segments_enabled)


    def read(self, reader_stream):
        """
        Read a single s-expression from the stream

        Returns a cons cell, symbol, or numeric value. Returns READ_EOF if
        no data remains in the stream.

        :param reader_stream: the source stream to read from
        :return: the parsed value, or READ_EOF at EOF
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream)

        if event is VALUE:
            return value
        elif event is EOF:
            return READ_EOF
        else:
            raise reader_stream.error("invalid syntax", pos)


    def read_atom(self, reader_stream):
        """
        Read a single s-expression, returning atoms as unprocessed strings

        Returns a cons cell, or an unprocessed atomic value

        :param reader_stream: the source stream to read from
        :return: the parsed value, or READ_EOF at EOF
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream, raw=True)

        if event is VALUE:
            return value
        elif event is ATOM:
            return Atom(value)
        elif event is EOF:
            return READ_EOF
        else:
            raise reader_stream.error("invalid syntax", pos)


    def read_and_position(self, reader_stream):
        """
        Read a single s-expression and return its position

        :param reader_stream: the source stream to read from
        :return: a tuple of (value, position) where position is (line, col);
            value is READ_EOF at EOF
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream)

        if event is VALUE:
            return value, pos
        elif event is EOF:
            return READ_EOF, pos
        else:
            raise reader_stream.error("invalid syntax", pos)


    def _sync_terms_from_macros(self):
        """
        Recompute ``terminating`` and ``_terms`` from stacked macros plus base
        whitespace terminators.
        """

        extra = set()
        for c, pairs in self.reader_macros.items():
            for _fn, term, _pred in pairs:
                if term:
                    extra.add(c)
        self.terminating = set(_BASE_TERMINATORS) | extra
        self._terms = "".join(sorted(self.terminating))


    def _atom_boundary_split(self, stream, c, atom_prefix):
        """
        Whether the atom ends before ``c`` (split) vs folding ``c`` into it.

        Newest-first scan of terminating registrations for ``c``.

        :param stream: the source stream
        :param c: the boundary character (not consumed)
        :param atom_prefix: atom text so far if we split before ``c``
        :return: True to split, False to fold ``c`` into the atom
        """

        pairs = self.reader_macros.get(c, ())
        for macro_fn, term, predicate_fn in pairs:
            if not term:
                continue
            if predicate_fn is None:
                return True
            if not predicate_fn(stream, c, atom_prefix):
                return False
        return True


    def _collect_atom_continuation(self, stream, prefix, terminators):
        """
        Extend atom text from ``prefix`` using ``terminators`` for
        ``read_until_chars``, applying predicated boundaries.

        :param stream: the source stream
        :param prefix: atom text with all characters consumed so far
        :param terminators: terminator string for ``read_until_chars``
        :return: completed atom string; boundary char left unconsumed on split
        """

        while True:
            suffix = stream.read_until_chars(terminators)
            atom = prefix + suffix
            peeked = stream.peek(1)
            if not peeked:
                return atom
            c = peeked[0]
            if c not in self.terminating:
                return atom
            if self._atom_boundary_split(stream, c, atom):
                return atom
            stream.read(1)
            prefix = atom + c


    def _event_macro_chain_for(self, c):
        """
        Handlers tried in order until one does not return DEFER.

        Yields all stacked macros for ``c`` (newest first), then the
        ``_read_default`` fallback that folds ``c`` into an atom.

        :param c: the single character that was just read from the stream
        :yields: ``(handler, predicate_fn)`` where handler is
            ``(stream, c) -> (event, value)`` and optional ``predicate_fn`` is
            ``(stream, c, atom_prefix) -> bool`` or None for fallbacks
        """

        for macro_fn, _term, predicate_fn in self.reader_macros.get(c, ()):
            yield macro_fn, predicate_fn
        yield self._read_default, None


    def _event_macro_predicate_allows(self, stream, c, predicate_fn):
        """
        Whether dispatch may run ``macro_fn`` for this stack entry.

        :param stream: the source stream
        :param c: the dispatch character (already consumed)
        :param predicate_fn: optional ``(stream, c, atom_prefix) -> bool``
        :return: False means skip this handler (same as DEFER)
        """

        if predicate_fn is None:
            return True
        return bool(predicate_fn(stream, c, None))


    def _dispatch_event_macros(self, stream, c):
        """
        Run :meth:`_event_macro_chain_for` until a non-DEFER result.

        If a stack entry's predicate returns false, that handler is skipped
        (same as ``(DEFER, None)`` without calling ``macro_fn``).

        If a handler returns (DEFER, repl) with repl not None, ``repl``
        replaces ``c`` for subsequent handlers in the chain.
        """

        for handler, predicate_fn in self._event_macro_chain_for(c):
            if not self._event_macro_predicate_allows(
                    stream, c, predicate_fn):
                continue
            event, value = handler(stream, c)
            if event is DEFER:
                if value is not None:
                    c = value
                continue
            return event, value

        raise RuntimeError('macro chain ended without a result')


    def _skip_horizontal_whitespace(self, stream):
        """
        Consume horizontal whitespace only (space and tab)

        :param stream: the source stream
        :return: the whitespace string consumed
        """

        consumed = []
        while True:
            c = stream.peek(1)
            if c not in (' ', '\t'):
                break
            consumed.append(stream.read(1))
        return ''.join(consumed)


    def _read(self, stream, raw=False, vertical_ws=True):
        """
        Internal read implementation

        :param stream: the source stream
        :param raw: if True, return atoms without processing
        :param vertical_ws: if True (default), skip all whitespace including
            newlines before each token; if False, skip only horizontal
            whitespace (layout header tails and logical-line reads)
        :return: tuple of (event, position, value)
        """

        while True:
            if vertical_ws:
                stream.skip_whitespace()
            else:
                self._skip_horizontal_whitespace(stream)

            position = stream.position()
            c = stream.read()
            if not c:
                return EOF, position, None

            # the macro about to run has consumed its trigger; this is
            # where the trigger stood, for it to report as the opener of
            # whatever it goes on to read. A macro that reads nested
            # forms must take it before the first nested read.
            stream.event_position = position

            try:
                event, value = self._dispatch_event_macros(stream, c)
                if not raw and event is ATOM:
                    event = VALUE
                    value = self.process_atom(value)

            except ValueError as ve:
                raise stream.error(ve.args[0], position) from None

            if is_pair(value):
                value.set_position(position)

            if event is SKIP:
                continue
            else:
                break

        if event is VALUE:
            value = self._dispatch_suffix_macros(stream, value, position)

        return event, position, value


    def _dispatch_suffix_macros(self, stream, value, position):
        """
        Apply suffix macros to a just-read value until none claim it

        Peeks the next character, dispatches to that character's macro
        stack (newest first, skipping entries whose predicate declines),
        and applies at most one macro per pass. A macro that returns a
        value re-enters this loop with it, so chains across differing
        suffix kinds -- ``foo[a].b[c]`` -- fall out of dispatch rather than
        out of any one macro. Dispatch stops at end of input, at an
        unregistered character, or when every entry for that character
        declines.

        :param stream: the source stream, positioned after ``value``
        :param value: the value just read
        :param position: the position of ``value``
        :return: the value, wrapped by however many suffix macros claimed it
        :raises ReaderSyntaxError: when a macro claims a suffix without
            consuming any input
        """

        while True:
            char = stream.peek(1)
            if not char:
                break

            entries = self.suffix_macros.get(char)
            if not entries:
                break

            for macro_fn, predicate_fn in entries:
                if predicate_fn is not None:
                    if not predicate_fn(stream, char, value):
                        continue

                before = stream.position()
                result = macro_fn(stream, value, position)
                if result is DEFER:
                    continue

                if stream.position() == before:
                    raise stream.error(
                        f"suffix macro for {char!r} consumed no input",
                        before)

                value = result
                break

            else:
                break

        return value


    def _index_brackets_enabled(self, stream, char, value):
        """
        Suffix macro predicate for :meth:`_apply_index_brackets`

        :param stream: the source stream
        :param char: the trigger character
        :param value: the value the bracket group would attach to
        :return: whether ``reader/index-brackets`` is enabled
        """

        return self.index_brackets


    def _apply_index_brackets(self, stream, value, position):
        """
        Suffix macro: postfix index-bracket reader macro

        When ``reader/index-brackets`` is enabled, an ``[ ... ]`` group
        attached directly to a just-read value (no intervening whitespace)
        desugars into ``(item VALUE KEY)``. Consumes exactly one group; the
        suffix driver re-enters for chains, so ``foo[a][b]`` reads as
        ``(item (item foo a) b)``. A space breaks the attachment:
        ``foo [a]`` reads as two forms.

        :param stream: the source stream, positioned at ``[``
        :param value: the value the bracket group attaches to
        :param position: the position of ``value``
        :return: the wrapped ``(item ...)`` form
        :raises ReaderSyntaxError: when the group carries no key
        """

        # the stream sits on the bracket; hand its position to the pair
        # reader as the opener, as _read does for every other trigger
        stream.event_position = stream.position()
        index = self.read_pair(stream)

        keys = []
        node = index
        while is_pair(node) and not is_nil(node):
            head, node = node
            keys.append(head)

        if not keys:
            raise stream.error("index brackets require a key", position)

        value = cons(symbol("item"), value, *keys, nil)
        value.set_position(position)

        return value


    def _dot_attrs_enabled(self, stream, char, value):
        """
        Suffix macro predicate for :meth:`_apply_dot_attr`

        :param stream: the source stream
        :param char: the trigger character
        :param value: the value the attribute would be read from
        :return: whether ``reader/dot-attrs`` is enabled
        """

        return self.dot_attrs


    def _apply_dot_attr(self, stream, value, position):
        """
        Suffix macro: postfix dotted attribute reader macro

        When ``reader/dot-attrs`` is enabled, a ``.`` attached directly to
        a just-read value (no intervening whitespace) reads an attribute
        name and wraps into ``(compile/attr VALUE NAME)``. Consumes exactly
        one attribute; the suffix driver re-enters for chains, so
        ``(f x).a.b`` is left-associative. A space breaks the attachment,
        leaving ``.name`` an ordinary symbol, so the leading-dot method
        spelling ``(.name obj)`` is untouched.

        :param stream: the source stream, positioned at ``.``
        :param value: the value the attribute is read from
        :param position: the position of ``value``
        :return: the wrapped (compile/attr ...) form
        :raises ReaderEOF: when input ends right after the dot, so the REPL
            treats the form as unfinished rather than wrong
        :raises ReaderSyntaxError: when what follows is not a name
        """

        stream.read(1)

        # not a ReaderEOF even at end of input: the name must be adjacent
        # to the dot, so no amount of further input can complete this, and
        # signalling "unfinished" would have the REPL prompt for a line it
        # is then obliged to reject
        dot_at = stream.position()
        char = stream.peek(1)
        if not char or char not in _ATTR_NAME_HEAD:
            raise stream.error("attribute name expected after '.'", dot_at)

        name = [stream.read(1)]
        while True:
            char = stream.peek(1)
            if not char or char not in _ATTR_NAME_REST:
                break
            name.append(stream.read(1))

        value = _attr_link(value, "".join(name))
        value.set_position(position)

        return value


    def _string_segments_enabled(self, stream, char, value):
        """
        Suffix macro predicate for :meth:`_apply_string_segment`

        Only a string literal, or a joined form already built from one, can
        continue. Deciding here rather than in the handler is what keeps
        ``(foo)-`` reading as a form followed by a minus.

        :param stream: the source stream
        :param char: the trigger character
        :param value: the value the segment would continue
        :return: whether a string segment may attach here
        """

        if not self.string_segments:
            return False

        # a str subclass (Atom) is not a string literal, so match exactly
        if type(value) is str or type(value) is bytes:
            return True

        if is_pair(value) and not is_nil(value):
            head, _tail = value
            return head is _symbol__str

        return False


    def _skip_segment_gap(self, stream):
        """
        Consume whitespace and comments between a segment marker and the
        next segment

        Comments are treated as absent from the stream entirely, and the
        gap crosses newlines -- a segment need not be on the following
        line, and need not be indented.

        :param stream: the source stream
        """

        while True:
            stream.skip_whitespace()
            if stream.peek(1) != ";":
                break
            stream.readline()


    def _peek_string_opener(self, stream):
        """
        Whether the stream is positioned at a string literal opener

        Accepts a bare quote and every flavor prefix the reader has. The
        macro decides that a literal follows, not which literals are
        sensible to join: a ``b`` segment joins like any other, and what
        ``compile/#str`` makes of it is the author's business.

        Running out of input is reported as ``EOF`` rather than as "not an
        opener", so the caller can distinguish an unfinished form from a
        wrong one. A flavor prefix with nothing after it is EOF too: at the
        end of ``"a"- f`` the next character is still coming.

        :param stream: the source stream
        :return: True at an opener, ``EOF`` when input ran out mid-check,
            False otherwise
        """

        char = stream.peek(1)
        if not char:
            return EOF
        elif char == '"':
            return True
        elif char in "frb":
            ahead = stream.peek(2)
            if len(ahead) < 2:
                return EOF
            return ahead[1:2] == '"'
        else:
            return False


    def _apply_string_segment(self, stream, value, position):
        """
        Suffix macro: string segment continuation

        When ``reader/string-segments`` is enabled, a ``-`` attached
        directly to a string literal's closing quote continues that literal
        into the next quoted chunk, wrapping into ``(compile/#str ...)``.
        Consumes exactly one segment; the driver re-enters, so a chain
        stays flat. Each segment carries its own flavor prefix, and an
        f-string segment splices its parts rather than nesting, matching
        what f-strings already read as. Mixing flavors is not the reader's
        business: a ``b`` segment joins like any other and answers to
        ``compile/#str``.

        The following segment is read through the character macros directly
        rather than as a general value, so its own suffixes do not bind
        before the join: in ``"a"- " b".join`` the ``.join`` attaches to the
        joined result.

        :param stream: the source stream, positioned at ``-``
        :param value: the string literal being continued
        :param position: the position of ``value``
        :return: the joined (compile/#str ...) form
        :raises ReaderEOF: when input ends before the segment arrives, so
            the REPL treats the form as unfinished rather than wrong
        :raises ReaderSyntaxError: when what follows is not a string literal
        """

        stream.read(1)
        self._skip_segment_gap(stream)

        segment_at = stream.position()

        # the gap skip stops at a dedent rather than crossing it, so a
        # continuation that falls out of its block never reaches its
        # segment. Report the indentation, which is the actual cause; the
        # generic message describes the symptom and sends people looking
        # at the marker instead.
        if stream.dedented:
            raise stream.error(
                "string segment must stay indented within its block",
                segment_at)

        opener = self._peek_string_opener(stream)
        if opener is EOF:
            raise stream.reader_eof(opened_at=segment_at,
                                    context="string segment")
        elif not opener:
            raise stream.error(
                "string segment expected after '-'", segment_at)

        event, segment = self._dispatch_event_macros(stream, stream.read(1))
        if event is not VALUE:
            raise stream.error(
                "string segment expected after '-'", segment_at)

        parts = _string_segment_parts(value) + _string_segment_parts(segment)

        value = cons(_symbol__str, *parts, nil)
        value.set_position(position)

        return value


    def add_event_macro(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Add a character event macro to the reader

        Each call **prepends** ``(macro_fn, terminating, predicate)`` to that
        character's macro stack (newest first).

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, char) -> (event, value). The macro
            may return (DEFER, None) or (DEFER, repl) to decline handling and
            try the next handler from :meth:`_event_macro_chain_for`; the
            chain always ends with ``_read_default``. The opening character
            was already consumed; pass ``c`` through to ``_read_default``. On
            DEFER, do not advance the stream beyond peek-only inspection.
        :param terminating: if True, this character terminates atoms
        :param predicate: optional ``(stream, char, atom_prefix) -> bool``.
            If false before ``macro_fn`` runs at read dispatch, that entry is
            skipped (early DEFER). For terminating registrations, also
            participates in atom boundary decisions (see
            :meth:`_atom_boundary_split`).
        """

        triplet = (macro_fn, terminating, predicate)
        for c in char:
            self.reader_macros.setdefault(c, []).insert(0, triplet)
        self._sync_terms_from_macros()


    def get_event_macros(self, char):
        """
        Return stacked event macros for one character

        :param char: single character to look up
        :return: list of ``(macro_fn, terminating, predicate_fn)``, newest
            first; empty if none
        """

        return list(self.reader_macros.get(char, ()))


    def get_event_macro(self, char):
        """
        Get the newest character event macro from the reader

        :param char: the character to look up
        :return: tuple of (macro_fn, terminating, predicate_fn) or None
        """

        lst = self.reader_macros.get(char)
        if not lst:
            return None
        return lst[0]


    def clear_event_macro(self, char):
        """
        Remove all stacked event macros for each character in ``char``

        :param char: the character(s) to clear
        """

        for c in char:
            self.reader_macros.pop(c, None)
        self._sync_terms_from_macros()


    @contextmanager
    def temporary_event_macro(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Context manager to temporarily apply a character event macro

        Prepends one ``(macro_fn, terminating, predicate)`` triplet shared
        across ``char``. On exit, removes **only** that triplet from the head
        of each affected list when it still matches by identity.

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, char) -> (event, value); may return
            (DEFER, None) like other event macros (see add_event_macro).
        :param terminating: if True, this character terminates atoms
        :param predicate: optional predicate (see :meth:`add_event_macro`)
        :yields: self
        """

        triplet = (macro_fn, terminating, predicate)
        for c in char:
            self.reader_macros.setdefault(c, []).insert(0, triplet)
        self._sync_terms_from_macros()

        # the removal must run however the body ends: an incomplete form
        # raising ReaderEOF out of the body would otherwise leave the
        # macro installed on a long-lived reader, the REPL's for one
        try:
            yield self
        finally:
            for c in char:
                lst = self.reader_macros.get(c)
                if lst and lst[0] is triplet:
                    lst.pop(0)
                    if not lst:
                        del self.reader_macros[c]
            self._sync_terms_from_macros()


    def add_suffix_macro(self, char, macro_fn, predicate=None):
        """
        Add a suffix macro to the reader

        Suffix macros are the reader's post macros: after any token is read
        as a ``VALUE``, the driver peeks the next character and dispatches
        to that character's macro stack. This is how attached-bracket
        indexing turns ``foo[x]`` into ``(item foo x)``.

        Each call **prepends** ``(macro_fn, predicate)`` to that character's
        macro stack (newest first), mirroring :meth:`add_event_macro`.

        A macro consumes **exactly one** suffix and returns the value to
        carry forward; the driver re-enters with that value, so chains such
        as ``foo[a][b]`` fall out of dispatch rather than out of any one
        macro.

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, value, position) -> value. The
            trigger character has **not** been consumed. May return
            ``DEFER`` to decline, leaving the stream untouched beyond
            peek-only inspection, in which case the next handler from that
            character's stack runs. A macro that has consumed input owes a
            reader error rather than a late ``DEFER``, and one that claims
            a suffix without consuming anything is an error.
        :param predicate: optional ``(stream, char, value) -> bool``. If
            false, that entry is skipped without calling ``macro_fn`` (early
            DEFER). Feature gating belongs here, where the value the suffix
            would attach to is available.
        """

        entry = (macro_fn, predicate)
        for c in char:
            self.suffix_macros.setdefault(c, []).insert(0, entry)


    def get_suffix_macros(self, char):
        """
        Return stacked suffix macros for one character

        :param char: single character to look up
        :return: list of ``(macro_fn, predicate)``, newest first; empty if
            none
        """

        return list(self.suffix_macros.get(char, ()))


    def get_suffix_macro(self, char):
        """
        Get the newest suffix macro from the reader

        :param char: the character to look up
        :return: tuple of (macro_fn, predicate) or None
        """

        lst = self.suffix_macros.get(char)
        if not lst:
            return None
        return lst[0]


    def clear_suffix_macro(self, char):
        """
        Remove all stacked suffix macros for each character in ``char``

        :param char: the character(s) to clear
        """

        for c in char:
            self.suffix_macros.pop(c, None)


    @contextmanager
    def temporary_suffix_macro(self, char, macro_fn, predicate=None):
        """
        Context manager to temporarily apply a suffix macro

        Prepends one ``(macro_fn, predicate)`` entry shared across ``char``.
        On exit, removes **only** that entry from the head of each affected
        list when it still matches by identity.

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, value, position) -> value; may
            return ``DEFER`` like other suffix macros (see
            :meth:`add_suffix_macro`).
        :param predicate: optional predicate (see :meth:`add_suffix_macro`)
        :yields: self
        """

        entry = (macro_fn, predicate)
        for c in char:
            self.suffix_macros.setdefault(c, []).insert(0, entry)

        try:
            yield self
        finally:
            for c in char:
                lst = self.suffix_macros.get(c)
                if lst and lst[0] is entry:
                    lst.pop(0)
                    if not lst:
                        del self.suffix_macros[c]


    def add_macro_character(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Add a reader macro to the parser

        Unlike add_event_macro, this wraps the macro function to always
        return a VALUE event; wrapped macros cannot DEFER.

        :param char: the character to trigger on
        :param macro_fn: function(stream, char) -> value
        :param terminating: if True, this character terminates atoms
        :param predicate: optional predicate (see :meth:`add_event_macro`)
        """

        def macro_adapter(stream, char):
            return VALUE, macro_fn(stream, char)

        self.add_event_macro(char, macro_adapter, terminating,
                             predicate=predicate)


    def temporary_macro_character(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Context manager to temporarily apply a reader macro

        :param char: the character to trigger on
        :param macro_fn: function(stream, char) -> value
        :param terminating: if True, this character terminates atoms
        :param predicate: optional predicate (see :meth:`add_event_macro`)
        :return: context manager
        """

        def macro_adapter(stream, char):
            return VALUE, macro_fn(stream, char)

        return self.temporary_event_macro(char, macro_adapter, terminating,
                                          predicate=predicate)


    def _add_default_macros(self):
        """
        Add the default reader macros for standard syntax
        """

        sm = self.add_event_macro

        sm('(', partial(self._read_pair, ')'), True)
        sm(')', self._close_pair, True)

        sm('[', partial(self._read_pair, ']'), True)
        sm(']', self._close_pair, True)

        sm('{', partial(self._read_thunk, '}'), True)
        sm('}', self._close_pair, True)

        sm('"', self._read_string, True)
        sm(';', self._read_comment, True)

        sm('f', self._read_fstring, False)
        sm('r', self._read_rstring, False)
        sm('b', self._read_bstring, False)

        sm('#', self._read_collection, False)
        sm('&', self._read_ref_call, False)
        for c in "-+~":
            sm(c, self._read_unary, False, predicate=self._unary_enabled)

        # -'FORM is (datumq FORM); registered after the unary prefixes so
        # it is tried first, and admitted only before a quote. A trigger
        # of two characters, pending multi-character registration
        sm('-', self._read_datumq, False, predicate=self._datumq_follows)

        sm("'", self._read_quote, True)
        sm("`", self._read_quasiquote, True)

        # registered after quasiquote so the triple-backtick check wins
        sm("`", self._read_fence, True, predicate=self._fence_predicate)


    def set_atom_pattern(self, namesym, match_fn, conversion_fn):
        """
        Add or update an atom pattern

        :param namesym: symbol name for this pattern
        :param match_fn: function(string) -> bool
        :param conversion_fn: function(string) -> value
        """

        for patt in self.atom_patterns:
            if patt[0] is namesym:
                patt[1] = match_fn
                patt[2] = conversion_fn
                break
        else:
            self.atom_patterns.insert(0, [namesym, match_fn, conversion_fn])


    def get_atom_pattern(self, namesym):
        """
        Get an atom pattern by name

        :param namesym: symbol name to look up
        :return: the pattern list [name, match, conv] or None
        """

        for patt in self.atom_patterns:
            if patt[0] is namesym:
                return patt
        else:
            return None


    def clear_atom_pattern(self, namesym):
        """
        Remove an atom pattern by name

        :param namesym: symbol name to remove
        """

        for index, patt in enumerate(self.atom_patterns):
            if patt[0] is namesym:
                del self.atom_patterns[index]
                break


    def reload_pragma(self, pragma):
        """
        Apply reader settings from a module pragma to this reader

        Covers every setting a plain reader carries: reader/brackets,
        reader/index-brackets, reader/dot-attrs, reader/string-segments,
        reader/unary, and reader/fences.

        Raw reader defaults are deliberately bare, so this is how a
        directly-constructed reader is brought up to the pragma's baseline.
        Tooling that builds a reader and skips this step sees none of the
        pragma-gated syntax, which is rarely what it wants.

        :param pragma: a module pragma, or None for a no-op
        """

        if pragma is None:
            return

        brackets = pragma.get("reader/brackets")
        if brackets is not None:
            self.bracket_pairs = bool(brackets)

        index_brackets = pragma.get("reader/index-brackets")
        if index_brackets is not None:
            self.index_brackets = bool(index_brackets)

        dot_attrs = pragma.get("reader/dot-attrs")
        if dot_attrs is not None:
            self.dot_attrs = bool(dot_attrs)

        string_segments = pragma.get("reader/string-segments")
        if string_segments is not None:
            self.string_segments = bool(string_segments)

        unary = pragma.get("reader/unary")
        if unary is not None:
            self.unary = bool(unary)

        fences = pragma.get("reader/fences")
        if fences is not None:
            self.fences_enabled = bool(fences)


    def _dotted_attr_match(self, atom):
        """
        Atom pattern matcher for dotted attribute access

        Gated on ``reader/dot-attrs`` so one setting governs both spellings.
        Gating here rather than by installing and removing the pattern keeps
        its position in the ordered pattern list fixed, which matters because
        that order decides which pattern claims an ambiguous atom.

        :param atom: the raw atom text
        :return: a match when dotted attributes are enabled, else None
        """

        return self.dot_attrs and _dotted_attr_atom_re(atom)


    def set_atom_regex(self, namesym, regexstr, conversion_fn):
        """
        Add an atom pattern using a regex string

        :param namesym: symbol name for this pattern
        :param regexstr: regex pattern string
        :param conversion_fn: function(string) -> value
        """

        match = partial(regex(regexstr).match)
        self.set_atom_pattern(namesym, match, conversion_fn)


    def _add_default_atoms(self):
        """
        Add the default atom patterns for standard literals
        """

        ap = self.set_atom_pattern

        ap(symbol("attr"), self._dotted_attr_match,
           _as_dotted_attr_atom)
        ap(symbol("keyword"), _keyword_re, keyword)
        ap(symbol("ellipsis"), _ellipsis_atom_re, _as_ellipsis)
        ap(symbol("py_singleton"), _py_singleton_atom_re, _as_py_singleton)
        ap(symbol("int"), _integer_re, _as_integer)
        ap(symbol("float"), _float_re, _as_float)
        ap(symbol("hex"), _hex_re, _as_hex)
        ap(symbol("oct"), _oct_re, _as_oct)
        ap(symbol("binary"), _bin_re, _as_bin)
        ap(symbol("complex"), _complex_re, _as_complex)
        ap(symbol("fraction"), _fraction_re, _as_fraction)
        ap(symbol("decimal"), _decimal_re, _as_decimal)


    def _collect_atom_after_first_char(self, stream, first_c):
        """
        Accumulate atom text after the first character has been consumed.

        Terminator characters stop accumulation without consuming the
        terminator; the next read dispatches it as a leading character so
        macros such as quasiquote ``,`` remain VALUE-producing reads.

        Soft boundaries use :meth:`_atom_boundary_split` for terminating
        registrations that supply a predicate.

        :param stream: the source stream
        :param first_c: first character of the atom (already read)
        :return: completed atom string
        """

        return self._collect_atom_continuation(stream, first_c, self._terms)


    def _read_default(self, stream, c):
        """
        Default character macro handler for unrecognized characters

        Reads characters until a terminating character is found.

        :param stream: the source stream
        :param c: the initial character
        :return: tuple of (event, value)
        """

        atom = self._collect_atom_after_first_char(stream, c)

        if atom == ".":
            return DOT, None

        return ATOM, atom


    def process_atom(self, atom):
        """
        Process a raw atom string into a typed value

        Tries each atom pattern in order. If none match, returns a symbol.

        :param atom: the raw atom string
        :return: the processed value
        """

        for name, match, conv in self.atom_patterns:
            if match(atom):
                return conv(atom)
        else:
            return symbol(atom)


    def read_pair(self, stream, openchar=None, closechar=None):
        """
        Read a pair (list) from the stream

        :param stream: the source stream
        :param openchar: the opening character (read if None)
        :param closechar: the closing character (inferred if None)
        :return: the parsed pair
        """

        if openchar is None:
            openchar = stream.read(1)

        if closechar is None:
            if openchar in "{[(":
                i = "{[(".index(openchar)
                closechar = "}])"[i]

        _event, result = self._read_pair(closechar, stream, openchar)
        return result


    def _read_pair(self, closer, stream, char):
        """
        Internal pair reading implementation

        ``[`` builds bracket-pair cells terminated by bracket-nil; ``(``
        (and ``{``) build plain pairs terminated by nil. The two flavors
        are interchangeable to the compiler; the distinction is the
        recorded delimiter style.

        :param closer: the expected closing character
        :param stream: the source stream
        :param char: the opening character
        :return: tuple of (event, value)
        """

        opened_at = stream.event_position

        if char == "[" and self.bracket_pairs:
            empty = bracket_nil
            make_cell = bracket_pair
        else:
            empty = nil
            make_cell = pair

        result = empty
        work = result

        with stream.suspend_indent():
            while True:
                event, position, value = self._read(stream)

                if event is CLOSE_PAIR:
                    break

                elif event is DOT:
                    if result is empty:
                        raise stream.error("invalid dotted list", position)

                    dot_position = position

                    event, position, value = self._read(stream)
                    if event is not VALUE:
                        raise stream.error("invalid list syntax", position)
                    else:
                        setcdr(work, value)

                    event, position, value = self._read(stream)
                    if event is not CLOSE_PAIR:
                        raise stream.error("invalid use of dot in list",
                                           dot_position)
                    else:
                        break

                elif event is EOF:
                    raise stream.reader_eof(opened_at=opened_at,
                                            context=f"{char!r} form")

                elif result is empty:
                    result = make_cell(value, empty)
                    result.set_position(position)
                    work = result

                else:
                    new_work = make_cell(value, empty)
                    new_work.set_position(position)
                    setcdr(work, new_work)
                    work = new_work

        if closer and value != closer:
            raise stream.error("mismatched open and close characters",
                               position)

        return VALUE, result


    def _read_thunk(self, closer, stream, char):
        """
        Read a thunk block as (compile/gensym-thunk . BODY)

        :param closer: the expected closing character
        :param stream: the source stream
        :param char: the opening character
        :return: tuple of (event, value)
        """

        event, result = self._read_pair(closer, stream, char)

        if event is VALUE:
            new_result = cons(_symbol_gensym_thunk, result)
            new_result.set_position(result.get_position())
            result = new_result

        return event, result


    def _close_pair(self, stream, char):
        """
        Handle a closing parenthesis/bracket/brace

        :param stream: the source stream
        :param char: the closing character
        :return: tuple of (CLOSE_PAIR, char)
        """

        return CLOSE_PAIR, char


    def _read_collection(self, stream, fchar):
        """
        Read a collection literal (#(...), #[...], #{...}), or the
        quote-syntax and quasiquote-syntax prefixes (#'form, #`form)

        :param stream: the source stream
        :param fchar: the '#' character
        :return: tuple of (event, value)
        """

        peek = stream.peek(1)
        if peek == "'":
            stream.read(1)
            return self._read_quote(stream, fchar + peek, _symbol_quote_syntax)
        if peek == "`":
            stream.read(1)
            return self._read_quasiquote(
                stream, fchar + peek, _symbol_quasiquote_syntax)
        if not peek or peek not in "({[":
            return DEFER, None

        peek = stream.read(1)
        i = "({[".index(peek)

        closer = ")}]"[i]
        name = ("#tuple", "#dict", "#list")[i]

        pos = stream.position()

        with self.temporary_event_macro(closer, self._close_pair, True):
            event, result = self._read_pair(closer, stream, peek)

        if event is VALUE:
            if name == "#dict":
                result = self._split_dict_keywords(result)
            new_result = cons(symbol(name), result)
            new_result.set_position(pos)
            result = new_result

        return event, result


    def _split_dict_keywords(self, body):
        """
        Lower each key/value entry in a ``#dict`` spine into a
        ``(#tuple KEY VALUE)`` invocation form, so the spine is an
        ordinary argument list for the ``#dict`` call.

        A ``foo:`` keyword key becomes the atom ``foo`` (re-read through
        the normal atom classifier, so ``5:`` yields the integer ``5``);
        an explicit ``KEY : VALUE`` triple keeps its key expression as
        written. Both shapes emit one ``(#tuple KEY VALUE)`` entry. The
        ``**`` splat keyword and any keyword appearing in value position
        pass through untouched. A spine the pair matcher cannot parse is
        returned unchanged, so the ``#dict`` call reports the malformed
        form on its own terms.

        :param body: the read dict spine (a proper list)
        :return: the rewritten spine
        """

        matched = _DICT_SPLIT_MATCHER.match(body)
        if not matched.matched:
            return body

        out = []
        for frame in matched.captures.get("pairs", ()):
            if "splat" in frame:
                out.append(_keyword_starstar)
                out.append(frame["splat"].datum)
                continue

            if "kw" in frame:
                keycap = frame["kw"]
                key = self.process_atom(str(keycap.datum))
            else:
                keycap = frame["key"]
                key = keycap.datum

            entry = cons(_symbol_tuple, key, frame["val"].datum, nil)
            if keycap.position:
                entry.set_position(keycap.position)
            out.append(entry)

        return cons(*out, nil) if out else nil


    def _read_operand(self, stream, char, context):
        """
        Read the form a prefix reader macro applies to

        The macro character has been consumed. End of input is a reader
        EOF opened at the macro, and anything that is not a value is an
        invalid use of the macro

        :param stream: the source stream
        :param char: the macro character, for the message
        :param context: what the EOF was opened for
        :return: (position, form)
        """

        opened_at = stream.event_position
        event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(opened_at=opened_at, context=context)
        if event is not VALUE:
            raise stream.error(f"invalid use of {char}", pos)

        return pos, child


    def _read_ref_call(self, stream, fchar):
        """
        Read a by-ref expression as ``(compile/ref FORM)``

        ``&`` is a prefix operator: when it is directly attached to a
        following form -- the next character is non-whitespace and does not
        begin a comment or close a delimiter -- that whole form is read and
        wrapped in ``compile/ref``. So ``&foo``, ``&foo.bar``, and
        ``&(item foo 12)`` all wrap their operand, and because the operand
        is read as a full form any postfix syntax binds tighter than the
        prefix: ``&foo["a"]`` reads as ``(compile/ref (item foo "a"))``.

        When ``&`` stands alone -- followed by whitespace, a comment, a
        closing delimiter, or end of input -- it defers so it reads as the
        bare ``&`` symbol (e.g. the operator in ``(& 8 1)``).

        :param stream: the source stream
        :param fchar: the '&' character
        :return: tuple of (event, value)
        """

        peek = stream.peek(1)
        if not peek or peek.isspace() or peek == ";" or peek in ")]}":
            return DEFER, None

        _pos, child = self._read_operand(stream, fchar, "ref call")

        if is_keyword(child):
            return VALUE, keyword("&" + str(child))

        return VALUE, cons(_symbol_ref, child, nil)


    def _unary_enabled(self, stream, char, value):
        """
        Predicate for the unary prefix macros

        :return: whether ``reader/unary`` is enabled
        """

        return self.unary


    def _read_unary(self, stream, fchar):
        """
        Read a unary operator prefix as ``(OP FORM)``

        ``-``, ``+``, and ``~`` are prefix operators when directly attached
        to a name, a digit, or an opening delimiter: that whole form is
        read and wrapped in a call of the operator, so ``-x`` is ``(- x)``,
        ``+foo.bar`` is ``(+ (attr foo bar))``, and ``~(f y)`` is
        ``(~ (f y))``. The operand is read as a full form, so postfix
        syntax binds tighter than the prefix and ``-foo["a"]`` reads as
        ``(- (item foo "a"))``. The one-argument builtins call the
        Python unary methods, so ``+x`` reaches ``__pos__`` and ``~x``
        reaches ``__invert__``.

        A ``-`` before a digit, or a ``.`` and a digit, defers so the
        number atom keeps the sign as part of the literal; ``+1`` and
        ``~1`` have no literal spelling and read as calls. Every operator
        defers standing alone before whitespace, a comment, a closing
        delimiter, or end of input, reading as its bare symbol, and
        before any other character the ordinary atom scan runs, so
        ``->``, ``-=``, ``--x``, and ``+=`` stay symbols.

        :param stream: the source stream
        :param fchar: the operator character
        :return: tuple of (event, value)
        """

        peek = stream.peek(2)
        if not peek:
            return DEFER, None

        nxt = peek[0]
        digit = nxt.isdigit() or (nxt == '.' and peek[1:2].isdigit())
        if digit:
            if fchar == '-':
                return DEFER, None
        elif not (nxt.isalpha() or nxt == '_' or nxt in '([{'):
            return DEFER, None

        _pos, child = self._read_operand(stream, fchar, f"unary {fchar}")

        return VALUE, cons(_unary_symbols[fchar], child, nil)


    def _read_fstring(self, stream, fchar):
        """
        Read a format string (f"...")

        :param stream: the source stream
        :param fchar: the 'f' character
        :return: tuple of (event, value)
        """

        if stream.peek(1) != '"':
            return DEFER, None

        char = stream.read(1)
        pos = stream.position()
        lin, col = pos

        # the first quote is consumed; two more make the opener triple
        if stream.peek(2) == char * 2:
            col += 2

        # Read the body raw (escapes undecoded) so the field/literal split
        # sees source characters: an escape that decodes to a brace must not
        # act as a field delimiter, and embedded expression positions must
        # track the source rather than the post-escape text. Escapes are
        # decoded per literal chunk and per field interior in the splitter.
        _evnt, data = self._read_string(stream, char, with_escapes=False)

        # string text, not a script: a body starting with #! is literal
        substream = source_str(data, stream.filename, auto_skip_exec=False,
                               line_no=lin, col_no=col)

        fsr = FormatStringReader(self)
        result = fsr.split(substream)
        result.append(nil)

        return VALUE, cons(_symbol__str, *result)


    def _read_rstring(self, stream, char):
        """
        Read a raw string (r"...")

        :param stream: the source stream
        :param char: the 'r' character
        :return: tuple of (event, value)
        """

        if stream.peek(1) != '"':
            return DEFER, None
        else:
            return self._read_string(stream, stream.read(1), False)


    def _read_bstring(self, stream, char):
        """
        Read a binary string (b"...")

        :param stream: the source stream
        :param char: the 'b' character
        :return: tuple of (event, value)
        """

        if stream.peek(1) != '"':
            return DEFER, None

        pos = stream.position()
        event, raw = self._read_string(stream, stream.read(1), False)

        # as in Python, the literal text of a bytes string is ASCII; a
        # byte above 127 is written as an escape
        if not raw.isascii():
            raise stream.error(
                "bytes literal may only contain ASCII characters;"
                " write other bytes as escapes", pos)

        # escapes decode to code points, and a byte is a code point below
        # 256, so latin-1 maps every escaped byte back one to one; an
        # escape naming a code point past that has no byte value
        val = _as_unicode(raw)
        try:
            return event, val.encode('latin-1')
        except UnicodeEncodeError:
            raise stream.error(
                "bytes literal escape names a value above 255", pos) from None


    def _read_string(self, stream, char, with_escapes=True):
        """
        Read a string literal

        :param stream: the source stream
        :param char: the quote character
        :param with_escapes: if True, process escape sequences
        :return: tuple of (VALUE, string)
        """

        # the event that led here was the opening quote, or the prefix
        # letter before it; either way the literal opened there
        opened_at = stream.event_position

        if stream.peek(2) == (char * 2):
            stream.read(2)
            return self._read_3string(stream, char, with_escapes)

        result = []

        is_escp = "\\".__eq__
        is_char = char.__eq__

        sr = partial(stream.read, 1)
        for C in iter(sr, ''):
            if is_char(C):
                break
            result.append(C)
            if is_escp(C):
                result.append(sr())
        else:
            raise stream.reader_eof(opened_at=opened_at, context="string")

        res = "".join(result)
        if with_escapes:
            res = _as_unicode(res)

        return VALUE, res


    def _read_3string(self, stream, char, with_escapes=True):
        """
        Read a triple-quoted string

        :param stream: the source stream
        :param char: the quote character
        :param with_escapes: if True, process escape sequences
        :return: tuple of (VALUE, string)
        """

        seen = 0
        esc = False

        def seen_3(c):
            nonlocal seen
            nonlocal esc
            if esc:
                esc = False
                seen = 0
            elif c == "\\":
                esc = True
            elif c == char:
                seen += 1
            else:
                seen = 0
            return (seen == 3)

        value = stream.read_until(seen_3)
        if seen == 3:
            value = value[:-2]
            stream.read(1)

        else:
            raise stream.reader_eof(opened_at=stream.event_position,
                                    context="triple-quoted string")

        if with_escapes:
            value = _as_unicode(value)

        return VALUE, value


    def _datumq_follows(self, stream, char, value):
        """
        Predicate for the -' prefix

        :return: whether a quote follows the ``-``
        """

        return stream.peek(1) == "'"


    def _read_datumq(self, stream, char):
        """
        Read a raw form (-'expr) as ``(datumq expr)``

        :param stream: the source stream
        :param char: the ``-`` character
        :return: tuple of (VALUE, (datumq expr))
        """

        stream.read(1)
        return self._read_quote(stream, "-'", head=_symbol_datumq)


    def _read_quote(self, stream, char, head=_symbol_quote):
        """
        Read a quoted expression ('expr)

        :param stream: the source stream
        :param char: the quote character
        :param head: the symbol heading the read form, ``quote`` or
            ``quote-syntax``
        :return: tuple of (VALUE, (quote expr))
        """

        pos, child = self._read_operand(stream, char, "quote")

        # the operand's cell carries its position, as a list's cells do
        tail = cons(child, nil)
        tail.set_position(pos)
        return VALUE, cons(head, tail)


    def _read_quasiquote(self, stream, char, head=_symbol_quasiquote):
        """
        Read a quasiquoted expression (`expr)

        :param stream: the source stream
        :param char: the backquote character
        :param head: the symbol heading the read form, ``quasiquote`` or
            ``quasiquote-syntax``
        :return: tuple of (VALUE, (quasiquote expr))
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            _pos, child = self._read_operand(stream, char, "quasiquote")

        return VALUE, cons(head, child, nil)


    def _fence_predicate(self, stream, c, atom_prefix):
        """
        Allow the fence macro only for a triple-backtick opener

        At atom boundaries this returns True so backtick keeps its
        terminating behavior; the lookahead check applies at dispatch
        only, deferring single backticks to the quasiquote macros. The
        :attr:`fences_enabled` switch (the reader/fences pragma) defers
        all dispatch to the quasiquote macros when off.

        :param stream: source stream positioned after the first backtick
        :param c: the backtick character
        :param atom_prefix: atom boundary context; None at dispatch
        """

        if atom_prefix is not None:
            return True
        if not self.fences_enabled:
            return False
        return stream.peek(2) == "``"


    def _read_fence(self, stream, char):
        """
        Read a triple-backtick fence into a compile/apply-fence form

        The opener line names the fence format immediately after the
        backticks (whitespace optional), optionally followed by
        keyword/value option pairs. The body is captured as raw text
        until a line containing only three backticks closes the fence;
        body lines are dedented by the closing line's indentation.
        Lowers to::

            (compile/apply-fence NAME RAW-SRC META)

        where META is a keyword plist carrying ``filename:``, ``line:``
        and ``col:`` (the payload origin and dedent base), and
        ``options:`` (the opener-line pairs).

        :param stream: the source stream, positioned after the first
            backtick
        :param char: the backtick character
        :return: tuple of (VALUE, (compile/apply-fence NAME RAW META))
        """

        lin, col = stream.position()
        fence_pos = (lin, col - 1)

        stream.read(2)
        self._skip_horizontal_whitespace(stream)

        name = stream.read_until_chars(" \t\r\n")
        if not name:
            raise stream.error("fence requires a format name", fence_pos)

        options = self._read_fence_options(stream)

        c = stream.read(1)
        if not c:
            raise stream.reader_eof(opened_at=fence_pos, context="fence")
        if c == "\r" and stream.peek(1) == "\n":
            stream.read(1)

        payload_line = stream.position()[0]

        raw_lines = []
        while True:
            if not stream.peek(1):
                raise stream.reader_eof(opened_at=fence_pos, context="fence")
            line = stream.peek_line()
            if line.strip(" \t") == "```":
                base = len(line) - len(line.lstrip(" \t"))
                stream.read_until_chars("\r\n")
                break
            raw_lines.append(stream.readline())

        raw = "".join(_dedent_fence_line(ln, base) for ln in raw_lines)

        opts = nil
        for key, value in reversed(options):
            opts = cons(key, value, opts)

        meta_items = []
        if stream.filename:
            meta_items.extend((_keyword_fence_filename, stream.filename))
        meta_items.extend((_keyword_fence_line, payload_line))
        meta_items.extend((_keyword_fence_col, base))
        meta_items.extend((_keyword_fence_options, opts))
        meta = cons(*meta_items, nil)

        form = cons(_symbol_apply_fence, symbol(name), raw, meta, nil)
        fill_position(form, fence_pos)
        return VALUE, form


    def _read_fence_options(self, stream):
        """
        Read keyword/value option pairs from a fence opener line

        Stops before the line terminator, which is left unconsumed. Each
        option is a keyword atom followed by a value form on the same
        physical line.

        :param stream: the source stream, positioned after the fence name
        :return: list of (keyword, value) pairs
        """

        options = []
        while True:
            self._skip_horizontal_whitespace(stream)
            nxt = stream.peek(1)
            if not nxt or nxt in "\r\n":
                return options
            if nxt == ";":
                stream.read_until_chars("\r\n")
                return options

            event, pos, key = self._read(stream, vertical_ws=False)
            if event is not VALUE or not is_keyword(key):
                raise stream.error(
                    "fence option must be a keyword: value pair", pos)

            self._skip_horizontal_whitespace(stream)
            nxt = stream.peek(1)
            if not nxt or nxt in "\r\n;":
                raise stream.error(
                    f"fence option {key} requires a value", pos)

            event, vpos, value = self._read(stream, vertical_ws=False)
            if event is not VALUE:
                raise stream.error(
                    f"fence option {key} requires a value", vpos)
            if stream.position()[0] != pos[0]:
                raise stream.error(
                    "fence option value must not span lines", vpos)

            options.append((key, value))


    def _read_unquote(self, stream, char):
        """
        Read an unquoted expression (,expr)

        :param stream: the source stream
        :param char: the comma character
        :return: tuple of (VALUE, (unquote expr) or (unquote-splicing expr))
        """

        with self.temporary_event_macro("@", self._read_splice, True):
            _pos, child = self._read_operand(stream, char, "unquote")

        # is_pair(nil) is True (nil subclasses pair), so guard the empties
        # before indexing — `,()` / `,[]` are unquotes of the empty list.
        if is_pair(child) and not is_nil(child) and child[0] is _symbol_splice:
            value = cons(_symbol_unquote_splicing, child[1])
        else:
            value = cons(_symbol_unquote, child, nil)

        return VALUE, value


    def _read_splice(self, stream, char):
        """
        Read a splice expression (@expr inside unquote)

        :param stream: the source stream
        :param char: the '@' character
        :return: tuple of (VALUE, (splice expr))
        """

        _pos, child = self._read_operand(stream, char, "unquote-splicing")

        return VALUE, cons(_symbol_splice, child, nil)


    def _read_comment(self, stream, char):
        """
        Read and skip a comment

        :param stream: the source stream
        :param char: the semicolon character
        :return: tuple of (SKIP, comment-text)
        """

        return SKIP, stream.readline()


# ===========================================================================
# FormatStringReader
# ===========================================================================


class FormatStringReader(object):
    """
    Reader for parsing the interior of format strings
    """

    def __init__(self, normal_reader=None):
        """
        Initialize a format string reader

        :param normal_reader: the reader to use for embedded expressions
        """

        if normal_reader is None:
            self.normal = default_reader
        else:
            self.normal = normal_reader


    def _read_format_spec(self, stream, opened_at=None):
        """
        Read a colon-suffix format spec until the field closing brace

        Nested ``{`` / ``}`` inside the spec adjust depth.

        :param stream: the source stream, positioned after the field colon
        :return: the raw format spec string; stream ends past the closing ``}``
        """

        parts = []
        depth = 0

        while True:
            c = stream.read(1)
            if not c:
                raise stream.reader_eof(opened_at=opened_at,
                                        context="f-string field")
            if c == '{':
                depth += 1
                parts.append(c)
            elif c == '}':
                if depth == 0:
                    break
                depth -= 1
                parts.append(c)
            else:
                parts.append(c)

        return ''.join(parts)


    def _read_field_expr(self, stream, opened_at=None):
        """
        Read the value expression of a ``{...}`` field

        A delimited form reads through the general reader. A leading
        atom is collected with ``:`` and ``}`` as extra terminators so
        the format spec stays outside it, then suffix adjacency applies,
        so ``{foo[0]}`` is one subscript form rather than ``[0]`` in the
        spec position. A unary prefix the bare grammar admits (``-``,
        ``+`` or ``~`` before a name or an opening delimiter, under
        ``reader/unary``) wraps the operand read the same way, so
        ``{-x:04d}`` is ``(- x)`` with its spec; ``-`` before a digit is
        the literal's sign and stays in the atom.

        :param stream: the source stream, positioned at the expression
        :param opened_at: (line, col) of the opening ``{``
        :return: tuple of (event, position, value)
        """

        norm = self.normal
        position = stream.position()
        lead = stream.peek(1)

        if lead in '([{':
            return norm._read(stream)

        if lead and lead in _unary_symbols and norm.unary:
            peek = stream.peek(2)
            nxt = peek[1:2]
            digit = nxt.isdigit() or (nxt == '.' and stream.peek(3)[2:3].isdigit())
            if (nxt.isalpha() or nxt == '_' or nxt in '([{'
                    or (digit and lead != '-')):
                stream.read(1)
                event, pos, child = self._read_field_expr(stream, opened_at)
                if event is EOF:
                    raise stream.reader_eof(
                        opened_at=opened_at, context=f"unary {lead}")
                if event is not VALUE:
                    raise stream.error(f"invalid use of {lead}", pos)
                return VALUE, position, cons(_unary_symbols[lead], child, nil)

        if lead and (lead.isalpha() or lead == '_' or lead.isdigit()
                     or lead == '-'):
            first = stream.read(1)
            terms = norm._terms
            for c in ':}':
                if c not in terms:
                    terms = terms + c
            expr = norm.process_atom(
                norm._collect_atom_continuation(stream, first, terms))
            expr = norm._dispatch_suffix_macros(stream, expr, position)
            return VALUE, position, expr

        return norm._read(stream)


    def _read_format_field(self, stream, opened_at=None):
        """
        Read one ``{...}`` f-string field

        :param stream: the source stream, positioned after the opening ``{``
        :return: a ``(format ...)`` form for this field
        """

        norm = self.normal

        norm._skip_horizontal_whitespace(stream)
        event, pos, expr = self._read_field_expr(stream, opened_at)

        if event is EOF:
            raise stream.reader_eof(opened_at=opened_at,
                                    context="f-string field")
        if event is not VALUE:
            raise stream.error("invalid format field", pos)

        norm._skip_horizontal_whitespace(stream)
        peek = stream.peek(1)

        if peek == ':':
            stream.read(1)
            spec = self._read_format_spec(stream, opened_at)
            child = cons(_symbol_format, expr, spec, nil)

        elif peek == '}':
            stream.read(1)
            child = cons(_symbol_format, expr, nil)

        else:
            forms = [expr]
            while stream.peek(1) != '}':
                event, pos, val = norm._read(stream)
                if event is EOF:
                    raise stream.reader_eof(opened_at=opened_at,
                                            context="f-string field")
                if event is not VALUE:
                    raise stream.error("invalid format field", pos)
                forms.append(val)
            stream.read(1)
            child = cons(_symbol_format, *forms, nil)

        if is_pair(expr) and expr.get_position():
            field_pos = expr.get_position()
        else:
            field_pos = pos
        child.set_position(field_pos)

        return child


    def _read_literal_escape(self, stream):
        """
        Read one backslash escape sequence from literal text and decode it

        :param stream: the source stream, positioned at the backslash
        :return: the decoded escape (a lone backslash if no sequence matches)
        """

        window = stream.peek(_FSTRING_ESCAPE_PEEK)
        match = ESCAPE_SEQUENCE_RE.match(window)
        if match:
            text = match.group(0)
            stream.read(len(text))
            return _as_unicode(text)
        return stream.read(1)


    def _read_raw_field(self, stream, opened_at=None):
        """
        Read one field's raw text up to and consuming the matching ``}``

        Tracks brace depth (for nested spec fields) and inner strings, where
        a ``\\"`` opens or closes a string so that braces inside it are not
        treated as delimiters. Escapes are left undecoded; the caller decodes
        the returned text before parsing it.

        :param stream: the source stream, positioned after the opening ``{``
        :return: the raw field text without the closing ``}``
        """

        parts = []
        depth = 0
        in_string = False

        while True:
            c = stream.read(1)
            if not c:
                raise stream.reader_eof(opened_at=opened_at,
                                        context="f-string field")
            if c == '\\':
                nxt = stream.read(1)
                if not nxt:
                    raise stream.reader_eof(opened_at=opened_at,
                                            context="f-string field")
                parts.append(c)
                parts.append(nxt)
                if nxt == '"':
                    in_string = not in_string
                continue
            if in_string:
                parts.append(c)
                continue
            if c == '{':
                depth += 1
            elif c == '}':
                if depth == 0:
                    return ''.join(parts)
                depth -= 1
            parts.append(c)


    def _read_field(self, stream, opened_at=None):
        """
        Read one ``{...}`` field: extract its raw text, decode, and parse

        :param stream: the source stream, positioned after the opening ``{``
        :param opened_at: (line, col) of the opening ``{``, named when the
            field is never closed
        :return: a ``(format ...)`` form for this field
        """

        pos = stream.position()
        raw = self._read_raw_field(stream, opened_at)
        substream = source_str(_as_unicode(raw) + '}', stream.filename,
                               auto_skip_exec=False,
                               line_no=pos[0], col_no=pos[1])
        return self._read_format_field(substream, opened_at)


    def split(self, stream):
        """
        Split a raw format string into literal and expression parts

        The stream holds the undecoded f-string body. Literal runs are
        accumulated (with escapes decoded individually) and flushed as one
        string at each field or escaped-brace boundary, so ``{{`` / ``}}``
        remain their own literal parts. Fields are read by :meth:`_read_field`.

        :param stream: the source stream containing the raw format string
        :return: list of string literals and expression forms
        """

        result = []
        literal = []

        def flush():
            if literal:
                result.append(''.join(literal))
                literal.clear()

        try:
            while True:
                chunk = stream.read_until_chars("{}\\")
                if chunk:
                    literal.append(chunk)

                c = stream.peek(1)
                if not c:
                    break

                if c == '\\':
                    literal.append(self._read_literal_escape(stream))
                    continue

                if c == '{':
                    open_pos = stream.position()
                    stream.read(1)
                    if stream.peek(1) == '{':
                        stream.read(1)
                        flush()
                        result.append('{')
                        continue
                    flush()
                    result.append(self._read_field(stream, open_pos))
                    continue

                # c == '}'
                stream.read(1)
                if stream.peek(1) == '}':
                    stream.read(1)
                    flush()
                    result.append('}')
                    continue
                raise FormatStringSyntaxError(
                    "mis-matched closing } in f-string",
                    stream.position(),
                    filename=stream.filename)

        except ReaderEOF as eof:
            # the f-string's closing quote was already found, so the body
            # is complete: running out of it inside a field is a
            # malformed literal, never a request for more input
            raise FormatStringSyntaxError(eof.message, eof.location,
                                          filename=eof.filename,
                                          text=eof.text) from None
        except ReaderSyntaxError as rex:
            raise FormatStringSyntaxError(rex.message, rex.location,
                                          filename=rex.filename,
                                          text=rex.text)

        flush()
        return result


default_reader = Reader()


# The end.
