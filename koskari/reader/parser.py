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
from re import compile as regex, UNICODE, VERBOSE

from ..exceptions import KoskariSyntaxError, ReaderSyntaxError, FormatStringSyntaxError
from ..types import (
    BaseSourceStream,
    Module,
    car,
    cdr,
    cons,
    is_pair,
    is_keyword,
    is_symbol,
    keyword,
    nil,
    setcdr,
    symbol,
    unpack,
)

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
)


_symbol__str = symbol("compile/str")
_symbol_begin = symbol("compile/begin")
_symbol_decimal = symbol("decimal")
_symbol_format = symbol("format")
_symbol_fraction = symbol("fraction")
_symbol_quasiquote = symbol("compile/quasiquote")
_symbol_quasiquote_block = symbol("compile/quasiquote-block")
_symbol_quote = symbol("compile/quote")
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
MARK = keyword("mark")

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

_as_integer = int
_as_bin = partial(int, base=2)
_as_oct = partial(int, base=8)
_as_hex = partial(int, base=16)


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
|| \\u....
|| \\x..
|| \\[0-7]{1,3}
|| \\N\{[^}]+\}
|| \\[\\'"abfnrtv]
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

    def __init__(self, stream, filename=None, auto_skip_exec=True,
                 line_no=1, col_no=0):
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

        The terminating character is not consumed; it is pushed back
        into the buffer so that the next read or position() call sees
        it at its correct position.

        :param terminators: string of terminating characters
        :return: text read up to (not including) the terminator
        """

        result = []
        while True:
            pos = self.position()
            c = self.read(1)
            if not c:
                break
            if c in terminators:
                self.pushback(c, pos)
                break
            result.append(c)
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

        Otherwise, scans ahead using peek(i+1) without consuming any
        characters. Blank lines are skipped over. When the first non-blank,
        non-empty line is found, its indent is measured:

        - If indent <= max_indent: sets self.dedented = True and returns
          without consuming anything.

        - If indent > max_indent: consumes the whitespace-only characters
          scanned (the newline and indent spaces), leaving the stream at
          the content character.

        The underlying stream is seekable so peek(n) for large n is cheap.
        """

        if self._max_indent is None:
            self.dedented = False
            return super().skip_whitespace()

        self.dedented = False

        consume = 0
        i = 0

        while True:
            ahead = self.peek(i + 1)
            if not ahead or len(ahead) <= i:
                self.dedented = True
                return ''
            c = ahead[i]

            if c in ' \t':
                consume += 1
                i += 1
                continue

            if c != '\n':
                for _ in range(consume):
                    self.read(1)
                return ''

            newline_start = i
            i += 1
            next_indent = 0

            while True:
                ahead = self.peek(i + 1)
                if not ahead or len(ahead) <= i:
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
                    for _ in range(newline_start + 1 + next_indent):
                        self.read(1)
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

    return SourceStream(StringIO(src), filename,
                        auto_skip_exec=auto_skip_exec,
                        line_no=line_no, col_no=col_no)


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

        Returns a cons cell, symbol, or numeric value. Returns None if
        no data remains in the stream.

        :param reader_stream: the source stream to read from
        :return: the parsed value, or None at EOF
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream)

        if event is VALUE:
            return value
        elif event is EOF:
            return None
        else:
            raise reader_stream.error("invalid syntax", pos)


    def read_atom(self, reader_stream):
        """
        Read a single s-expression, returning atoms as unprocessed strings

        Returns a cons cell, or an unprocessed atomic value

        :param reader_stream: the source stream to read from
        :return: the parsed value, or None at EOF
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream, raw=True)

        if event is VALUE:
            return value
        elif event is ATOM:
            return Atom(value)
        elif event is EOF:
            return None
        else:
            raise reader_stream.error("invalid syntax", pos)


    def read_and_position(self, reader_stream):
        """
        Read a single s-expression and return its position

        :param reader_stream: the source stream to read from
        :return: a tuple of (value, position) where position is (line, col)
        :raises ReaderSyntaxError: on syntax errors
        """

        event, pos, value = self._read(reader_stream)

        if event is VALUE:
            return value, pos
        elif event is EOF:
            return None, pos
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

        sm('{', partial(self._read_begin, '}'), True)
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

        ap(symbol("keyword"), _keyword_re, keyword)
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


    def _read_begin(self, closer, stream, char):
        """
        Read a begin block (curly braces)

        :param closer: the expected closing character
        :param stream: the source stream
        :param char: the opening character
        :return: tuple of (event, value)
        """

        event, result = self._read_pair(closer, stream, char)

        if event is VALUE:
            new_result = cons(_symbol_begin, result)
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
# _ProbeStream -- recording wrapper for speculative header probes
# ===========================================================================


class _ProbeStream(object):
    """
    Thin recording wrapper used during block-header probe passes

    Wraps a SourceStream and records every character consumed via read()
    or read_until_chars(). get_recorded() returns the full text consumed
    since the probe began.

    This is used exclusively inside _probe_block_header_end. On failure
    the recorded text is pushed back onto the original stream via pushback().
    On success the recorded text is replayed through a fresh SourceStream
    for the real parse pass.

    Only the methods actually called by the reader during a probe pass are
    implemented here: read, peek, position, filename, error, skip_whitespace,
    read_until_chars, and readline.
    """

    def __init__(self, stream):
        self._stream = stream
        self._record = []


    def read(self, n=1):
        data = self._stream.read(n)
        if data:
            self._record.extend(data)
        return data


    def peek(self, n=1):
        return self._stream.peek(n)


    def position(self):
        return self._stream.position()


    @property
    def filename(self):
        return self._stream.filename


    def error(self, message, position=None):
        return self._stream.error(message, position)


    def skip_whitespace(self):
        # Skip and record whitespace one character at a time without consuming
        # past non-whitespace. Delegates peek to the underlying stream so that
        # indent-aware streams still see the correct lookahead.
        while True:
            c = self._stream.peek(1)
            if not c or c not in ' \t\n\r':
                return ''
            c = self._stream.read(1)
            self._record.append(c)
        return ''


    def read_until_chars(self, terminators):
        # Implement character-by-character so every read goes through
        # self.read(), which records into _record.
        result = []
        while True:
            c = self._stream.peek(1)
            if not c or c in terminators:
                break
            c = self.read(1)
            result.append(c)
        return ''.join(result)


    def readline(self):
        result = []
        while True:
            c = self.read(1)
            if not c:
                break
            result.append(c)
            if c == '\n':
                break
        return ''.join(result)


    def get_recorded(self):
        """
        Return all characters consumed since this probe stream was created
        """

        return ''.join(self._record)


# ===========================================================================
# LayoutReader
# ===========================================================================


class LayoutReader(Reader):
    """
    Reader that supports layout-based block syntax

    Subclasses Reader and adds indent-aware block reading. Resolves
    layout heads via namespace (layout/<head>). Requires a namespace
    for layout resolution, like the compiler.

    Stream interface contract: LayoutReader expects its stream arguments
    to be SourceStream instances (for pushback support). Body reads always
    use an IndentAwareStream so that dedent detection is available through
    the stream.dedented attribute rather than getattr guards.
    """

    def __init__(self, namespace, nodefaults=False):
        """
        Initialize the layout reader

        :param namespace: Module, Namespace, or dict for layout lookup
        :param nodefaults: if True, do not add default macros and patterns
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
        self._break_marker = None
        self._block_mode = False
        self._block_macros = {
            "'": self._read_quote_block,
            "`": self._read_quasiquote_block,
        }


    def _read(self, stream, raw=False):
        """
        Override to support break marker for block-header parsing

        When _break_marker is set, checks the current stream position before
        each form read. If the position has reached or passed the marker,
        returns a synthetic MARK event so that read_block_header stops
        exactly at the colon without accidentally consuming it.
        """

        if self._break_marker is not None:
            stream.skip_whitespace()
            pos = stream.position()
            if pos >= self._break_marker:
                self._break_marker = None
                return MARK, pos, None

        return super()._read(stream, raw)


    def _probe_block_header_end(self, stream, require_colon=True):
        """
        Probe pass: read the block header to locate the final colon

        Wraps the stream in a _ProbeStream so every character consumed is
        recorded. Installs ':' as a terminating reader macro. Reads forms
        until the colon (at depth 0) or end-of-header, recording text.

        On failure (no colon, require_colon=False), all consumed characters
        are restored to the original stream via pushback so the stream is
        left exactly as it was before the probe.

        Returns (recorded_str, last_colon_pos, start_pos):
        - recorded_str: every character consumed during the probe
        - last_colon_pos: stream position of the colon token, or None
        - start_pos: stream position at the start of the probe

        When require_colon=True and no colon is found, raises ReaderSyntaxError.
        """

        start_pos = stream.position()
        probe = _ProbeStream(stream)

        def colon_macro(s, c):
            return VALUE, symbol(':')

        depth = 0
        last_colon_pos = None

        with self.temporary_event_macro(':', colon_macro, terminating=True):
            while True:
                peek = probe.peek(1)
                if not peek:
                    break

                if peek in '([{':
                    depth += 1

                event, pos, form = self._read(probe)
                if event is EOF:
                    break

                if peek in '([{':
                    depth -= 1

                if form == symbol(':') and depth == 0:
                    last_colon_pos = pos
                    # Consume trailing whitespace (up to the newline) into
                    # the recording so the replay stream covers the full
                    # header text.
                    probe.skip_whitespace()
                    break

                if form != symbol(':') and depth == 0 and not require_colon:
                    # Speculatively: after reading a non-colon form at the
                    # top level, skip whitespace and check whether the next
                    # character is a newline or EOF. If so, there is no colon
                    # on this header line and we exit without finding one.
                    probe.skip_whitespace()
                    inner = probe._stream
                    if isinstance(inner, IndentAwareStream) and inner.dedented:
                        break
                    next_c = probe.peek(1)
                    if next_c == '\n' or not next_c:
                        break

        recorded_str = probe.get_recorded()

        if require_colon and last_colon_pos is None:
            raise stream.error(
                'block header must end with colon', start_pos)

        if last_colon_pos is None and not require_colon:
            # Restore everything consumed back to the original stream.
            if recorded_str:
                stream.pushback(recorded_str, start_pos)
            return recorded_str, None, start_pos

        return recorded_str, last_colon_pos, start_pos


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


    def read_block_header(self, stream):
        """
        Read forms until block opener; handles s-exprs spanning lines

        Two-pass approach: first probe to find the exact colon position, then
        replay the recorded text through a fresh stream for the real parse.

        :param stream: the source stream positioned at the first header token
        :return: (head, args, opener) where head is the first form, args are
            the remaining forms before the colon, opener is symbol(':')
        :raises ReaderSyntaxError: on EOF or missing colon
        """

        recorded, last_colon_pos, start_pos = self._probe_block_header_end(
            stream)
        start_lin, start_col = start_pos

        header_stream = source_str(
            recorded, stream.filename,
            line_no=start_lin, col_no=start_col)

        self._break_marker = last_colon_pos

        def colon_macro(s, c):
            return VALUE, symbol(':')

        forms = []
        try:
            with self.temporary_event_macro(':', colon_macro, terminating=True):
                while True:
                    event, pos, value = self._read(header_stream)
                    if event is MARK:
                        break
                    if event is EOF:
                        raise stream.error(
                            'unexpected EOF in block header', pos)
                    forms.append(value)
        finally:
            self._break_marker = None

        if len(forms) < 1:
            raise stream.error(
                'block header must have head and opener', pos)

        head = forms[0]
        args = list(forms[1:])
        opener = symbol(':')
        return (head, args, opener)


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


    def _read_quote_block(self, stream, outer_indent,
            recorded, header_start, last_colon_pos):
        """
        Block-macro handler for the ' prefix

        Replays the probed header text to extract head and args, reads
        the block as a raw :block: cons, and wraps it in
        (compile/quote ...).

        :param stream: the source stream, positioned after the header colon
        :param outer_indent: column of the block head line
        :param recorded: text string captured during the probe pass
        :param header_start: position where the recorded text begins
        :param last_colon_pos: position of the colon token in the recording
        :return: (compile/quote (:block: ...)) cons
        """

        forms = self._replay_header_forms(
            recorded, stream, header_start, last_colon_pos)
        if not forms:
            raise stream.error(
                'block header must have head and opener', header_start)
        head = forms[0]
        args = [f for f in forms[1:] if f != symbol(':')]
        raw = self.read_block(stream, outer_indent, head, args)
        return cons(_symbol_quote, raw, nil)


    def _read_quasiquote_block(self, stream, outer_indent,
            recorded, header_start, last_colon_pos):
        """
        Block-macro handler for the backtick prefix

        Installs the , and ,@ unquote reader macros before replaying the
        probed header text, so unquote sites in the block head arguments are
        recognised. The macros remain active for the full raw block read so
        that body forms and nested sub-block headers are also parsed with
        unquote support. Wraps the result in (compile/quasiquote-block ...).

        :param stream: the source stream, positioned after the header colon
        :param outer_indent: column of the block head line
        :param recorded: text string captured during the probe pass
        :param header_start: position where the recorded text begins
        :param last_colon_pos: position of the colon token in the recording
        :return: (compile/quasiquote-block (:block: ...)) cons
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            forms = self._replay_header_forms(
                recorded, stream, header_start, last_colon_pos)
            if not forms:
                raise stream.error(
                    'block header must have head and opener', header_start)
            head = forms[0]
            args = [f for f in forms[1:] if f != symbol(':')]
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


    def _replay_header_forms(self, recorded, stream, header_start,
                             last_colon_pos):
        """
        Replay recorded header text and extract forms up to the colon marker

        Creates a fresh SourceStream from the recorded text at the correct
        starting position, sets _break_marker to last_colon_pos, and reads
        all forms until the MARK event (colon reached) or EOF.

        :param recorded: the text string consumed during the probe pass
        :param stream: the live stream (used only for error reporting)
        :param header_start: position where the recorded text begins
        :param last_colon_pos: position of the colon token in the recording
        :return: list of forms read before the colon
        """

        start_lin, start_col = header_start
        sub_stream = source_str(
            recorded, stream.filename,
            line_no=start_lin, col_no=start_col)

        self._break_marker = last_colon_pos
        forms = []
        try:
            with self.temporary_event_macro(
                    ':', lambda s, c: (VALUE, symbol(':')),
                    terminating=True):
                while True:
                    ev, p, val = self._read(sub_stream)
                    if ev is MARK:
                        break
                    if ev is EOF:
                        raise stream.error(
                            'unexpected EOF in block header', p)
                    forms.append(val)
        finally:
            self._break_marker = None

        return forms


    def read_form(self, stream):
        """
        Read one layout form: s-expr, prefixed raw block, or normal block

        At layout depth > 0 (inside delimiters), delegates to the base _read.

        At depth 0, four cases:

        1. c in '([{' -- parenthesised s-expression, read directly via _read.

        2. c in _block_macros -- a prefix character that may introduce a raw
           block (e.g. ' for compile/quote-block). Consumes the prefix, peeks
           at the next character. If the next char starts an atom, probes for
           a colon. If found, wraps the raw block with the registered wrapper
           function. If not found, pushes the prefix back and lets _read handle
           everything as a normal form.

        3. Bare atom whose trailing colon was consumed as a keyword -- the
           colon is already past; dispatch directly to read_block.

        4. Bare atom followed by more forms -- probe speculatively for a colon.
           If found, read as a block. If not, restore probed chars and return
           the atom as a plain form.

        :param stream: source stream positioned at the start of the next form
        :return: (event, position, value)
        """

        if self._layout_depth > 0:
            return self._read(stream)

        stream.skip_whitespace()
        if stream.dedented:
            return EOF, stream.position(), None

        position = stream.position()
        c = stream.peek(1)
        if not c:
            return EOF, position, None

        if c in '([{':
            return self._read(stream)

        outer_indent = position[1]

        # Case 2: block-macro prefix character (e.g. ')
        # Consume the prefix, look at the following character:
        # - next char is a paren/missing: restore prefix, fall to _read
        # - next char starts an atom: probe for a colon
        #   - colon found: read as raw block, wrap with the registered fn
        #   - colon not found: restore prefix + probed chars, fall to _read
        if c in self._block_macros:
            prefix_pos = position
            stream.read(1)
            next_c = stream.peek(1)

            if next_c and next_c not in '([{':
                header_start = stream.position()
                recorded, last_colon_pos, _ = self._probe_block_header_end(
                    stream, require_colon=False)

                if last_colon_pos is not None:
                    block_macro = self._block_macros[c]
                    result = block_macro(
                        stream, outer_indent,
                        recorded, header_start, last_colon_pos)
                    if is_pair(result):
                        result.set_position(position)
                    return VALUE, position, result

                # No colon found; stream already restored by probe.
                # Also restore the prefix character itself.
                stream.pushback(c, prefix_pos)
                return self._read(stream)

            else:
                # Next char is a paren or EOF: restore prefix, fall to _read.
                stream.pushback(c, prefix_pos)
                return self._read(stream)

        # Cases 3 and 4: bare atom
        # Read the first token. If it came out as a keyword, the colon was
        # already consumed by the atom reader (e.g. 'begin:' -> keyword('begin')).
        # Otherwise probe the rest of the line for a colon.
        event, pos, form1 = self._read(stream)
        if event is EOF:
            return EOF, pos, None

        header_indent = pos[1]

        if is_keyword(form1):
            # Case 3: trailing colon already consumed.
            stream.skip_whitespace()
            head = symbol(str(form1))
            if self._block_mode:
                block = self.read_block(stream, header_indent, head, [])
            else:
                block = self.read_layout_block(stream, header_indent, head, [])
            return VALUE, position, block

        # Case 4: check whether the rest of the line has a colon.
        stream.skip_whitespace()
        if stream.dedented:
            return VALUE, pos, form1
        if not stream.peek(1):
            return VALUE, pos, form1

        header_start = stream.position()
        recorded, last_colon_pos, _ = self._probe_block_header_end(
            stream, require_colon=False)

        if last_colon_pos is not None:
            forms = self._replay_header_forms(
                recorded, stream, header_start, last_colon_pos)

            head = form1
            args = [f for f in forms if f != symbol(':')]
            if self._block_mode:
                block = self.read_block(stream, header_indent, head, args)
            else:
                block = self.read_layout_block(stream, header_indent, head, args)
            return VALUE, position, block

        # No colon: probe restored the stream. Return form1 as a plain form.
        return VALUE, pos, form1


    def read(self, reader_stream):
        """
        Read a single layout form from the stream

        Wraps reader_stream in an IndentAwareStream (with no max_indent
        constraint) if it is not already one, then delegates to
        read_form. This ensures stream.dedented is always available
        throughout the layout read without any getattr guards.
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
            return None
        else:
            raise reader_stream.error('invalid syntax', pos)


    def _read_pair(self, closer, stream, char):
        """
        Override to track layout depth for s-expr mode
        """

        self._layout_depth += 1
        try:
            return super()._read_pair(closer, stream, char)
        finally:
            self._layout_depth -= 1


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
