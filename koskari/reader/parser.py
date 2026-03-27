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
2. SourceStream(BaseSourceStream) -- error reporting, pushback buffer
3. IndentAwareStream(SourceStream) -- indent-bounded skip_whitespace, dedented flag

The Reader reads any SourceStream. LayoutReader(Reader) adds layout-block
syntax and requires an IndentAwareStream for body reads. No reader method
inspects private attributes of streams it receives; the stream interface is
uniform and well-defined.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Sonnet 4.6 via Cursor
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

from ..exceptions import FormatStringSyntaxError, ReaderSyntaxError
from ..types import (BaseSourceStream, Module, cons, is_pair, is_symbol,
                     keyword, nil, setcdr, symbol, unpack)
from .layouts import LayoutHandler


DEDENT = keyword("dedent")

_layout_block = symbol("layout/block")

__all__ = (
    "ReaderSyntaxError",
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
)


class ReadEOFType(object):
    """
    Sentinel returned by Reader.read at end of stream.

    Distinct from the Python None literal, which may appear as a parsed value.
    """

    __slots__ = ()

    def __repr__(self):
        return '<read EOF>'


READ_EOF = ReadEOFType()


_symbol__str = symbol("compile/str")
_symbol_gensym_thunk = symbol("compile/gensym-thunk")
_symbol_decimal = symbol("decimal")
_symbol_format = symbol("format")
_symbol_fraction = symbol("fraction")
_symbol_quasiquote = symbol("compile/quasiquote")
_symbol_quasiquote_block = symbol("compile/quasiquote-block")
_symbol_attr = symbol("compile/attr")
_symbol_quote = symbol("compile/quote")
_symbol_ref = symbol("compile/ref")
_symbol_refq = symbol("compile/refq")
_symbol_splice = symbol("compile/splice")
_symbol_unquote = symbol("compile/unquote")
_symbol_unquote_splicing = symbol("compile/unquote-splicing")
_keyword_block = keyword(":block:")

IN_PROGRESS = keyword("work-in-progress")
VALUE = keyword("value")
ATOM = keyword("atom")
DOT = keyword("dot")
SKIP = keyword("skip")
CLOSE_PAIR = keyword("close-pair")
EOF = keyword("eof")
COLON = keyword("colon")

_integer_re = regex(r"^-?\d+$").match
_bin_re = regex(r"^0b[01]+$").match
_oct_re = regex(r"^0o[0-7]+$").match
_hex_re = regex(r"^0x[\da-f]+$").match
_float_re = regex(r"^-?("
                  r"(\d*\.\d+|\d+\.\d*)(e-?\d+)?f?"
                  r"|(\d+e-?\d+)f?"
                  r"|\d+f"
                  ")$").match
_decimal_re = regex(r"^-?((\d*\.\d+)|(\d+\.\d*)|\d+)d$").match
_fraction_re = regex(r"-?\d+/\d+").match
_complex_re = regex(r"^-?\d*\.?\d+\+\d*\.?\d*[ij]$").match
_keyword_re = regex(r"^(:.+|.+:)$").match
_dotted_attr_atom_re = regex(
    r"^[a-zA-Z_][a-zA-Z0-9_-]*(\.[a-zA-Z_][a-zA-Z0-9_-]*)+$").match
_ref_atom_re = regex(r"^&\S+$").match
_ellipsis_atom_re = regex(r"^\.\.\.$").match
_py_singleton_atom_re = regex(r"^(None|True|False)$").match


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
    Source stream with error reporting and a pushback buffer

    Inherits from the native BaseSourceStream for efficient character-level
    I/O with position tracking. Adds error reporting, a pushback buffer for
    speculative reads, and helper methods used by the reader.

    The pushback buffer allows the reader to consume characters speculatively
    then restore them via pushback(text, position) if the speculation fails.
    The buffer is drained before any read reaches the underlying stream, so
    position() reflects the buffer start while characters are buffered.
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

        self._buffer = []
        self._buffer_start_pos = None

        if auto_skip_exec:
            self.skip_exec()


    def read(self, n=1):
        """
        Read n characters, draining the pushback buffer first

        :param n: number of characters to read
        :return: a string of at most n characters
        """

        if self._buffer:
            result = []
            while len(result) < n and self._buffer:
                result.append(self._buffer.pop(0))
            if not self._buffer:
                self._buffer_start_pos = None
            rest = n - len(result)
            if rest > 0:
                data = super().read(rest)
                if data:
                    result.append(data)
            return ''.join(result)
        return super().read(n)


    def peek(self, n=1):
        """
        Peek at n characters without consuming them

        Composes from the pushback buffer and the underlying stream's peek.

        :param n: number of characters to peek at
        :return: a string of at most n characters
        """

        if self._buffer:
            from_buf = min(n, len(self._buffer))
            buf_part = ''.join(self._buffer[:from_buf])
            if from_buf >= n:
                return buf_part
            return buf_part + (super().peek(n - from_buf) or '')
        return super().peek(n)


    def position(self):
        """
        Return the current stream position as (line, col)

        When characters are buffered, returns the position at which
        the buffered text begins rather than the underlying stream's
        position (which has advanced past the buffered characters).

        :return: (line, col) tuple
        """

        if self._buffer and self._buffer_start_pos is not None:
            return self._buffer_start_pos
        return super().position()


    def read_until_chars(self, terminators):
        """
        Read characters until a terminating character is found

        The terminating character is not consumed. When the pushback buffer
        is empty, delegates to the C-native implementation which leaves the
        terminator unconsumed without any pushback. When the buffer is
        non-empty, drains non-terminating characters manually then delegates
        to C for the remainder.

        :param terminators: string of terminating characters
        :return: text read up to (not including) the terminator
        """

        if not self._buffer:
            return super().read_until_chars(terminators)

        result = []
        while self._buffer:
            c = self._buffer[0]
            if c in terminators:
                return ''.join(result)
            result.append(self._buffer.pop(0))
        if not self._buffer:
            self._buffer_start_pos = None

        rest = super().read_until_chars(terminators)
        if rest:
            result.append(rest)
        return ''.join(result)


    def pushback(self, text, position):
        """
        Push text back into the stream at the given position

        The pushed-back text will be returned by subsequent reads before
        any characters from the underlying stream. The position argument
        is recorded and returned by position() until the buffer drains.

        :param text: the text to push back
        :param position: the stream position corresponding to the start of text
        """

        if not text:
            return
        chars = list(text)
        self._buffer = chars + self._buffer
        self._buffer_start_pos = position


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


    def __iter__(self):
        """
        Iterate over characters in the stream
        """

        return iter(partial(self.read, 1), '')


    def skip_whitespace(self):
        """
        Skip whitespace characters, draining the pushback buffer first

        When the buffer is empty, delegates to the C-native implementation
        for maximum speed. When the buffer is non-empty, drains whitespace
        from the buffer manually (since the C implementation cannot see the
        Python-level buffer), then delegates to C for the remainder.

        :return: the whitespace string consumed
        """

        if not self._buffer:
            return super().skip_whitespace()

        consumed = []
        while self._buffer and self._buffer[0] in ' \t\n\r':
            consumed.append(self._buffer.pop(0))
        if not self._buffer:
            self._buffer_start_pos = None

        rest = super().skip_whitespace()
        if rest:
            consumed.append(rest)
        return ''.join(consumed)


    def skip_exec(self):
        """
        Skip a #! line at the start of the stream
        """

        if self.peek(2) == "#!":
            self.readline()


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

        Otherwise, peeks ahead with a single large-window read to obtain
        the lookahead string, then scans it as a plain Python string. Blank
        lines are skipped over. When the first non-blank, non-empty line is
        found, its indent is measured:

        - If indent <= max_indent: sets self.dedented = True and returns
          without consuming anything.

        - If indent > max_indent: consumes exactly the whitespace prefix
          scanned (the newline and indent spaces) via a single read(n),
          leaving the stream at the content character.

        The underlying stream is seekable so a single large peek is cheap.
        """

        if self._max_indent is None:
            self.dedented = False
            return super().skip_whitespace()

        self.dedented = False

        # When the C-level column is 0 and _buffer is empty, the stream is
        # at the start of a new line (a preceding \n was consumed without
        # going through the peek-ahead path below). Synthesise a virtual \n
        # by prepending one to the lookahead so the main scan's newline
        # branch fires immediately and measures the indent of the first
        # real content line. Track the synthetic offset so that the consume
        # count is adjusted to exclude it.
        if self.col == 0 and not self._buffer:
            ahead = '\n' + (self.peek(512) or '')
            synthetic = 1
        else:
            ahead = self.peek(512) or ''
            synthetic = 0

        i = 0

        while True:
            if i >= len(ahead):
                self.dedented = True
                return ''
            c = ahead[i]

            if c in ' \t':
                i += 1
                continue

            if c != '\n':
                consume = i - synthetic
                if consume > 0:
                    self.read(consume)
                return ''

            newline_start = i
            i += 1
            next_indent = 0

            while True:
                if i >= len(ahead):
                    self.dedented = True
                    return ''
                nc = ahead[i]
                if nc in ' \t':
                    next_indent += 1
                    i += 1
                elif nc == '\n':
                    i += 1
                    next_indent = 0
                else:
                    if next_indent <= self._max_indent:
                        self.dedented = True
                        return ''
                    consume = newline_start + 1 + next_indent - synthetic
                    if consume > 0:
                        self.read(consume)
                    return ''


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


class Reader(object):
    """
    S-expression reader with configurable reader macros and atom patterns

    The reader parses textual input into Koskari data structures. It
    supports reader macros for special character handling and atom
    patterns for converting atomic tokens into typed values.
    """

    def __init__(self, nodefaults=False):
        """
        Initialize a new reader

        :param nodefaults: if True, do not add default macros and patterns
        """

        self.reader_macros = {}
        self.atom_patterns = []
        self.terminating = ["\n", "\r", "\t", " "]
        self._terms = "".join(self.terminating)

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


    def _read(self, stream, raw=False):
        """
        Internal read implementation

        :param stream: the source stream
        :param raw: if True, return atoms without processing
        :return: tuple of (event, position, value)
        """

        while True:
            stream.skip_whitespace()

            position = stream.position()
            c = stream.read()
            if not c:
                return EOF, position, None

            macro = self.reader_macros.get(c, self._read_default)

            try:
                event, value = macro(stream, c)
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


    def set_event_macro(self, char, macro_fn, terminating=False):
        """
        Add a character event macro to the reader

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, char) -> (event, value)
        :param terminating: if True, this character terminates atoms
        """

        for c in char:
            self.reader_macros[c] = macro_fn
            if terminating:
                self.terminating.append(c)

        if terminating:
            self._terms = "".join(self.terminating)


    def get_event_macro(self, char):
        """
        Get a character event macro from the reader

        :param char: the character to look up
        :return: tuple of (macro_fn, terminating) or None
        """

        try:
            return (self.reader_macros[char], char in self.terminating)
        except KeyError:
            return None


    def clear_event_macro(self, char):
        """
        Remove a character event macro from the reader

        :param char: the character to remove
        """

        if char in self.reader_macros:
            self.terminating.remove(char)
            self._terms = "".join(self.terminating)
            del self.reader_macros[char]


    @contextmanager
    def temporary_event_macro(self, char, macro_fn, terminating=False):
        """
        Context manager to temporarily apply a character event macro

        :param char: the character to trigger on
        :param macro_fn: function(stream, char) -> (event, value)
        :param terminating: if True, this character terminates atoms
        :yields: self
        """

        old = self.get_event_macro(char)
        self.set_event_macro(char, macro_fn, terminating)

        yield self

        if old is None:
            self.clear_event_macro(char)
        else:
            self.set_event_macro(char, *old)


    def set_macro_character(self, char, macro_fn, terminating=False):
        """
        Add a reader macro to the parser

        Unlike set_event_macro, this wraps the macro function to always
        return a VALUE event.

        :param char: the character to trigger on
        :param macro_fn: function(stream, char) -> value
        :param terminating: if True, this character terminates atoms
        """

        def macro_adapter(stream, char):
            return VALUE, macro_fn(stream, char)

        self.set_event_macro(char, macro_adapter, terminating)


    def temporary_macro_character(self, char, macro_fn, terminating=False):
        """
        Context manager to temporarily apply a reader macro

        :param char: the character to trigger on
        :param macro_fn: function(stream, char) -> value
        :param terminating: if True, this character terminates atoms
        :return: context manager
        """

        def macro_adapter(stream, char):
            return VALUE, macro_fn(stream, char)

        return self.temporary_event_macro(char, macro_adapter, terminating)


    def _add_default_macros(self):
        """
        Add the default reader macros for standard syntax
        """

        sm = self.set_event_macro

        sm('(', partial(self._read_pair, ')'), True)
        sm(')', self._close_pair, True)

        sm('[', partial(self._read_pair, ']'), True)
        sm(']', self._close_pair, True)

        sm('{', partial(self._read_thunk, '}'), True)
        sm('}', self._close_pair, True)

        sm('"', self._read_string, True)
        sm("'", self._read_quote, True)
        sm("`", self._read_quasiquote, True)
        sm(';', self._read_comment, True)

        sm('f', self._read_fstring, False)
        sm('r', self._read_rstring, False)

        sm('#', self._read_collection, False)


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


    def _read_default(self, stream, c):
        """
        Default character macro handler for unrecognized characters

        Reads characters until a terminating character is found.

        :param stream: the source stream
        :param c: the initial character
        :return: tuple of (event, value)
        """

        atom = c + stream.read_until_chars(self._terms)

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

        :param closer: the expected closing character
        :param stream: the source stream
        :param char: the opening character
        :return: tuple of (event, value)
        """

        result = nil
        work = result

        while True:
            event, position, value = self._read(stream)

            if event is CLOSE_PAIR:
                break

            elif event is DOT:
                if result is nil:
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
                raise stream.error("unexpected EOF")

            elif result is nil:
                result = cons(value, nil)
                result.set_position(position)
                work = result

            else:
                new_work = cons(value, nil)
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
            return self._read_default(stream, fchar)

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


    def _read_fstring(self, stream, fchar):
        """
        Read a format string (f"...")

        :param stream: the source stream
        :param fchar: the 'f' character
        :return: tuple of (event, value)
        """

        if stream.peek(1) != '"':
            return self._read_default(stream, fchar)

        char = stream.read(1)
        pos = stream.position()
        lin, col = pos

        if stream.peek(3) == '"""':
            col += 2

        _evnt, data = self._read_string(stream, char)

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
            return self._read_default(stream, char)
        else:
            return self._read_string(stream, stream.read(1), False)


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
            raise stream.error("Unexpected EOF")

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
            raise stream.error("Unexpected EOF")

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

        if event is not VALUE:
            msg = "invalid use of %s" % char
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

        if event is not VALUE:
            msg = "invalid use of %s" % char
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

        if event is not VALUE:
            msg = "invalid use of %s" % char
            raise stream.error(msg, pos)

        if is_pair(child) and child[0] is _symbol_splice:
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

        if event is not VALUE:
            msg = "invalid use of %s" % char
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
# Layout block header helpers
# ===========================================================================


class HeaderCommitDetector:
    """
    Decide whether the current header head should commit to header parsing.

    Commit rule:
    - After consuming the head atom, if the next non-horizontal-whitespace
      character is:
        - `;` (comment start) => do NOT commit
        - `\\n` or `\\r` or EOF => do NOT commit
      then commit only if there is any other token character present.
    """

    def __init__(self, *, hot_peek=2, fallback_peek=2048):
        self._hot_peek = hot_peek
        self._fallback_peek = fallback_peek

    def should_commit(self, stream):
        ahead = stream.peek(self._hot_peek) or ''
        i = 0
        while i < len(ahead) and ahead[i] in ' \t':
            i += 1

        if i >= len(ahead):
            # Undecidable from the hot peek alone; inspect a larger window.
            ahead = stream.peek(self._fallback_peek) or ''
            i = 0
            while i < len(ahead) and ahead[i] in ' \t':
                i += 1

            if i >= len(ahead):
                return False
            ch = ahead[i]
        else:
            ch = ahead[i]

        if ch == ';' or ch in '\n\r':
            return False
        return True


class LogicalLineScanner:
    """
    Minimal logical-line boundary helper for header-mode logic.

    A newline ends the logical line only at delimiter depth 0.
    """

    def __init__(self, reader):
        self._reader = reader

    def at_logical_eol(self, stream):
        if self._reader._layout_depth != 0:
            return False
        nxt = stream.peek(1)
        if not nxt:
            return True
        return nxt in '\n\r'


# ===========================================================================
# LayoutReader
# ===========================================================================


class LayoutReader(Reader):
    """
    Reader that supports layout-based block syntax

    Subclasses Reader and adds indent-aware block reading. Resolves
    layout heads via namespace (layout/<head>). Requires a namespace
    for layout resolution, like the compiler.

    Block headers use contextual structural `:` terminators: `:` ends the
    header only when followed by whitespace and then comment, EOL, or EOF,
    so keyword atoms like `as:` can appear in header arguments at delimiter
    depth 0. When a top-level head commits to a block header, a missing
    structural trailing `:` is a syntax error rather than a silent fallback.

    Stream interface contract: LayoutReader expects its stream arguments
    to be SourceStream instances (for pushback support). Body reads always
    use an IndentAwareStream so that dedent detection is available through
    the stream.dedented attribute rather than getattr guards.
    """

    def __init__(self, namespace, nodefaults=False,
                 layout_enabled=True, layout_implicit_calls=True,
                 layout_dot_escape=True):
        """
        Initialize the layout reader

        :param namespace: Module, Namespace, or dict for layout lookup
        :param nodefaults: if True, do not add default macros and patterns
        :param layout_enabled: if False, disable layout/block parsing and
            parse using base s-expression semantics
        :param layout_implicit_calls: if True, symbol-headed non-block
            lines lower to implicit calls
        :param layout_dot_escape: if True, `. expr` is enabled as a strict
            raw-expression escape for layout lines
        """

        super().__init__(nodefaults=nodefaults)
        from ..types import ModuleNamespace

        if isinstance(namespace, Module):
            self._ns = vars(namespace)
        elif isinstance(namespace, dict):
            self._ns = ModuleNamespace('layout', namespace)
        else:
            self._ns = namespace

        self._layout_depth = 0
        self._block_mode = False
        self._block_macros = {}
        self.layout_enabled = layout_enabled
        self.layout_implicit_calls = layout_implicit_calls
        self.layout_dot_escape = layout_dot_escape

        if not nodefaults:
            self._add_default_block_macros()

        # Reader does not install a depth-0 `:` macro; structural `:` is
        # emitted only while parsing headers (see _header_mode / _read_default).
        self._header_scan = False
        self._commit_detector = HeaderCommitDetector()
        self._line_scanner = LogicalLineScanner(self)


    def set_layout_modes(self, *, layout_enabled=None, layout_implicit_calls=None,
                         layout_dot_escape=None):
        """
        Update layout-mode feature switches on this reader.

        Intended as the stable hook for future pragma-driven reader reload.
        Any None parameter leaves the current setting unchanged.
        """

        if layout_enabled is not None:
            self.layout_enabled = bool(layout_enabled)
        if layout_implicit_calls is not None:
            self.layout_implicit_calls = bool(layout_implicit_calls)
        if layout_dot_escape is not None:
            self.layout_dot_escape = bool(layout_dot_escape)


    def reload_pragma(self, pragma):
        """
        Apply pragma-controlled reader switches when present.

        This is intentionally permissive: unknown pragma objects are ignored,
        while known attributes are mapped to the layout mode switches.
        """

        if pragma is None:
            return

        self.set_layout_modes(
            layout_enabled=getattr(pragma, 'layout_enabled', None),
            layout_implicit_calls=getattr(pragma, 'layout_implicit_calls', None),
            layout_dot_escape=getattr(pragma, 'layout_dot_escape', None))


    def _add_default_block_macros(self):
        self._block_macros.update({
            "'": self._read_quote_block,
            "`": self._read_quasiquote_block,
        })


    @contextmanager
    def _header_mode(self):
        """
        Enable header terminator handling for `:` within atom scans.
        """
        old = self._header_scan
        self._header_scan = True
        try:
            yield
        finally:
            self._header_scan = old


    def _read_default(self, stream, c):
        """
        Atom scanning override to support maybe-interrupting `:`.

        In header scan mode at delimiter depth 0:
        - structural `:` emits COLON and is not part of the atom
        - non-structural `:` becomes part of the atom (for keyword atoms)
        """
        if self._header_scan and self._layout_depth == 0:
            if c == ':' and self._is_structural_colon_after_consumed(stream):
                return COLON, symbol(':')

            terms_with_colon = self._terms + ':'
            atom_parts = [c]
            while True:
                chunk = stream.read_until_chars(terms_with_colon)
                if chunk:
                    atom_parts.append(chunk)

                if stream.peek(1) != ':':
                    break

                if self._is_structural_colon_at_next(stream):
                    break

                # Non-structural ':' becomes part of this atom.
                stream.read(1)
                atom_parts.append(':')

            atom = ''.join(atom_parts)
            if atom == '.':
                return DOT, None
            return ATOM, atom

        return super()._read_default(stream, c)


    def _is_structural_colon_in_ahead(self, ahead, colon_index):
        """
        Apply the structural rule to a ':' inside `ahead`.
        """
        j = colon_index + 1
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'


    def _is_structural_colon_after_consumed(self, stream):
        """
        Decide whether a header-terminator `:` is structural.

        Assumes the `:` has already been consumed and the stream is positioned
        right after it.
        """
        hot = stream.peek(2) or ''
        i = 0
        while i < len(hot) and hot[i] in ' \t':
            i += 1

        if i < len(hot):
            ch = hot[i]
            return ch == ';' or ch in '\n\r'

        # Undecidable from the hot peek window; fall back to a larger peek.
        ahead = stream.peek(2048) or ''
        j = 0
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'


    def _is_structural_colon_at_next(self, stream):
        """
        Decide whether the next `:` is a structural header terminator.

        Assumes the current stream position begins with `:`.
        """
        hot = stream.peek(2) or ''
        if not hot or hot[0] != ':':
            return False

        i = 1
        while i < len(hot) and hot[i] in ' \t':
            i += 1
        if i < len(hot):
            ch = hot[i]
            return ch == ';' or ch in '\n\r'

        # Undecidable from the hot peek window; fall back.
        ahead = stream.peek(2048) or ''
        if not ahead or ahead[0] != ':':
            return False
        j = 1
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'


    def find_layout(self, name):
        """
        Resolve layout/<head> in namespace and builtins

        :param name: symbol for the block head (e.g. if, let)
        :return: layout object with .read, or None
        """

        name = str(name)
        if not name.startswith('layout/'):
            name = 'layout/' + name

        try:
            r = self._ns.ref(name)
        except (KeyError, AttributeError, TypeError):
            return None

        if r is None or r.empty:
            return None

        v = r.deref()
        if isinstance(v, LayoutHandler):
            return v
        return None


    def _has_block_colon_ahead(self, stream):
        """
        Probe ahead for a structural header-terminator ':'.

        Scans a limited window and tracks delimiter depth so ':' within
        parentheses/brackets/braces does not count.

        :param stream: the source stream
        :return: True if a structural block-opener colon exists ahead
        """

        ahead = stream.peek(2048) or ''
        depth = 0
        for i, ch in enumerate(ahead):
            if depth == 0 and ch in '\n\r;':
                return False
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == ':' and depth == 0:
                if self._is_structural_colon_in_ahead(ahead, i):
                    return True
        return False


    def _read_header_args(self, stream):
        """
        Read header argument forms until a structural COLON or end-of-line.
        """

        args = []
        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return args, False

            peek = stream.peek(1)
            if not peek or peek in '\n\r':
                return args, False
            if peek == ';':
                return args, False

            with self._header_mode():
                event, pos, value = super()._read(stream)
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
            raise stream.error('unexpected EOF in block header', pos)

        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', pos)

        return head, args, symbol(':')


    def read_header_until_terminator(self, stream, *, head_pos, head):
        """
        Read header argument forms until a structural header terminator ':'.

        Assumes the stream is positioned right after the already-consumed
        header head atom.
        """
        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', head_pos)
        return args


    def _skip_horizontal_whitespace(self, stream):
        """
        Consume horizontal whitespace only (space and tab)
        """

        consumed = []
        while True:
            c = stream.peek(1)
            if c not in (' ', '\t'):
                break
            consumed.append(stream.read(1))
        return ''.join(consumed)


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

            event, pos, value = self._read(stream)
            if event is EOF:
                return forms
            if event is DOT:
                raise stream.error('invalid use of dot in layout line', pos)
            if event is not VALUE:
                raise stream.error('invalid syntax', pos)

            forms.append(value)


    def _require_logical_eol_after_single_expr(self, stream, pos):
        """
        Require that only optional spaces/tab/comment remain on this line
        """

        self._skip_horizontal_whitespace(stream)
        c = stream.peek(1)
        if not c or c in '\n\r;':
            return
        raise stream.error('dot escape accepts exactly one expression', pos)


    def _read_dot_escape_expr(self, stream, position):
        """
        Read `. expr` and enforce exactly one expression on the line
        """

        self._skip_horizontal_whitespace(stream)
        c = stream.peek(1)
        if not c or c in '\n\r;':
            raise stream.error('dot escape requires one expression', position)

        event, pos, value = self._read(stream)
        if event is EOF:
            raise stream.error('dot escape requires one expression', position)
        if event is DOT:
            raise stream.error('dot escape requires one expression', position)
        if event is not VALUE:
            raise stream.error('invalid syntax', pos)

        self._require_logical_eol_after_single_expr(stream, position)
        return value


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

        old_indent = stream._max_indent
        stream._max_indent = outer_indent
        body = []

        try:
            while True:
                stream.skip_whitespace()
                if stream.dedented:
                    break
                if not stream.peek(1):
                    break
                event, pos, form = self.read_form(stream)
                if event is EOF:
                    break
                body.append(form)
        finally:
            stream._max_indent = old_indent

        return body


    def _read_block_source(self, stream, outer_indent, head, args):
        """
        Build the raw block source structure: ((head . args) . body...)

        :param stream: source stream positioned after the block header colon
        :param outer_indent: column of the block head line
        :param head: the block head symbol
        :param args: list of forms between head and colon
        :return: cons((head . args) . body...) with position set
        """

        pos = stream.position()
        body = self.read_block_body(stream, outer_indent)
        header = cons(head, *args, nil)
        source = cons(header, *body, nil)
        if is_pair(source):
            source.set_position(pos)
        return source


    def _peek_peer_block_head(self, stream, outer_indent):
        """
        Non-consumingly check whether the next line starts a declared peer

        Temporarily installs outer_indent as the stream's max_indent so that
        skip_whitespace correctly detects the dedent at the peer line's column.
        If a dedent is detected, peeks far enough ahead to read the next
        line's leading token.

        :param stream: IndentAwareStream positioned at end of current block
        :param outer_indent: column of the enclosing block head
        :return: symbol or None
        """

        old_indent = stream._max_indent
        stream._max_indent = outer_indent
        try:
            stream.skip_whitespace()
            if not stream.dedented:
                return None
        finally:
            stream._max_indent = old_indent

        ahead = stream.peek(outer_indent + 257) or ''
        if not ahead or ahead[0] != '\n':
            return None

        indent = 0
        i = 1
        while i < len(ahead) and ahead[i] in ' \t':
            indent += 1
            i += 1

        if indent != outer_indent or i >= len(ahead):
            return None

        text = ahead[i:]
        token = []
        for ch in text:
            if ch in ' \t\r\n':
                break
            if ch in '()[]{}':
                break
            token.append(ch)

        if not token:
            return None

        head = ''.join(token)
        if head.endswith(':'):
            head = head[:-1]

        if not head:
            return None

        return symbol(head)


    def _collect_peer_blocks(self, stream, outer_indent, layout_obj):
        """
        Read any declared peer blocks following the primary block

        :param stream: IndentAwareStream after the primary block body
        :param outer_indent: column of the enclosing block head
        :param layout_obj: LayoutHandler whose peers attribute lists allowed
            peer head symbols
        :return: list of raw source structures, one per peer block
        """

        declared = tuple(layout_obj.peers or ())
        if not declared:
            return []

        declared_set = {str(sym) for sym in declared}
        peers = []

        while True:
            head = self._peek_peer_block_head(stream, outer_indent)
            if head is None or str(head) not in declared_set:
                break

            stream.read(1)
            stream.read(outer_indent)
            phead, pargs, _opener = self.read_block_header(stream)
            peers.append(self._read_block_source(stream, outer_indent, phead, pargs))

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

        Sets _block_mode so that sub-block detection routes through
        read_block instead of read_layout_block at every nesting level.

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


    def _read_quote_block(self, stream, outer_indent):
        """
        Block-macro handler for the ' prefix

        Reads the block header and body, then wraps the raw :block: cons
        in (compile/quote ...).

        :param stream: the source stream, positioned after the ' character
        :param outer_indent: column of the block head line
        :return: (compile/quote (:block: ...)) cons
        """

        head, args, _ = self.read_block_header(stream)
        raw = self.read_block(stream, outer_indent, head, args)
        return cons(_symbol_quote, raw, nil)


    def _read_quasiquote_block(self, stream, outer_indent):
        """
        Block-macro handler for the backtick prefix

        Installs the , and ,@ unquote reader macros for the duration of
        the header and body read so that unquote sites are recognised.
        Wraps the result in (compile/quasiquote-block ...).

        :param stream: the source stream, positioned after the ` character
        :param outer_indent: column of the block head line
        :return: (compile/quasiquote-block (:block: ...)) cons
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            head, args, _ = self.read_block_header(stream)
            raw = self.read_block(stream, outer_indent, head, args)
        return cons(_symbol_quasiquote_block, raw, nil)


    def _apply_layout(self, stream, outer_indent, source):
        """
        Apply a layout handler to an already-read block source cons

        :param stream: IndentAwareStream positioned after the primary block body
        :param outer_indent: column of the block head line
        :param source: cons of the form ((head . args) . body-forms)
        :return: the expanded form produced by the layout handler
        """

        header = source[0]
        head = header[0]

        layout_obj = self.find_layout(head)
        if layout_obj is not None:
            peers = self._collect_peer_blocks(stream, outer_indent, layout_obj)
            return layout_obj.read(source, peers)

        body = source[1]
        synthetic_header = cons(_layout_block, *list(unpack(header)), nil)
        default_source = cons(synthetic_header, body)
        if is_pair(default_source):
            default_source.set_position(source.get_position())

        layout_obj = self.find_layout(symbol('layout/block'))
        if layout_obj is None:
            raise stream.error('layout/block not found', stream.position())
        return layout_obj.read(default_source, [])


    def read_layout_block(self, stream, outer_indent, head=None, args=None):
        """
        Read a block and apply its layout handler, producing a layout form

        :param stream: the source stream, positioned after the header colon
            and its trailing whitespace
        :param outer_indent: column of the block head line
        :param head: block head symbol if already read by the caller
        :param args: header argument forms if already read by the caller
        :return: the transformed form produced by the layout handler
        """

        if head is None:
            head, args, opener = self.read_block_header(stream)

        source = self._read_block_source(stream, outer_indent, head, args)
        return self._apply_layout(stream, outer_indent, source)



    def read_form(self, stream):
        """
        Read one layout form: s-expr, prefixed raw block, or normal block

        At layout depth > 0 (inside delimiters), delegates to the base _read.

        At depth 0:
        1. c in '([{' -- parenthesised s-expression, read directly via _read.
        2. c in _block_macros -- quote/quasiquote raw block when a structural
           header colon exists ahead; otherwise fall through to _read.
        3. Bare atom -- read under header atom rules; if the head commits as
           a block header, read args until structural `:` or raise on missing
           terminator.

        :param stream: source stream positioned at the start of the next form
        :return: (event, position, value)
        """

        if self._layout_depth > 0 or not self.layout_enabled:
            return self._read(stream)

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

            # Tolerate CRLF by consuming a stray leading '\r'.
            if c == '\r':
                stream.read(1)
                continue

            if c in '([{':
                return self._read(stream)

            outer_indent = position[1]

            if c in self._block_macros:
                next_c = stream.peek(2)[1:2]
                if next_c and next_c not in '([{' and self._has_block_colon_ahead(stream):
                    stream.read(1)
                    block_macro = self._block_macros[c]
                    result = block_macro(stream, outer_indent)
                    if is_pair(result):
                        result.set_position(position)
                    return VALUE, position, result

                return super()._read(stream)

            with self._header_mode():
                event, pos, form1 = super()._read(stream)
            if event is COLON:
                raise stream.error('block header must have a head', pos)
            if event is EOF:
                return EOF, pos, None
            if event is DOT:
                if self.layout_dot_escape:
                    value = self._read_dot_escape_expr(stream, pos)
                    return VALUE, pos, value
                raise stream.error('invalid syntax', pos)

            header_indent = pos[1]

            if (is_symbol(form1) and self._commit_detector.should_commit(stream)
                    and self._has_block_colon_ahead(stream)):
                args = self.read_header_until_terminator(
                    stream, head_pos=pos, head=form1)

                if self._block_mode:
                    block = self.read_block(stream, header_indent, form1, args)
                else:
                    block = self.read_layout_block(stream, header_indent, form1, args)
                return VALUE, position, block

            if is_symbol(form1) and self.layout_implicit_calls:
                tail = self._read_line_tail_forms(stream)
                return VALUE, pos, cons(form1, *tail, nil)

            return VALUE, pos, form1


    def read(self, reader_stream):
        """
        Read a single layout form from the stream

        Wraps reader_stream in an IndentAwareStream (with no max_indent
        constraint) if it is not already one, then delegates to
        read_form. This ensures stream.dedented is always available
        throughout the layout read without any getattr guards.

        At EOF returns READ_EOF (same as Reader.read), not None, so callers
        can distinguish end-of-stream from a parsed Python None literal.
        """

        if not isinstance(reader_stream, IndentAwareStream):
            reader_stream = IndentAwareStream(
                reader_stream.stream, reader_stream.filename,
                auto_skip_exec=False,
                line_no=reader_stream.lin,
                col_no=reader_stream.col,
                max_indent=None)

        event, pos, value = self.read_form(reader_stream)

        if event is VALUE:
            return value
        elif event is EOF:
            return READ_EOF
        else:
            raise reader_stream.error('invalid syntax', pos)


    def _read_pair(self, closer, stream, char):
        """
        Override to track layout depth for s-expr mode

        Removes ':' from the terminating set while inside a delimiter so that
        trailing-colon keywords (e.g. as:) can form normally in s-expr mode.
        Restores it on exit.
        """

        self._layout_depth += 1
        colon_was_terminating = ':' in self.terminating
        if colon_was_terminating:
            self.terminating.remove(':')
            self._terms = "".join(self.terminating)
        try:
            return super()._read_pair(closer, stream, char)
        finally:
            self._layout_depth -= 1
            if colon_was_terminating:
                self.terminating.append(':')
                self._terms = "".join(self.terminating)


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


    def split(self, stream):
        """
        Split a format string into literal and expression parts

        :param stream: the source stream containing the format string
        :return: list of string literals and expression forms
        """

        result = []

        norm = self.normal

        try:
            peek = stream.peek(2)
            while peek:
                if peek == "{{":
                    stream.read(2)
                    result.append("{")

                elif peek == "}}":
                    stream.read(2)
                    result.append("}")

                elif peek == "}":
                    raise FormatStringSyntaxError(
                        "mis-matched closing } in f-string",
                        stream.position(),
                        filename=stream.filename)

                elif peek[0] == "{":
                    stream.read(1)
                    event, child = norm._read_pair("}", stream, "{")
                    new_child = cons(_symbol_format, child)
                    new_child.set_position(child.get_position())
                    result.append(new_child)

                else:
                    result.append(stream.read_until_chars("{}"))

                peek = stream.peek(2)

        except ReaderSyntaxError as rex:
            raise FormatStringSyntaxError(rex.message, rex.location,
                                          filename=rex.filename,
                                          text=rex.text)

        return result


default_reader = Reader()


# The end.
