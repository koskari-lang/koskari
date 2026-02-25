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

Koskari's s-expression parser with run-time modifiable reader macros.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

from codecs import decode
from contextlib import contextmanager
from decimal import Decimal as decimal
from fractions import Fraction as fraction
from functools import partial
from io import StringIO
from os.path import exists
from re import compile as regex, UNICODE, VERBOSE

from ..exceptions import KoskariSyntaxError, ReaderSyntaxError, FormatStringSyntaxError, ReaderSyntaxError, FormatStringSyntaxError
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


_symbol__str = symbol("#str")
_symbol_begin = symbol("begin")
_symbol_decimal = symbol("decimal")
_symbol_format = symbol("format")
_symbol_fraction = symbol("fraction")
_symbol_quasiquote = symbol("quasiquote")
_symbol_quote = symbol("quote")
_symbol_splice = symbol("splice")
_symbol_unquote = symbol("unquote")
_symbol_unquote_splicing = symbol("unquote-splicing")

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
    This allows parsing once at compile time rather than repeatedly at
    runtime.

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


class Atom(str):
    """
    A raw atom string that has not yet been processed
    """

    pass


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


def _is_block_opener(form):
    """
    Return True if form is a block opener per layout-syntax

    Block openers: standalone colon symbol, or keyword ending with colon
    """

    if is_keyword(form):
        return str(form).endswith(':')
    if is_symbol(form):
        return str(form) == ':'
    return False


class LayoutReader(Reader):
    """
    Reader that supports layout-based block syntax

    Subclasses Reader and adds indent-aware block reading. Resolves
    layout heads via namespace (layout/<head>). Requires a namespace
    for layout resolution, like the compiler.
    """

    def __init__(self, namespace, nodefaults=False):
        """
        Initialize the layout reader

        :param namespace: Module, Namespace, or dict for layout lookup.
            If Module, uses vars(module). If dict, wraps in ModuleNamespace.
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


    def _read(self, stream, raw=False):
        """
        Override to support break marker for block-header parsing
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
        Probe pass: parse with : as terminator, discard, find last colon

        Returns (recorded_str, last_colon_pos, start_pos). The stream is
        consumed past the header. If require_colon and no colon found, raises.
        """

        start_pos = stream.position()
        wrapped = RecordingStream(stream)

        def colon_macro(s, c):
            return VALUE, symbol(':')

        depth = 0
        last_colon_pos = None

        with self.temporary_event_macro(':', colon_macro, terminating=True):
            while True:
                peek = wrapped.peek(1)
                if not peek:
                    break

                if peek in '([{':
                    depth += 1

                event, pos, form = self._read(wrapped)
                if event is EOF:
                    break

                if peek in '([{':
                    depth -= 1

                if form == symbol(':') and depth == 0:
                    last_colon_pos = pos
                    wrapped.skip_whitespace()
                    break

                if form != symbol(':') and depth == 0 and not require_colon:
                    wrapped.skip_whitespace()
                    inner = getattr(wrapped, '_stream', wrapped)
                    if getattr(inner, '_dedent_detected', False):
                        break
                    next_c = wrapped.peek(1)
                    if next_c == '\n' or not next_c:
                        break

        if require_colon and last_colon_pos is None:
            raise stream.error(
                'block header must end with colon', start_pos)

        return wrapped.get_recorded(), last_colon_pos, start_pos


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

        Uses probe pass (parse with : terminator, discard) to find the
        final colon, then reads from a fresh stream until MARK. Both
        "unknown x:" and "unknown x :" parse identically.

        :param stream: the source stream
        :return: (head, args, opener) where head is first form, args
            are forms between head and opener, opener is the colon/keyword
        :raises ReaderSyntaxError: on EOF in header
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

        Each element is either a simple form or a sub-block. Uses
        IndentAwareStream to stop at dedent.

        :param stream: the source stream
        :param outer_indent: indent level that ends the block
        :return: list of body forms
        """

        if isinstance(stream, IndentAwareStream):
            wrapped = stream
        else:
            wrapped = IndentAwareStream(stream, max_indent=outer_indent)
        body = []

        while True:
            wrapped.skip_whitespace()
            if getattr(wrapped, '_dedent_detected', False):
                break

            c = wrapped.peek(1)
            if not c:
                break

            if c in '([{':
                event, pos, form = self._read(wrapped)
                if event is EOF:
                    break
                body.append(form)
            else:
                event, pos, form1 = self._read(wrapped)
                if event is EOF:
                    break
                header_indent = pos[1]
                wrapped.skip_whitespace()
                if getattr(wrapped, '_dedent_detected', False):
                    body.append(form1)
                    continue
                if not wrapped.peek(1):
                    body.append(form1)
                    continue

                header_start = wrapped.position()
                recording = RecordingStream(wrapped)
                recorded, last_colon_pos, _ = (
                    self._probe_block_header_end(
                        recording, require_colon=False))

                if last_colon_pos is not None:
                    start_lin, start_col = header_start
                    sub_header_stream = source_str(
                        recorded, wrapped.filename,
                        line_no=start_lin, col_no=start_col)
                    self._break_marker = last_colon_pos
                    try:
                        probe_forms = []
                        with self.temporary_event_macro(
                                ':', lambda s, c: (VALUE, symbol(':')),
                                terminating=True):
                            while True:
                                event, pos, value = self._read(
                                    sub_header_stream)
                                if event is MARK:
                                    break
                                if event is EOF:
                                    raise wrapped.error(
                                        'unexpected EOF in block header',
                                        pos)
                                probe_forms.append(value)
                    finally:
                        self._break_marker = None

                    head = form1
                    args = [f for f in probe_forms if f != symbol(':')]
                    block = self.read_block(
                        wrapped, header_indent, head, args)
                    body.append(block)
                else:
                    wrapped._buffer = list(recorded) + (
                        getattr(wrapped, '_buffer', []) or [])
                    wrapped._buffer_start_pos = header_start
                    body.append(form1)

        # print("read_block_body ->", body)
        return body


    def _read_block_source(self, stream, outer_indent, head, args):
        pos = stream.position()
        body = self.read_block_body(stream, outer_indent)
        header = cons(head, *args, nil)
        source = cons(header, *body, nil)
        if is_pair(source):
            source.set_position(pos)

        # print("_read_block_source ->", source)
        return source


    def _peek_declared_peer_head(self, stream, outer_indent):
        stream.skip_whitespace()
        if not getattr(stream, '_dedent_detected', False):
            return None

        buf = list(getattr(stream, '_buffer', []) or [])
        if not buf or buf[0] != '\n':
            return None

        i = 1
        indent = 0
        while i < len(buf) and buf[i] in ' 	':
            indent += 1
            i += 1

        if indent != outer_indent or i >= len(buf):
            return None

        prefix = ''.join(buf[i:])
        suffix = stream._stream.peek(256) or ''
        text = prefix + suffix

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


    def _read_declared_peers(self, stream, outer_indent, layout_obj):
        declared = tuple(getattr(layout_obj, 'peers', ()) or ())
        if not declared:
            return []

        declared_set = {str(sym) for sym in declared}
        peers = []

        while True:
            head = self._peek_declared_peer_head(stream, outer_indent)
            if head is None or str(head) not in declared_set:
                break

            stream.read(1)
            stream.read(outer_indent)
            phead, pargs, _opener = self.read_block_header(stream)
            peers.append(self._read_block_source(stream, outer_indent, phead, pargs))

        return peers


    def read_block(self, stream, outer_indent, head=None, args=None):
        """
        Read a block: header (if not provided) and body, resolve layout

        :param stream: the source stream
        :param outer_indent: indent of the block header line
        :param head: if provided, block header was already read
        :param args: if provided, header args were already read
        :return: layout object result (default blocks route through layout/block)
        """

        if head is None:
            head, args, opener = self.read_block_header(stream)

        wrapped = IndentAwareStream(stream, max_indent=outer_indent)
        source = self._read_block_source(wrapped, outer_indent, head, args)

        layout_obj = self.find_layout(head)
        if layout_obj is not None:
            peers = self._read_declared_peers(wrapped, outer_indent, layout_obj)
            self._layout_stream = wrapped
            try:
                return layout_obj.read(self, source, peers)
            finally:
                self._layout_stream = None

        header, body = source
        synthetic_header = cons(_layout_block, *list(unpack(header)), nil)
        default_source = cons(synthetic_header, body)
        if is_pair(default_source):
            default_source.set_position(source.get_position())

        layout_obj = self.find_layout(symbol('layout/block'))
        if layout_obj is None:
            raise stream.error('layout/block not found', stream.position())
        self._layout_stream = wrapped
        try:
            return layout_obj.read(self, default_source, [])
        finally:
            self._layout_stream = None


    def read_layout_form(self, stream):
        """
        Read one top-level layout form: s-expr or block

        At depth 0, detects blocks. Inside delimiters, uses base _read.
        """

        if self._layout_depth > 0:
            event, pos, value = self._read(stream)
            return event, pos, value

        stream.skip_whitespace()
        position = stream.position()
        c = stream.peek(1)
        if not c:
            return EOF, position, None

        if c in '([{':
            event, pos, value = self._read(stream)
            return event, pos, value

        head, args, opener = self.read_block_header(stream)
        outer_indent = position[1]
        block = self.read_block(stream, outer_indent, head, args)
        return VALUE, position, block


    def read(self, reader_stream):
        """
        Read a single layout form from the stream

        Delegates to read_layout_form at top level.
        """

        event, pos, value = self.read_layout_form(reader_stream)

        if event is VALUE:
            return value
        elif event is EOF:
            return None
        else:
            raise reader_stream.error('invalid syntax', pos)


    def _read_pair(self, closer, stream, char):
        """Override to track layout depth for s-expr mode"""

        self._layout_depth += 1
        try:
            return super()._read_pair(closer, stream, char)
        finally:
            self._layout_depth -= 1


class FormatStringReader(object):
    """
    Reader for parsing the interior of format strings.
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


def source_str(source_str, filename, auto_skip_exec=True,
               line_no=1, col_no=0):
    """
    Create a SourceStream from a string

    :param source_str: the source string
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :param line_no: starting line number
    :param col_no: starting column number
    :return: a SourceStream
    """

    return SourceStream(StringIO(source_str), filename,
                        auto_skip_exec=auto_skip_exec,
                        line_no=line_no, col_no=col_no)


def source_stream(source_stream, filename, auto_skip_exec=True):
    """
    Create a SourceStream from an existing stream

    :param source_stream: the stream to wrap
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :return: a SourceStream
    """

    return SourceStream(source_stream, filename,
                        auto_skip_exec=auto_skip_exec)


class RecordingStream(object):
    """
    Wraps a stream and records all characters read for replay

    Used by the block-header probe pass: record the header, then
    create a fresh stream from the recording for the real read pass.
    """

    def __init__(self, stream):
        self._stream = stream
        self._record = []
        self._putback = []


    def read(self, n=1):
        if self._putback:
            result = []
            while len(result) < n and self._putback:
                c = self._putback.pop(0)
                result.append(c)
                self._record.append(c)
            rest = n - len(result)
            if rest <= 0:
                return ''.join(result)[:n]
            data = self._stream.read(rest)
            if data:
                self._record.extend(data)
            return ''.join(result) + (data or '')
        data = self._stream.read(n)
        if data:
            self._record.extend(data)
        return data


    def peek(self, n=1):
        if self._putback:
            from_buf = min(n, len(self._putback))
            result = ''.join(self._putback[:from_buf])
            if from_buf >= n:
                return result
            return result + (self._stream.peek(n - from_buf) or '')
        return self._stream.peek(n)


    def position(self):
        return self._stream.position()


    @property
    def filename(self):
        return self._stream.filename


    def error(self, message, position=None):
        return self._stream.error(message, position)


    def skip_whitespace(self):
        while True:
            c = self._stream.peek(1)
            if not c or c not in ' \t\n\r':
                return
            c = self._stream.read(1)
            self._record.append(c)


    def read_until_chars(self, terminators):
        data = self._stream.read_until_chars(terminators)
        if data:
            self._record.extend(data)
        return data


    def get_recorded(self):
        """
        Return the string of all characters read since creation
        """
        return ''.join(self._record)


class IndentAwareStream(object):
    """
    Wraps a SourceStream to provide indent-bounded skip_whitespace

    When max_indent is set, skip_whitespace stops at a line whose indent
    is <= max_indent (dedent), leaving that line unconsumed for the next
    reader. Uses an internal buffer for pushback when stopping at dedent.
    """

    def __init__(self, stream, max_indent=None):
        """
        Initialize the indent-aware stream

        :param stream: the underlying SourceStream to wrap
        :param max_indent: when set, skip_whitespace stops at dedent
        """

        self._stream = stream
        self._max_indent = max_indent
        self._buffer = []
        self._buffer_start_pos = None


    def _read_from_underlying(self, n=1):
        return self._stream.read(n)


    def read(self, n=1):
        if self._buffer:
            result = []
            while len(result) < n and self._buffer:
                result.append(self._buffer.pop(0))
            if not self._buffer:
                self._buffer_start_pos = None
            rest = n - len(result)
            if rest > 0:
                result.append(self._read_from_underlying(rest))
            return ''.join(result) if result else self._read_from_underlying(n)
        return self._read_from_underlying(n)


    def peek(self, n=1):
        if self._buffer:
            from_buf = min(n, len(self._buffer))
            buf_part = ''.join(self._buffer[:from_buf])
            if from_buf >= n:
                return buf_part
            return buf_part + (self._stream.peek(n - from_buf) or '')
        return self._stream.peek(n)


    def position(self):
        if self._buffer and self._buffer_start_pos is not None:
            return self._buffer_start_pos
        return self._stream.position()


    @property
    def filename(self):
        return self._stream.filename


    def error(self, message, position=None):
        return self._stream.error(message, position)


    def skip_whitespace(self):
        """
        Skip whitespace, stopping at dedent when max_indent is set

        When max_indent is set and the next line would dedent (indent <=
        max_indent), does not consume the newline; puts it into the
        internal buffer and returns. Otherwise behaves like normal skip.
        """

        if self._max_indent is None:
            return self._stream.skip_whitespace()

        self._dedent_detected = False
        if self._buffer:
            if (len(self._buffer) > 1 and self._buffer[0] == '\n'):
                self._dedent_detected = True
                return ''
            if all(c in ' \t' for c in self._buffer):
                self._buffer = []
                self._buffer_start_pos = None
            elif len(self._buffer) == 1 and self._buffer[0] == '\n':
                next_indent = 0
                line_chars = []
                while True:
                    ch = self._read_from_underlying(1)
                    if not ch:
                        self._dedent_detected = True
                        return ''
                    if ch in ' \t':
                        next_indent += 1
                        line_chars.append(ch)
                    elif ch == '\n':
                        next_indent = 0
                        line_chars = []
                    else:
                        if next_indent <= self._max_indent:
                            self._buffer = ['\n'] + line_chars + [ch]
                            self._buffer_start_pos = self._stream.position()
                            self._dedent_detected = True
                            return ''
                        self._buffer = [ch]
                        self._buffer_start_pos = self._stream.position()
                        return ''
            else:
                return ''

        while True:
            pos = self._stream.position()
            c = self._read_from_underlying(1)

            if not c:
                break

            if c in ' \t':
                continue

            if c == '\n':
                next_indent = 0
                line_chars = []

                while True:
                    ch = self._read_from_underlying(1)
                    if not ch:
                        self._buffer = ['\n'] + line_chars
                        self._buffer_start_pos = pos
                        self._dedent_detected = True
                        return ''
                    if ch in ' \t':
                        next_indent += 1
                        line_chars.append(ch)
                    elif ch == '\n':
                        next_indent = 0
                        line_chars = []
                    else:
                        if next_indent <= self._max_indent:
                            putback = ['\n'] + line_chars + [ch]
                            self._buffer = putback
                            self._buffer_start_pos = pos
                            self._dedent_detected = True
                            return ''
                        self._buffer = [ch]
                        self._buffer_start_pos = self._stream.position()
                        return ''
                break

            putback = [c]
            self._buffer = putback
            self._buffer_start_pos = pos
            return ''

        return ''


    def read_until_chars(self, terminators):
        result = []
        while True:
            c = self.read(1)
            if not c:
                break
            if c in terminators:
                self._buffer.insert(0, c)
                if self._buffer_start_pos is None:
                    self._buffer_start_pos = self._stream.position()
                break
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


class SourceStream(BaseSourceStream):
    """
    A wrapper for a text stream that tracks line and column position

    Inherits from the native BaseSourceStream for efficient character-level
    I/O with position tracking. Adds error reporting and callback-based
    read_until for extensibility.
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

        if auto_skip_exec:
            self.skip_exec()


    def error(self, message, position=None):
        """
        Create a ReaderSyntaxError for the current position

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


    def skip_exec(self):
        """
        Skip a #! line at the start of the stream.
        """

        if self.peek(2) == "#!":
            self.readline()


default_reader = Reader()


# The end.
