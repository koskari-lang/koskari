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
koskari.reader.parser_ng

Koskari's s-expression parser with a clean three-layer stream architecture.

Layers:

1. BaseSourceStream (C native) -- hot-path I/O with Unicode position tracking
2. SourceStream(BaseSourceStream) -- error reporting helpers
3. IndentAwareStream(SourceStream) -- indent-bounded skip_whitespace, dedented flag

The Reader reads any SourceStream. LayoutReader subclasses Reader as a
layout coordinator: it owns structural colon, LINE_END header tails, and
layout-only prefixes; it holds an inner Reader with full s-expr defaults.
Bridge macros on the coordinator forward ``( ) [ ] { }`` ``"`` ``;`` ``f``
``r`` ``b`` ``#`` into the inner reader so delimited reads do not see layout
rules. LayoutReader requires an IndentAwareStream for body reads. No reader
method inspects private attributes of streams it receives; the stream
interface is uniform and well-defined.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from codecs import decode
from contextlib import contextmanager
from decimal import Decimal as decimal
from fractions import Fraction as fraction
from functools import partial
from io import StringIO
from os.path import exists
from re import UNICODE, VERBOSE
from re import compile as regex

from ..exceptions import (FormatStringSyntaxError, ReaderEOF, ReaderSyntaxError)
from ..lib import (BaseSourceStream, Module, bracket_nil, bracket_pair, cons,
                   keyword, nil, pair, setcdr, symbol)
from ..types import is_nil, is_pair
from . import patterns as _patterns
from .layouts import apply_layout, apply_layouts, find_layout


__all__ = (
    "ReaderSyntaxError",
    "ReaderEOF",
    "FormatStringSyntaxError",
    "SourceStream",
    "IndentAwareStream",
    "source_open",
    "source_str",
    "source_stream",
    "Reader",
    "LayoutReader",
    "default_reader",
    "READ_EOF",
    "DEFER",
    "LINE_END",
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

_symbol__str = symbol("compile/#str")
_symbol_gensym_thunk = symbol("compile/gensym-thunk")
_symbol_decimal = symbol("decimal")
_symbol_format = symbol("format")
_symbol_fraction = symbol("fraction")
_symbol_quasiquote = symbol("compile/quasiquote")
_symbol_attr = symbol("compile/attr")
_symbol_quote = symbol("compile/quote")
_symbol_layout_quote = symbol("quote")
_symbol_layout_quasiquote = symbol("quasiquote")
_symbol_compile_decorate = symbol("compile/decorate")
_symbol_ref = symbol("compile/ref")
_symbol_refq = symbol("compile/refq")
_symbol_splice = symbol("compile/splice")
_symbol_unquote = symbol("compile/unquote")
_symbol_unquote_splicing = symbol("compile/unquote-splicing")
_symbol_pass_none = symbol("reader/pass-none")
_keyword_block = keyword(":block:")

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
_dotted_attr_atom_re = regex(
    r"^[a-zA-Z_][a-zA-Z0-9/_-]*(\.[a-zA-Z_][a-zA-Z0-9/_-]*)+$").match
_ref_atom_re = regex(r"^&\S+$").match
_ellipsis_atom_re = regex(r"^\.\.\.$").match
_py_singleton_atom_re = regex(r"^(None|True|False)$").match
_pass_atom_re = regex(r"^pass$").match


_as_integer = int
_as_bin = partial(int, base=2)
_as_oct = partial(int, base=8)
_as_hex = partial(int, base=16)


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


def _as_pass_none(_s):
    """
    Reader symbol macro converting the ``pass`` symbol into the None literal
    """

    return None


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


def _as_dotted_attr_atom(s):
    """
    Convert dotted identifier atom (e.g. foo.bar) to (compile/attr obj name).
    Left-associative: foo.bar.baz becomes (compile/attr (compile/attr foo bar)
    baz).
    """

    parts = s.split(".")
    obj = symbol(parts[0])
    for name in parts[1:]:
        obj = cons(_symbol_attr, obj, symbol(name), nil)

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
    """

    return decode(match.group(0), 'unicode-escape')


def _as_unicode(s, pattern=ESCAPE_SEQUENCE_RE):
    """
    Process escape sequences in a string

    :param s: the string containing escape sequences
    :param pattern: the regex pattern for escape sequences
    :return: the string with escape sequences decoded
    """

    return pattern.sub(_decode_sub, s)


# ===========================================================================
# Stream layer
# ===========================================================================


class SourceStream(BaseSourceStream):
    """
    Source stream with error reporting helpers

    Inherits from the native BaseSourceStream for efficient character-level
    I/O with position tracking. Adds error reporting and the helper methods
    used by the reader (skip_exec, suspend_indent).
    """

    def __init__(
            self,
            stream,
            filename=None,
            auto_skip_exec=True,
            line_no=1,
            col_no=0):
        """
        Initialize a source stream

        :param stream: the underlying seekable stream
        :param filename: the filename for error reporting
        :param auto_skip_exec: if True, skip any #! line at the start
        :param line_no: starting line number (default 1)
        :param col_no: starting column number (default 0)
        :raises TypeError: if stream is not seekable
        """

        if not stream.seekable():
            raise TypeError("SourceStream's stream argument must be seekable")

        super().__init__(stream, filename, line=line_no, col=col_no)

        if auto_skip_exec:
            self.skip_exec()


    def error(self, message, position=None):
        """
        Create a ReaderSyntaxError for the given or current position

        :param message: the error message
        :param position: optional (line, col) tuple; uses current if None
        :return: a ReaderSyntaxError instance
        """

        text = None

        if position is None:
            position = self.lin, self.col

        if exists(self.filename):
            with open(self.filename, "rt") as fin:
                for text, _lineno in zip(fin, range(0, position[0])):
                    pass

        return ReaderSyntaxError(message, position,
                                 text=text, filename=self.filename)


    def reader_eof(self, position=None, *, context=None):
        """
        Build a ReaderEOF for stream end inside an unfinished form

        Does not read the source file for a source line snippet; callers
        raise this immediately and REPL parse paths map it to incomplete
        input without displaying the exception.

        :param position: optional (line, col) tuple; uses current if None
        :param context: optional phrase appended after unexpected EOF
        :return: a ReaderEOF instance (caller should raise it)
        """

        if position is None:
            position = self.lin, self.col

        message = "unexpected EOF"
        if context:
            message = f"{message} in {context}"

        return ReaderEOF(message, position,
                         text=None, filename=self.filename)


    def __iter__(self):
        """
        Iterate over characters in the stream
        """

        return iter(partial(self.read, 1), '')


    @contextmanager
    def speculate(self):
        """
        Mark the stream on entry and rewind to that mark on exit

        For pure lookahead: read freely inside the block to inspect upcoming
        input; the stream (position and line/col) is restored to its
        pre-block state on exit, whether the block returns or raises. Unlike
        a fixed-size peek, a speculative scan sees the whole remaining input.
        """

        checkpoint = self.mark()
        try:
            yield
        finally:
            self.rewind(checkpoint)


    def peek_line(self):
        """
        Peek the rest of the current physical line without consuming it

        Returns the text from the current position up to (not including) the
        next newline or carriage return, or to end of stream. Unbounded: the
        result is the whole line regardless of length.

        :return: the remaining text on the current physical line
        """

        with self.speculate():
            return self.read_until_chars("\r\n")


    def skip_exec(self):
        """
        Skip a #! line at the start of the stream
        """

        if self.peek(2) == "#!":
            self.readline()


    @contextmanager
    def suspend_indent(self):
        """
        No-op indent suspension for streams without indent tracking

        Streams that do not bound whitespace skipping by indentation have
        nothing to suspend, so this simply yields.
        """

        yield


class IndentAwareStream(SourceStream):
    """
    Source stream with indent-bounded skip_whitespace

    Extends SourceStream with a max_indent threshold. When max_indent is set,
    skip_whitespace inspects the indent of the next content line without
    consuming it. If that indent is at or below max_indent, skip_whitespace
    sets self.dedented = True and returns without consuming any characters,
    leaving the stream positioned at the newline so the outer reader can
    handle the boundary.

    When max_indent is None, skip_whitespace delegates to the native C
    implementation (BaseSourceStream.skip_whitespace) for maximum speed.

    The dedented attribute is reset to False at the start of each
    skip_whitespace call. Callers read it immediately after skip_whitespace
    returns; it is never guarded by getattr.
    """

    def __init__(self, stream, filename=None, auto_skip_exec=True,
                 line_no=1, col_no=0, max_indent=None):
        """
        Initialize an indent-aware source stream

        :param stream: the underlying seekable stream
        :param filename: the filename for error reporting
        :param auto_skip_exec: if True, skip any #! line at the start
        :param line_no: starting line number (default 1)
        :param col_no: starting column number (default 0)
        :param max_indent: column value; skip_whitespace stops without
            consuming when a content line's indent is <= this value
        """

        super().__init__(stream, filename, auto_skip_exec=auto_skip_exec,
                         line_no=line_no, col_no=col_no)
        self._max_indent = max_indent
        self.dedented = False


    def skip_whitespace(self):
        """
        Skip whitespace, stopping at dedent when max_indent is set

        When max_indent is None, delegates to the native C skip_whitespace.

        Otherwise, speculatively scans the upcoming whitespace and blank
        lines to find the next content character. Blank lines are skipped.
        The first non-blank content line is measured:

        - If the content is on the current line (no newline was crossed),
          the leading horizontal whitespace is consumed and the stream is
          left at the content.

        - If a newline was crossed and the content line's indent is
          <= max_indent, sets self.dedented = True and consumes nothing,
          leaving the stream at the boundary.

        - If a newline was crossed and the indent is > max_indent, consumes
          the newline(s) and indent, leaving the stream at the content.

        The scan is speculative (mark/rewind) so its lookahead is unbounded
        regardless of how many blank lines separate the content.
        """

        if self._max_indent is None:
            self.dedented = False
            return super().skip_whitespace()

        self.dedented = False
        max_indent = self._max_indent

        # Being at the start of a line (col 0) means the current line's own
        # indent is the dedent candidate, exactly as if a newline had just
        # been crossed.
        saw_newline = (self.col == 0)
        indent = 0
        consume = 0

        with self.speculate():
            while True:
                c = self.read(1)
                if not c:
                    # EOF within whitespace: dedent, consume nothing.
                    consume = None
                    break
                if c == '\n':
                    saw_newline = True
                    indent = 0
                    consume += 1
                elif c in ' \t':
                    indent += 1
                    consume += 1
                elif not saw_newline:
                    # Content on the current line: keep the horizontal
                    # whitespace already scanned.
                    break
                elif indent <= max_indent:
                    consume = None
                    break
                else:
                    break

        if consume is None:
            self.dedented = True
            return ''
        if consume:
            self.read(consume)
        return ''


    @contextmanager
    def suspend_indent(self):
        """
        Temporarily disable indent-bounded whitespace skipping

        Indentation has no meaning inside a balanced delimited form, so a
        pair read suspends the indent guard for the duration of its body.
        While suspended, max_indent is None and skip_whitespace consumes
        vertical whitespace freely (native behavior), letting a form span
        dedented lines up to its closing delimiter. The previous max_indent
        is restored on exit.
        """

        old = self._max_indent
        self._max_indent = None
        try:
            yield
        finally:
            self._max_indent = old


    @contextmanager
    def indent_limit(self, max_indent):
        """
        Bound skip_whitespace by ``max_indent`` for the duration

        While active, skip_whitespace sets :attr:`dedented` and stops without
        consuming when the next content line is at or below ``max_indent``.
        The previous limit is restored on exit.

        :param max_indent: column that ends a block body on dedent
        """

        old = self._max_indent
        self._max_indent = max_indent
        try:
            yield
        finally:
            self._max_indent = old


# ===========================================================================
# Stream factory helpers
# ===========================================================================


@contextmanager
def source_open(filename, auto_skip_exec=True):
    """
    Context manager to open a file as a SourceStream

    :param filename: path to the file to open
    :param auto_skip_exec: if True, skip any #! line at the start
    :yields: SourceStream for the file
    """

    with open(filename, "rt") as fs:
        reader = SourceStream(fs, filename=filename,
                              auto_skip_exec=auto_skip_exec)
        yield reader


def source_str(src, filename, auto_skip_exec=True, line_no=1, col_no=0):
    """
    Create a SourceStream from a string

    :param src: the source string
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :param line_no: starting line number
    :param col_no: starting column number
    :return: a SourceStream
    """

    return SourceStream(
        StringIO(src),
        filename,
        auto_skip_exec=auto_skip_exec,
        line_no=line_no,
        col_no=col_no)


def source_stream(underlying, filename, auto_skip_exec=True):
    """
    Create a SourceStream wrapping an existing stream

    :param underlying: the stream to wrap
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :return: a SourceStream
    """

    return SourceStream(underlying, filename,
                        auto_skip_exec=auto_skip_exec)


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
        self.terminating = set(_BASE_TERMINATORS)
        self._terms = "".join(sorted(self.terminating))
        self.bracket_pairs = bracket_pairs

        if not nodefaults:
            self._add_default_macros()
            self._add_default_atoms()


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

        return event, position, value


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

        yield self

        for c in char:
            lst = self.reader_macros.get(c)
            if lst and lst[0] is triplet:
                lst.pop(0)
                if not lst:
                    del self.reader_macros[c]
        self._sync_terms_from_macros()


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

        sm("'", self._read_quote, True)
        sm("`", self._read_quasiquote, True)


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


    def set_pass_none(self, enabled):
        """
        Install or remove the reader symbol macro that reads the ``pass``
        symbol as the None literal

        :param enabled: True to install the macro, False to remove it
        """

        if enabled:
            self.set_atom_pattern(
                _symbol_pass_none, _pass_atom_re, _as_pass_none)
        else:
            self.clear_atom_pattern(_symbol_pass_none)


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

        ap(symbol("compile/refq"), _ref_atom_re, self._as_ref_atom)
        ap(symbol("compile/attr"), _dotted_attr_atom_re, _as_dotted_attr_atom)
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


    def _as_ref_atom(self, s):
        """
        Convert &rest atom to (compile/ref expanded). Strips the leading &
        and defers to process_atom for the rest.
        """

        rest = s[1:]
        expanded = self.process_atom(rest)
        return cons(_symbol_ref, expanded, nil)


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
                    raise stream.reader_eof(position)

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
        Read a collection literal (#(...), #[...], #{...})

        :param stream: the source stream
        :param fchar: the '#' character
        :return: tuple of (event, value)
        """

        peek = stream.peek(1)
        if peek not in "({[<":
            return DEFER, None

        peek = stream.read(1)
        i = "({[".index(peek)

        closer = ")}]"[i]
        name = ("#tuple", "#dict", "#list")[i]

        pos = stream.position()

        with self.temporary_event_macro(closer, self._close_pair, True):
            event, result = self._read_pair(closer, stream, peek)

        if event is VALUE:
            new_result = cons(symbol(name), result)
            new_result.set_position(pos)
            result = new_result

        return event, result


    def _read_ref_call(self, stream, fchar):
        """
        Read a by-ref compound expression (&(expr ...))

        Triggers only when ``&`` is immediately followed by ``(``; otherwise
        defers so ``&rest`` atoms continue to read via the atom pattern path.

        :param stream: the source stream
        :param fchar: the '&' character
        :return: tuple of (event, value)
        """

        if stream.peek(1) != "(":
            return DEFER, None

        event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            raise stream.error(f"invalid use of {fchar}", pos)

        return VALUE, cons(_symbol_ref, child, nil)


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

        if stream.peek(3) == '"""':
            col += 2

        # Read the body raw (escapes undecoded) so the field/literal split
        # sees source characters: an escape that decodes to a brace must not
        # act as a field delimiter, and embedded expression positions must
        # track the source rather than the post-escape text. Escapes are
        # decoded per literal chunk and per field interior in the splitter.
        _evnt, data = self._read_string(stream, char, with_escapes=False)

        substream = source_str(data, stream.filename,
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
        else:
            event, val = self._read_string(stream, stream.read(1), True)
            return event, val.encode('ascii')


    def _read_string(self, stream, char, with_escapes=True):
        """
        Read a string literal

        :param stream: the source stream
        :param char: the quote character
        :param with_escapes: if True, process escape sequences
        :return: tuple of (VALUE, string)
        """

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
            raise stream.reader_eof(stream.position())

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
            raise stream.reader_eof(stream.position())

        if with_escapes:
            value = _as_unicode(value)

        return VALUE, value


    def _read_quote(self, stream, char):
        """
        Read a quoted expression ('expr)

        :param stream: the source stream
        :param char: the quote character
        :return: tuple of (VALUE, (quote expr))
        """

        event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            msg = f"invalid use of {char}"
            raise stream.error(msg, pos)

        return VALUE, cons(_symbol_quote, child, nil)


    def _read_quasiquote(self, stream, char):
        """
        Read a quasiquoted expression (`expr)

        :param stream: the source stream
        :param char: the backquote character
        :return: tuple of (VALUE, (quasiquote expr))
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            msg = f"invalid use of {char}"
            raise stream.error(msg, pos)

        return VALUE, cons(_symbol_quasiquote, child, nil)


    def _read_unquote(self, stream, char):
        """
        Read an unquoted expression (,expr)

        :param stream: the source stream
        :param char: the comma character
        :return: tuple of (VALUE, (unquote expr) or (unquote-splicing expr))
        """

        with self.temporary_event_macro("@", self._read_splice, True):
            event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            msg = f"invalid use of {char}"
            raise stream.error(msg, pos)

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

        event, pos, child = self._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            msg = f"invalid use of {char}"
            raise stream.error(msg, pos)

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
# LayoutReader
# ===========================================================================


class LayoutReader(Reader):
    """
    Reader that supports layout-based block syntax

    Subclasses Reader and adds indent-aware block reading. Resolves
    layout heads via namespace (layout/<head>). Requires a namespace
    for layout resolution, like the compiler.

    **Three default surfaces:** (1) ``self.inner`` is a plain :class:`Reader`
    with full s-expr defaults—balanced forms and prefix reads there use only
    inner state. (2) **Bridge** macros on this coordinator forward ``( ) [ ] {
    }`` ``"`` ``;`` ``f`` ``r`` ``b`` ``#`` into ``inner`` so delimiter and
    string/comment/prefix parsing matches s-expr mode. (3) **Layout-only**
    registrations: structural ``:``, ``_layout_macros`` (e.g. ``@``), and
    stacked ``'`` / backtick (predicated raw block then normal quote).

    Top-level layout reads are **line-oriented**: header-mode values on a
    logical line are collected until ``LINE_END`` or structural ``COLON``.
    Empty lines are skipped. Multiple values on one line fold to an implicit
    ``cons`` chain or raise per :attr:`layout_implicit_calls`. A structural
    colon ends the block header (``head`` plus ``args``); the body is read
    until dedent and passed to the layout handler (non-symbol heads use the
    ``layout/block`` fallback when no named layout matches). Structural ``:``
    rules still allow keyword atoms such as ``as:`` inside header arguments
    at delimiter depth 0.

    Stream interface contract: LayoutReader expects its stream arguments
    to be SourceStream instances (for pushback support). Body reads always
    use an IndentAwareStream so that dedent detection is available through
    the stream.dedented attribute rather than getattr guards.

    Subclasses may override _add_default_layout_macros to register or omit
    layout-mode-only prefix handlers (_layout_macros). Raw block quote and
    quasiquote use predicated reader macros on ``'`` and backtick when
    defaults are enabled.

    Layout header tails use LINE_END and COLON events with horizontal-only
    whitespace between tokens on a logical line.
    """

    def __init__(
            self,
            namespace,
            nodefaults=False,
            layout_enabled=True,
            layout_implicit_calls=True,
            bracket_pairs=True):
        """
        Initialize the layout reader

        :param namespace: Module, Namespace, or dict for layout lookup
        :param nodefaults: if True, do not add default macros and patterns
        :param layout_implicit_calls: if True, multiple values on one layout
            line fold to an implicit call; if False, more than one value is an
            error
        :param bracket_pairs: if True (default), ``[ ]`` reads into distinct
            bracket-pair / bracket-nil values; if False, as plain pairs
        """

        super().__init__(nodefaults=True, bracket_pairs=bracket_pairs)
        self._inner = Reader(nodefaults=True, bracket_pairs=bracket_pairs)

        from ..lib import ModuleNamespace

        if isinstance(namespace, Module):
            self._ns = vars(namespace)
        elif isinstance(namespace, dict):
            self._ns = ModuleNamespace('layout', namespace)
        else:
            self._ns = namespace

        self._block_mode = False
        self._layout_macros = {}

        self.layout_enabled = layout_enabled
        self.layout_implicit_calls = layout_implicit_calls

        if not nodefaults:
            self._add_default_macros()
            self._add_default_atoms()

        # this one describes a fundamental behavior of the reader, so it's not
        # a default macro, it's an always macro.
        self.add_layout_event_macro(
            ':', self._layout_colon_macro, True,
            predicate=self._layout_colon_predicate)

        self._header_scan = False


    @property
    def inner(self):
        """
        The dedicated s-expression reader used inside bridge macros.

        Attach s-expr-only extensions with ``reader.inner.add_event_macro``.
        """

        return self._inner


    def add_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):

        self.add_layout_event_macro(char, macro_fn, terminating, predicate)
        self.add_inner_event_macro(char, macro_fn, terminating, predicate)


    def add_layout_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):
        super().add_event_macro(char, macro_fn, terminating, predicate)


    def add_inner_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):
        self._inner.add_event_macro(char, macro_fn, terminating, predicate)


    def _add_default_macros(self):
        """
        Register bridge macros that forward delimiter, string, comment, and
        prefix reads to :attr:`_inner`.
        """

        sm = self.add_event_macro
        inner = self._inner

        sm('(', partial(inner._read_pair, ')'), True)
        sm(')', inner._close_pair, True)

        sm('[', partial(inner._read_pair, ']'), True)
        sm(']', inner._close_pair, True)

        sm('{', partial(inner._read_thunk, '}'), True)
        sm('}', inner._close_pair, True)

        sm('"', inner._read_string, True)
        sm(';', inner._read_comment, True)

        sm('f', inner._read_fstring, False)
        sm('r', inner._read_rstring, False)
        sm('b', inner._read_bstring, False)

        sm('#', inner._read_collection, False)
        sm('&', inner._read_ref_call, False)

        self.add_event_macro("'", inner._read_quote, True)
        self.add_event_macro("`", inner._read_quasiquote, True)

        self.add_layout_event_macro(
            "'",
            self._layout_quote_block_reader_macro,
            True,
            predicate=self._layout_block_quote_predicate)
        self.add_layout_event_macro(
            "`",
            self._layout_quasiquote_block_reader_macro,
            True,
            predicate=self._layout_block_quote_predicate)

        # self.add_layout_event_macro("@", self._read_layout_decorate, False)
        self._layout_macros.update({
            "@": self._read_layout_decorate,
        })


    @contextmanager
    def temporary_event_macro(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Context manager to temporarily apply a character event macro

        Applies the triplet to both the layout coordinator (via the base
        implementation) and the inner s-expr reader for the duration, so
        delimited reads inside the block see it too. Each side removes only
        its own triplet on exit.

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, char) -> (event, value); may return
            (DEFER, None) like other event macros (see add_event_macro).
        :param terminating: if True, this character terminates atoms
        :param predicate: optional predicate (see :meth:`add_event_macro`)
        :yields: self
        """

        with super().temporary_event_macro(
                char, macro_fn, terminating, predicate), \
                self._inner.temporary_event_macro(
                    char, macro_fn, terminating, predicate):
            yield self


    def set_atom_pattern(self, namesym, match_fn, conversion_fn):
        self._inner.set_atom_pattern(namesym, match_fn, conversion_fn)


    def process_atom(self, atom):
        return self._inner.process_atom(atom)


    @classmethod
    def from_pragma(cls, namespace, pragma=None):
        """
        Build a reader from a module pragma (a default pragma when omitted),
        apply its reader settings, and subscribe for later pragma changes.
        """

        if pragma is None:
            from ..pragma import ModulePragma
            pragma = ModulePragma()

        inst = cls(namespace)
        inst.reload_pragma(pragma)
        pragma.watch(inst)
        return inst


    def set_layout_modes(self, *, layout_enabled=None, layout_implicit_calls=None):
        """
        Update layout-mode feature switches on this reader.

        Intended as the stable hook for future pragma-driven reader reload.
        Any None parameter leaves the current setting unchanged.
        """

        if layout_enabled is not None:
            self.layout_enabled = bool(layout_enabled)
        if layout_implicit_calls is not None:
            self.layout_implicit_calls = bool(layout_implicit_calls)


    def reload_pragma(self, pragma):
        """
        Apply reader settings from a module pragma: reader/layout and
        reader/implicit-calls drive the layout switches; reader/brackets sets
        bracket-pair reading on this reader and its inner reader;
        reader/pass-none toggles the ``pass`` symbol macro.
        """

        if pragma is None:
            return

        self.set_layout_modes(
            layout_enabled=pragma.get("reader/layout"),
            layout_implicit_calls=pragma.get("reader/implicit-calls"))

        brackets = pragma.get("reader/brackets")
        if brackets is not None:
            self.bracket_pairs = bool(brackets)
            self._inner.bracket_pairs = bool(brackets)

        pass_none = pragma.get("reader/pass-none")
        if pass_none is not None:
            self._inner.set_pass_none(bool(pass_none))


    @contextmanager
    def _header_mode(self):
        """
        Enable header-aware ``:`` predicate behavior for atom scans and
        dispatch.
        """
        old = self._header_scan
        self._header_scan = True
        try:
            yield
        finally:
            self._header_scan = old


    def _layout_colon_predicate(self, stream, c, atom_prefix):
        """
        Gate ``:`` dispatch and predicated atom boundaries for layout headers.

        When ``atom_prefix`` is None, this is leading-character dispatch.
        Otherwise it is an atom boundary before an unconsumed ``:``.

        :param stream: the source stream
        :param c: the ``:`` character
        :param atom_prefix: atom text before ``c``, or None at dispatch
        :return: True to run the macro or split the atom before ``:``
        """

        if not self._header_scan:
            if atom_prefix is None:
                return False
            return True
        if atom_prefix is None:
            return self._is_structural_colon_after_consumed(stream)
        return self._is_structural_colon_at_next(stream)


    def _layout_colon_macro(self, stream, c):
        """
        Emit a structural header COLON.

        :meth:`_layout_colon_predicate` ensures this runs only for that case
        at dispatch time; nested layout and non-header ``:`` skip the macro.
        """

        return COLON, symbol(':')


    def _layout_block_quote_predicate(self, stream, c, atom_prefix):
        """
        Allow layout raw-block quote and quasiquote when this line has a
        structural block colon and the head does not open with a delimiter.

        :param stream: source stream positioned after the prefix character
        :param c: the prefix character
        :param atom_prefix: atom boundary context; None at dispatch
        """

        if atom_prefix is not None:
            return True
        if not self.layout_enabled:
            return False
        nxt = stream.peek(1)
        if not nxt or nxt in '([{':
            return False
        return self._structural_colon_before_line_end(stream)


    def _layout_quote_block_reader_macro(self, stream, c):
        """
        Reader macro for layout raw-block quote.

        :param stream: positioned after ``'``
        :param c: the quote character
        """

        _lin, col = stream.position()
        outer_indent = col - 1
        return BLOCK, self._read_quote_block(stream, outer_indent)


    def _layout_quasiquote_block_reader_macro(self, stream, c):
        """
        Reader macro for layout raw-block quasiquote.

        :param stream: positioned after the backtick
        :param c: the backtick character
        """

        _lin, col = stream.position()
        outer_indent = col - 1
        return BLOCK, self._read_quasiquote_block(stream, outer_indent)


    @staticmethod
    def _rest_after_colon_is_structural(rest):
        """
        Apply the structural-colon rule to the text following a ``:``.

        A ``:`` is a structural header terminator when the remainder of its
        physical line, after horizontal whitespace, is empty (end of line) or
        the start of a comment.

        :param rest: line text after the ``:`` (no newline)
        :return: True if the colon is structural
        """

        rest = rest.lstrip(' \t')
        return rest == '' or rest[0] == ';'


    def _is_structural_colon_after_consumed(self, stream):
        """
        Decide whether a header-terminator `:` is structural.

        Assumes the `:` has already been consumed and the stream is positioned
        right after it.
        """

        return self._rest_after_colon_is_structural(stream.peek_line())


    def _is_structural_colon_at_next(self, stream):
        """
        Decide whether the next `:` is a structural header terminator.

        Assumes the current stream position begins with `:`.
        """

        line = stream.peek_line()
        if not line or line[0] != ':':
            return False
        return self._rest_after_colon_is_structural(line[1:])


    def find_layout(self, name):
        return find_layout(self._ns, name)


    def _structural_colon_before_line_end(self, stream):
        """
        True if the current layout line contains a structural header colon.

        Speculatively scans from the stream position until a structural
        header ``:`` is found or the logical line ends (a newline or comment
        at delimiter depth 0), tracking delimiter depth. Newlines enclosed in
        brackets do not end the scan. The stream is left unchanged.

        :param stream: the source stream
        :return: whether a structural ``:`` exists on this line
        """

        with stream.speculate():
            depth = 0
            while True:
                stream.read_until_chars("([{)]}:;\r\n")
                ch = stream.read(1)
                if not ch:
                    return False
                if depth == 0 and ch in ';\r\n':
                    return False
                if ch in '([{':
                    depth += 1
                elif ch in ')]}':
                    depth -= 1
                elif ch == ':' and depth == 0:
                    if self._rest_after_colon_is_structural(stream.peek_line()):
                        return True


    def _read_header_form(self, stream):
        """
        Read one header token or report logical line end.

        The stream must be positioned after vertical whitespace at a token
        boundary.

        :param stream: the source stream
        :return: ``(LINE_END, pos, None)`` or the result of ``_read``
        """

        peek = stream.peek(1)
        if not peek or peek in '\n\r' or peek == ';':
            return LINE_END, stream.position(), None
        with self._header_mode():
            return super()._read(stream, vertical_ws=False)


    def _read_header_args(self, stream):
        """
        Read header argument forms until a structural COLON or end-of-line.
        """

        args = []
        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return args, False

            event, pos, value = self._read_header_form(stream)
            if event is LINE_END:
                return args, False
            if event is COLON:
                return args, True
            if event is EOF:
                return args, False
            args.append(value)


    def read_block_header(self, stream):
        """
        Read the block head and arguments up to a structural header ':'.

        Uses header-mode atom parsing so peer heads like `else:` are read as
        symbols, not keyword atoms.

        :param stream: the source stream positioned at the first header token
        :return: (head, args, opener) where head is the first form, args are
            the remaining forms before the colon, opener is symbol(':')
        :raises ReaderSyntaxError: on EOF or missing colon
        """

        with self._header_mode():
            event, pos, head = super()._read(stream)

        if event is COLON:
            raise stream.error('block header must have a head', pos)
        if event is EOF:
            raise stream.reader_eof(pos, context='block header')

        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', pos)

        return head, args, symbol(':')


    def _read_line_tail_forms(self, stream):
        """
        Read remaining top-level forms on the current logical line
        """

        forms = []
        while True:
            self._skip_horizontal_whitespace(stream)
            c = stream.peek(1)
            if not c or c in '\n\r;':
                return forms

            event, pos, value = self._read(stream, vertical_ws=False)
            if event is EOF:
                return forms
            if event is DOT:
                raise stream.error('invalid use of dot in layout line', pos)
            if event is not VALUE:
                raise stream.error('invalid syntax', pos)

            forms.append(value)


    def read_block_body(self, stream, outer_indent):
        """
        Read body elements until dedent or EOF

        stream must be an IndentAwareStream (guaranteed by LayoutReader.read
        wrapping the top-level stream, and by all internal callers using the
        same stream throughout). Temporarily installs outer_indent as the
        max_indent so that skip_whitespace stops and sets stream.dedented when
        the next content line is at or below that column. Restores the
        previous max_indent on exit.

        :param stream: an IndentAwareStream
        :param outer_indent: column that ends the block body on dedent
        :return: list of parsed body forms
        """

        body = []

        try:
            with stream.indent_limit(outer_indent):
                while True:
                    stream.skip_whitespace()
                    if stream.dedented:
                        break
                    if not stream.peek(1):
                        break
                    event, pos, form = self.read_form(stream)
                    if event is EOF:
                        break
                    elif event is BLOCKS:
                        body.extend(form.unpack())
                    else:
                        body.append(form)
        finally:
            stream.dedented = False

        return body


    def _peek_peer_head_symbol(self, stream):
        """
        Speculatively read the leading header token of the current line

        Reads one header-mode form and rewinds, so the stream is left
        unchanged. Used to test a candidate line against a layout's declared
        peer heads before committing to read it as a block. Reusing the real
        header read means peer heads are recognised exactly as ``read_block_header``
        would recognise them.

        :param stream: IndentAwareStream positioned at the candidate peer line
        :return: the head form (a symbol for declared peers), or None
        """

        with stream.speculate():
            with self._header_mode():
                event, _pos, head = super()._read(stream)

        return head if event is VALUE else None


    def _consume_through_blank_lines_to_peer_column(self, stream, outer_indent):
        """
        Consume from the newline ending the prior line across blank lines and
        comment-only lines until the stream sits at the first content character
        on a line indented to outer_indent (the peer block header).

        :param stream: IndentAwareStream with peek(1) the newline that ends the
            previous logical line
        :param outer_indent: column of the peer head line's leading spaces
        :return: True if a peer header line was reached, False otherwise
        """

        if stream.peek(1) != '\n':
            return False
        stream.read(1)

        while True:
            indent = 0
            while stream.peek(1) in (' ', '\t'):
                stream.read(1)
                indent += 1

            pk = stream.peek(1)
            if pk == '\n':
                stream.read(1)
                continue
            if pk == '\r':
                stream.read(1)
                if stream.peek(1) == '\n':
                    stream.read(1)
                continue
            if pk == ';':
                stream.read(1)
                stream.read_until_chars('\n')
                if stream.peek(1) == '\n':
                    stream.read(1)
                continue
            if not pk:
                return False
            if indent != outer_indent:
                return False
            return True


    def _collect_peer_blocks(self, stream, outer_indent, layout_obj):
        """
        Read any declared peer blocks following the primary block

        :param stream: IndentAwareStream after the primary block body
        :param outer_indent: column of the enclosing block head
        :param layout_obj: LayoutHandler whose peers attribute lists allowed
            peer head symbols
        :return: list of (:block: ...) conses, one per peer block
        """

        declared = tuple(layout_obj.peers or ())
        if not declared:
            return []

        declared_set = {str(sym) for sym in declared}
        peers = []

        while True:
            checkpoint = stream.mark()

            if not self._consume_through_blank_lines_to_peer_column(
                    stream, outer_indent):
                stream.rewind(checkpoint)
                break

            head = self._peek_peer_head_symbol(stream)
            if head is None or str(head) not in declared_set:
                stream.rewind(checkpoint)
                break

            phead, pargs, _opener = self.read_block_header(stream)
            peers.append(self._build_block(stream, outer_indent, phead, pargs))

        return peers


    def _build_block(self, stream, outer_indent, head, args):
        """
        Build the raw :block: cons structure: (:block: (head . args) body...)

        :param stream: source stream positioned after the block header colon
        :param outer_indent: column of the block head line
        :param head: the block head symbol
        :param args: list of forms between head and colon
        :return: (:block: (head . args) (body...)) cons with position set
        """

        pos = stream.position()
        body = self.read_block_body(stream, outer_indent)
        header = cons(head, *args, nil)
        body_list = cons(*body, nil) if body else nil
        result = cons(_keyword_block, header, body_list, nil)
        if is_pair(result):
            result.set_position(pos)
        return result


    def read_block(self, stream, outer_indent, head=None, args=None):
        """
        Read a block as a :block: cons without invoking layout handlers

        Used when :attr:`_block_mode` is set (quote/quasiquote inner blocks).
        Sets _block_mode for the duration of the body read so nested colons
        also produce raw :block: conses.

        :param stream: the source stream, positioned after the header colon
            and its trailing whitespace
        :param outer_indent: column of the block head line
        :param head: block head symbol if already read by the caller
        :param args: header argument forms if already read by the caller
        :return: (:block: (head . args) (body...)) cons
        """

        if head is None:
            head, args, opener = self.read_block_header(stream)

        prev = self._block_mode
        self._block_mode = True
        try:
            return self._build_block(stream, outer_indent, head, args)
        finally:
            self._block_mode = prev


    def _read_block_with_peers(self, stream, outer_indent, head, args):
        """
        Read a primary :block: and any stream-collected peer :block: conses

        :param stream: source stream positioned after the header colon
        :param outer_indent: column of the block head line
        :param head: block head symbol
        :param args: header argument forms
        :return: (primary_block, peer_blocks) where peer_blocks may be empty
        """

        primary = self._build_block(stream, outer_indent, head, args)
        layout_obj = self.find_layout(head)
        peer_blocks = []
        if layout_obj and layout_obj.peers:
            peer_blocks = self._collect_peer_blocks(
                stream, outer_indent, layout_obj)
        return primary, peer_blocks


    def _read_quote_block(self, stream, outer_indent):
        """
        Block-macro handler for the ' prefix

        Reads the block header and body, then wraps the raw :block: cons in a
        synthetic (:block: quote () (INNER)) for layout/quote to unbox

        :param stream: the source stream, positioned after the ' character
        :param outer_indent: column of the block head line
        :return: synthetic (:block: quote () (INNER)) cons
        """

        head, args, _ = self.read_block_header(stream)
        raw = self.read_block(stream, outer_indent, head, args)
        return cons(_keyword_block,
                    cons(_symbol_layout_quote, nil),
                    cons(raw, nil),
                    nil)


    def _read_quasiquote_block(self, stream, outer_indent):
        """
        Block-macro handler for the backtick prefix

        Installs the , and ,@ unquote reader macros for the duration of
        the header and body read so that unquote sites are recognised.
        Wraps the raw :block: cons in a synthetic (:block: quasiquote () (INNER))

        :param stream: the source stream, positioned after the ` character
        :param outer_indent: column of the block head line
        :return: synthetic (:block: quasiquote () (INNER)) cons
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            head, args, _ = self.read_block_header(stream)
            raw = self.read_block(stream, outer_indent, head, args)
        return cons(_keyword_block,
                    cons(_symbol_layout_quasiquote, nil),
                    cons(raw, nil),
                    nil)


    def _combine_implicit_line_tail_forms(self, forms):
        """
        Combine multiple forms on a layout line into one value (implicit call)

        :param forms: non-empty list of parsed values
        :return: single form or cons chain
        """

        if len(forms) == 1:
            return forms[0]
        return cons(forms[0], *forms[1:], nil)


    def _finish_header_line(self, stream, values, first_value_pos, pos):
        """
        Fold the values collected on one layout line into a VALUE result

        A single value is returned as-is; several values fold to an implicit
        call, or raise when :attr:`layout_implicit_calls` is disabled.

        :param stream: the source stream (for error reporting)
        :param values: non-empty list of values collected on the line
        :param first_value_pos: position of the first value
        :param pos: position of the line terminator (for error reporting)
        :return: (VALUE, first_value_pos, folded value)
        """

        if len(values) == 1:
            return VALUE, first_value_pos, values[0]
        if self.layout_implicit_calls:
            return (VALUE, first_value_pos,
                    self._combine_implicit_line_tail_forms(values))
        raise stream.error('multiple forms on one layout line', pos)


    def _read_layout_decorate(self, stream, outer_indent):
        """
        Layout-macro handler for the @ prefix (leading decorator lines)

        The first ``@`` was consumed by read_form. Reads more ``@`` lines at
        the same column, then one layout form (block or s-expr), and wraps
        ``(compile/decorate (d1 d2 ...) INNER)``.

        :param stream: the source stream, positioned after the first ``@``
        :param outer_indent: column of the first ``@`` character
        :return: (compile/decorate (decorators...) INNER) cons
        """

        decorators = []
        while True:
            forms = self._read_line_tail_forms(stream)
            if not forms:
                raise stream.error('empty decorator line', stream.position())

            decorators.append(self._combine_implicit_line_tail_forms(forms))
            stream.skip_whitespace()
            pk = stream.peek(1)
            if not pk:
                raise stream.reader_eof(
                    stream.position(), context='after decorator')

            col = stream.position()[1]
            if col != outer_indent:
                raise stream.error(
                    'decorator lines must align with the decorated form',
                    stream.position())

            if pk != '@':
                break

            stream.read(1)

        event, pos, inner = self.read_form(stream)
        if event is not VALUE and event is not BLOCK and event is not BLOCKS:
            raise stream.error('invalid syntax after decorators', pos)

        deco_list = cons(*decorators, nil)
        if event is BLOCKS:
            return cons(_symbol_compile_decorate, deco_list, inner)
        else:
            return cons(_symbol_compile_decorate, deco_list, inner, nil)


    def read_form(self, stream):
        """
        Read one layout form using a line-oriented header-mode loop

        When layout is disabled, delegates to :meth:`Reader._read` on
        :attr:`inner`.

        When layout is enabled: ``c`` in ``_layout_macros`` runs the layout-only
        prefix handler (e.g. ``@``). Otherwise collects header-mode values on
        the current logical line until ``LINE_END``, ``EOF``, or structural
        ``COLON`` (horizontal whitespace only between tokens). Empty lines are
        skipped. At ``LINE_END`` with several values, folds with ``cons`` or
        errors per :attr:`layout_implicit_calls`. At ``COLON``, returns
        ``BLOCK`` for a single :block: cons, or ``BLOCKS`` for a primary
        :block: plus stream-collected peer :block: conses on a spine.
        Expansion is deferred to :meth:`read`.

        :param stream: source stream positioned at the start of the next form
        :return: (event, position, value)
        """

        if not self.layout_enabled:
            return self._inner._read(stream)

        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return EOF, stream.position(), None

            position = stream.position()
            c = stream.peek(1)
            if not c:
                return EOF, position, None

            if c == ';':
                stream.read(1)
                stream.read_until_chars('\n')
                if stream.peek(1) == '\n':
                    stream.read(1)
                continue

            if c == '\r':
                stream.read(1)
                continue

            outer_indent = position[1]

            if c in self._layout_macros:
                stream.read(1)
                layout_macro = self._layout_macros[c]
                result = layout_macro(stream, outer_indent)
                if is_pair(result):
                    result.fill_position(position)
                return BLOCK, position, result

            values = []
            first_value_pos = None

            while True:
                self._skip_horizontal_whitespace(stream)
                if stream.dedented:
                    return EOF, stream.position(), None

                event, pos, value = self._read_header_form(stream)
                if event is BLOCK or event is BLOCKS:
                    return event, pos, value

                if event is LINE_END:
                    if not values:
                        break
                    return self._finish_header_line(
                        stream, values, first_value_pos, pos)

                if event is COLON:
                    if not values:
                        raise stream.error(
                            'block header must have a head', pos)
                    head = values[0]
                    args = values[1:]
                    header_indent = first_value_pos[1]
                    if self._block_mode:
                        return BLOCK, position, self.read_block(
                            stream, header_indent, head, args)
                    primary, peer_blocks = self._read_block_with_peers(
                        stream, header_indent, head, args)
                    if peer_blocks:
                        return BLOCKS, position, cons(primary, *peer_blocks, nil)
                    return BLOCK, position, primary

                if event is EOF:
                    if not values:
                        return EOF, pos, None
                    return self._finish_header_line(
                        stream, values, first_value_pos, pos)

                if event is DOT:
                    raise stream.error('invalid syntax', pos)

                if event is not VALUE:
                    raise stream.error('invalid syntax', pos)

                if first_value_pos is None:
                    first_value_pos = pos
                values.append(value)


    def read(self, reader_stream):
        """
        Read a single layout form from the stream

        Wraps reader_stream in an IndentAwareStream (with no max_indent
        constraint) if it is not already one, then delegates to
        read_form. This ensures stream.dedented is always available
        throughout the layout read without any getattr guards.

        At EOF returns READ_EOF (same as Reader.read), not None, so callers
        can distinguish end-of-stream from a parsed Python None literal.

        ``BLOCK`` values are expanded via :func:`apply_layout`; ``BLOCKS``
        spines via :func:`apply_layouts` (returning the expanded car).
        """

        if not isinstance(reader_stream, IndentAwareStream):
            reader_stream = IndentAwareStream(
                reader_stream.stream, reader_stream.filename,
                auto_skip_exec=False,
                line_no=reader_stream.lin,
                col_no=reader_stream.col,
                max_indent=None)

        event, pos, value = self.read_form(reader_stream)

        if event is BLOCK:
            return apply_layout(value, self._ns)
        elif event is BLOCKS:
            return apply_layouts(value, self._ns).car
        elif event is VALUE:
            return value
        elif event is EOF:
            return READ_EOF
        else:
            raise reader_stream.error('invalid syntax', pos)


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


    def _read_format_spec(self, stream):
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
                raise stream.reader_eof(stream.position())
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


    def _read_format_field(self, stream):
        """
        Read one ``{...}`` f-string field

        :param stream: the source stream, positioned after the opening ``{``
        :return: a ``(format ...)`` form for this field
        """

        norm = self.normal

        norm._skip_horizontal_whitespace(stream)
        position = stream.position()
        lead = stream.peek(1)

        if lead in '([{':
            event, pos, expr = norm._read(stream)
        elif lead and (lead.isalpha() or lead == '_' or lead.isdigit()
                       or lead == '-'):
            first = stream.read(1)
            terms = norm._terms
            for c in ':}':
                if c not in terms:
                    terms = terms + c
            expr = norm.process_atom(
                norm._collect_atom_continuation(stream, first, terms))
            pos = position
            event = VALUE
        else:
            event, pos, expr = norm._read(stream)

        if event is EOF:
            raise stream.reader_eof(pos)
        if event is not VALUE:
            raise stream.error("invalid format field", pos)

        norm._skip_horizontal_whitespace(stream)
        peek = stream.peek(1)

        if peek == ':':
            stream.read(1)
            spec = self._read_format_spec(stream)
            child = cons(_symbol_format, expr, spec, nil)

        elif peek == '}':
            stream.read(1)
            child = cons(_symbol_format, expr, nil)

        else:
            forms = [expr]
            while stream.peek(1) != '}':
                event, pos, val = norm._read(stream)
                if event is EOF:
                    raise stream.reader_eof(pos)
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


    def _read_raw_field(self, stream):
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
                raise stream.reader_eof(stream.position())
            if c == '\\':
                nxt = stream.read(1)
                if not nxt:
                    raise stream.reader_eof(stream.position())
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


    def _read_field(self, stream):
        """
        Read one ``{...}`` field: extract its raw text, decode, and parse

        :param stream: the source stream, positioned after the opening ``{``
        :return: a ``(format ...)`` form for this field
        """

        pos = stream.position()
        raw = self._read_raw_field(stream)
        substream = source_str(_as_unicode(raw) + '}', stream.filename,
                               line_no=pos[0], col_no=pos[1])
        return self._read_format_field(substream)


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
                    stream.read(1)
                    if stream.peek(1) == '{':
                        stream.read(1)
                        flush()
                        result.append('{')
                        continue
                    flush()
                    result.append(self._read_field(stream))
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

        except ReaderEOF:
            raise
        except ReaderSyntaxError as rex:
            raise FormatStringSyntaxError(rex.message, rex.location,
                                          filename=rex.filename,
                                          text=rex.text)

        flush()
        return result


default_reader = Reader()


# The end.
