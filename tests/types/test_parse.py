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
tests.types.test_parse

Unit tests for koskari.parse

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

from io import StringIO
from unittest import TestCase

from koskari.types import cons, symbol, keyword, nil, car, cdr, BaseSourceStream
from koskari.parse import default_reader, source_str


def parse_source(src_str):
    reader = default_reader
    stream = source_str(src_str, "<unittest>")
    return reader.read(stream)


class TestAtomics(TestCase):

    def test_nothing(self):
        src = ""
        col = parse_source(src)
        self.assertIs(col, None)

    def test_symbol(self):
        src = "lambda"
        col = parse_source(src)
        self.assertIs(col, symbol("lambda"))

        src = "Number123"
        col = parse_source(src)
        self.assertIs(col, symbol("Number123"))

        src = "None"
        col = parse_source(src)
        self.assertIs(col, symbol("None"))

        src = "True"
        col = parse_source(src)
        self.assertIs(col, symbol("True"))

        src = "False"
        col = parse_source(src)
        self.assertIs(col, symbol("False"))

        src = "nil"
        col = parse_source(src)
        self.assertIs(col, symbol("nil"))

        src = "..."
        col = parse_source(src)
        self.assertIs(col, symbol("..."))

        src = "taco:bell"
        col = parse_source(src)
        self.assertIs(col, symbol("taco:bell"))

    def test_keyword(self):
        src = ":x"
        col = parse_source(src)
        self.assertIs(col, keyword("x"))

        src = "x:"
        col = parse_source(src)
        self.assertIs(col, keyword("x"))

        src = ":x:"
        col = parse_source(src)
        self.assertIs(col, keyword("x"))

        src = ":number-1"
        col = parse_source(src)
        self.assertIs(col, keyword("number-1"))

        src = "number-1:"
        col = parse_source(src)
        self.assertIs(col, keyword("number-1"))

    def test_integer(self):
        src = "123"
        col = parse_source(src)
        self.assertEqual(col, 123)

        src = "-9"
        col = parse_source(src)
        self.assertEqual(col, -9)

    def test_hex(self):
        src = "0xfe"
        col = parse_source(src)
        self.assertEqual(col, 0xfe)

    def test_octal(self):
        src = "0o74"
        col = parse_source(src)
        self.assertEqual(col, 0o74)

    def test_binary(self):
        src = "0b101"
        col = parse_source(src)
        self.assertEqual(col, 0b101)


class TestFloats(TestCase):

    def test_simple_float(self):
        src = "1.5"
        col = parse_source(src)
        self.assertEqual(col, 1.5)

    def test_trailing_dot(self):
        src = "1."
        col = parse_source(src)
        self.assertEqual(col, 1.0)

    def test_leading_dot(self):
        src = ".5"
        col = parse_source(src)
        self.assertEqual(col, 0.5)

    def test_scientific_notation(self):
        src = "1.5e2"
        col = parse_source(src)
        self.assertEqual(col, 1.5e2)

        src = ".5e2"
        col = parse_source(src)
        self.assertEqual(col, 0.5e2)

        src = "5e-1"
        col = parse_source(src)
        self.assertEqual(col, 5e-1)

    def test_float_suffix(self):
        src = "1.5f"
        col = parse_source(src)
        self.assertEqual(col, 1.5)

        src = "1.f"
        col = parse_source(src)
        self.assertEqual(col, 1.0)

        src = ".5f"
        col = parse_source(src)
        self.assertEqual(col, 0.5)


class TestComplex(TestCase):

    def test_complex_i(self):
        src = "1+5i"
        col = parse_source(src)
        self.assertEqual(col, complex("1+5j"))

    def test_complex_j(self):
        src = "1+5j"
        col = parse_source(src)
        self.assertEqual(col, complex("1+5j"))


class TestFraction(TestCase):

    def test_invalid_fraction(self):
        src = "1/2/3"
        self.assertRaises(SyntaxError, parse_source, src)

    def test_simple_fraction(self):
        src = "1/2"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("fraction"), 1, 2, nil))

    def test_negative_fraction(self):
        src = "-1/2"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("fraction"), -1, 2, nil))


class TestDecimal(TestCase):

    def test_decimal_with_fraction(self):
        src = "1.5d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (1, 5), -1), nil))

    def test_leading_dot_decimal(self):
        src = ".5d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (5,), -1), nil))

    def test_trailing_dot_decimal(self):
        src = "1.d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (1,), 0), nil))


class TestStrings(TestCase):

    def test_empty_string(self):
        src = '""'
        col = parse_source(src)
        self.assertEqual(col, "")

        src = ' "" '
        col = parse_source(src)
        self.assertEqual(col, "")

    def test_escaped_quote(self):
        src = r' "\"" '
        col = parse_source(src)
        self.assertEqual(col, "\"")

    def test_simple_string(self):
        src = '"hello world"'
        col = parse_source(src)
        self.assertEqual(col, "hello world")

    def test_escape_sequences(self):
        src = r' "hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, "hello\n \tworld")

        src = ' "hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, "hello\n \tworld")

    def test_unicode(self):
        src = '"hello"'
        col = parse_source(src)
        self.assertEqual(col, "hello")


class TestTripleStrings(TestCase):

    def test_empty_3string(self):
        src = r'""""""'
        col = parse_source(src)
        self.assertEqual(col, "")

    def test_3string_with_space(self):
        src = r' """ """ '
        col = parse_source(src)
        self.assertEqual(col, " ")

    def test_3string_simple(self):
        src = r'"""hello world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello world')

    def test_3string_with_quotes(self):
        src = r'"""hello "favorite" world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello "favorite" world')

    def test_3string_escaped_quotes(self):
        src = r'"""hello \""" "\"" ""\" world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello """ """ """ world')

    def test_3string_trailing_quote(self):
        src = r'"""hello "world\""""'
        col = parse_source(src)
        self.assertEqual(col, 'hello "world"')


class TestRawStrings(TestCase):

    def test_empty_raw(self):
        src = r'r""'
        col = parse_source(src)
        self.assertEqual(col, "")

    def test_raw_with_escapes(self):
        src = r' r"\"" '
        col = parse_source(src)
        self.assertEqual(col, r"\"")

        src = r' r"hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, r"hello\n \tworld")


class TestQuoted(TestCase):

    def test_quote_symbol(self):
        src = """
        'foo
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("quote"), symbol("foo"), nil))

    def test_quote_list(self):
        src = """
        '(foo bar)
        """
        col = parse_source(src)
        expected = cons(symbol("quote"),
                        cons(symbol("foo"), symbol("bar"), nil),
                        nil)
        self.assertEqual(col, expected)

    def test_quasiquote(self):
        src = """
        `bar
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("quasiquote"), symbol("bar"), nil))

    def test_unquote_splicing(self):
        src = """
        `(,@foo)
        """
        col = parse_source(src)
        exp = cons(symbol("quasiquote"),
                   cons(cons(symbol("unquote-splicing"),
                             symbol("foo"),
                             nil),
                        nil),
                   nil)
        self.assertEqual(col, exp)


class TestLists(TestCase):

    def test_simple_list(self):
        src = "(testing a thing)"
        col = parse_source(src)
        exp = cons(symbol("testing"),
                   symbol("a"),
                   symbol("thing"),
                   nil)
        self.assertEqual(col, exp)

    def test_bracket_list(self):
        src = "[testing a thing]"
        col = parse_source(src)
        exp = cons(symbol("testing"),
                   symbol("a"),
                   symbol("thing"),
                   nil)
        self.assertEqual(col, exp)

    def test_mismatched_delimiters(self):
        with self.assertRaises(SyntaxError):
            parse_source("(no way]")

        with self.assertRaises(SyntaxError):
            parse_source("[no way)")

        with self.assertRaises(SyntaxError):
            parse_source("{no way]")

        with self.assertRaises(SyntaxError):
            parse_source("[no way}")

        with self.assertRaises(SyntaxError):
            parse_source("{no way)")

        with self.assertRaises(SyntaxError):
            parse_source("(no way}")


class TestConsPairs(TestCase):

    def test_implicit_begin(self):
        src = "{testing a thing}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("begin"),
                                   symbol("testing"),
                                   symbol("a"),
                                   symbol("thing"),
                                   nil))

    def test_dot(self):
        src = "(testing . 123)"
        col = parse_source(src)
        exp = cons(symbol("testing"), 123)
        self.assertEqual(col, exp)

    def test_newline(self):
        src = """
        ( this is
        a test )
        """
        col = parse_source(src)
        exp = cons(symbol("this"),
                   symbol("is"),
                   symbol("a"),
                   symbol("test"),
                   nil)
        self.assertEqual(col, exp)

    def test_comments(self):
        src = """
        ; Let's check out the comments
        ( this is ; well it's something
        a test ) ; this ought to work
        """
        col = parse_source(src)
        exp = cons(symbol("this"),
                   symbol("is"),
                   symbol("a"),
                   symbol("test"),
                   nil)
        self.assertEqual(col, exp)

    def test_multi(self):
        src = """
        1.0 "2" (3)
        """
        strm = source_str(src, "<unittest>")
        read = default_reader.read

        a = read(strm)
        b = read(strm)
        c = read(strm)

        self.assertEqual(a, 1.0)
        self.assertEqual(b, "2")
        self.assertEqual(c, cons(3, nil))


class TestPositions(TestCase):

    def test_position_1(self):
        src = """
        (hello world)
        """
        strm = source_str(src, "<unittest>")
        expr = default_reader.read(strm)

        self.assertEqual(expr.get_position(), (2, 8))
        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (2, 15))

    def test_position_2(self):
        src = """
        (hello (world)
         how are you)
        """
        strm = source_str(src, "<unittest>")
        expr = default_reader.read(strm)

        self.assertEqual(expr.get_position(), (2, 8))
        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (2, 15))

        wld = car(expr)
        self.assertEqual(expr.get_position(), (2, 15))

        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (3, 9))

        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (3, 13))

        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (3, 17))


class TestCollections(TestCase):

    def test_tuple(self):
        src = "#(1 2 3)"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#tuple"), 1, 2, 3, nil))

    def test_list(self):
        src = "#[1 2 3]"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#list"), 1, 2, 3, nil))

    def test_dict(self):
        src = "#{a 1 b 2}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#dict"),
                                   symbol("a"), 1,
                                   symbol("b"), 2,
                                   nil))


class TestReader(TestCase):

    def test_read_atom(self):
        from koskari.parse import Reader, Atom

        reader = Reader()
        stream = source_str("hello", "<unittest>")
        result = reader.read_atom(stream)
        self.assertIsInstance(result, Atom)
        self.assertEqual(result, "hello")

    def test_read_and_position(self):
        from koskari.parse import Reader

        reader = Reader()
        stream = source_str("  hello", "<unittest>")
        value, pos = reader.read_and_position(stream)
        self.assertIs(value, symbol("hello"))
        self.assertEqual(pos, (1, 2))


class TestBaseSourceStreamCreate(TestCase):

    def test_create_with_stream_only(self):
        s = BaseSourceStream(StringIO("test"))
        self.assertIsNotNone(s)

    def test_create_with_filename(self):
        s = BaseSourceStream(StringIO("test"), "test.txt")
        self.assertEqual(s.filename, "test.txt")

    def test_create_with_position(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", line=5, col=10)
        self.assertEqual(s.position(), (5, 10))

    def test_default_position(self):
        s = BaseSourceStream(StringIO("test"), "test.txt")
        self.assertEqual(s.position(), (1, 0))


class TestBaseSourceStreamRead(TestCase):

    def test_read_single(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ch = s.read(1)
        self.assertEqual(ch, "h")

    def test_read_multiple(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read(3)
        self.assertEqual(data, "hel")

    def test_read_updates_column(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        s.read(3)
        self.assertEqual(s.position(), (1, 3))

    def test_read_newline_updates_line(self):
        s = BaseSourceStream(StringIO("ab\ncd"), "test.txt")
        s.read(3)
        self.assertEqual(s.position(), (2, 0))

    def test_read_past_newline(self):
        s = BaseSourceStream(StringIO("ab\ncd"), "test.txt")
        s.read(4)
        self.assertEqual(s.position(), (2, 1))

    def test_read_at_eof(self):
        s = BaseSourceStream(StringIO("hi"), "test.txt")
        s.read(2)
        data = s.read(1)
        self.assertEqual(data, "")


class TestBaseSourceStreamPeek(TestCase):

    def test_peek_single(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ch = s.peek(1)
        self.assertEqual(ch, "h")

    def test_peek_multiple(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.peek(3)
        self.assertEqual(data, "hel")

    def test_peek_does_not_advance(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        s.peek(3)
        self.assertEqual(s.position(), (1, 0))

    def test_peek_then_read(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        peeked = s.peek(2)
        read = s.read(2)
        self.assertEqual(peeked, read)


class TestBaseSourceStreamReadUntilChars(TestCase):

    def test_read_until_space(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        data = s.read_until_chars(" ")
        self.assertEqual(data, "hello")

    def test_read_until_newline(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        data = s.read_until_chars("\n")
        self.assertEqual(data, "hello")

    def test_read_until_multiple_terminators(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        data = s.read_until_chars(" \n\t")
        self.assertEqual(data, "hello")

    def test_read_until_no_match(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read_until_chars("x")
        self.assertEqual(data, "hello")

    def test_read_until_updates_position(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until_chars(" ")
        self.assertEqual(s.position(), (1, 5))

    def test_terminator_not_consumed(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until_chars(" ")
        ch = s.read(1)
        self.assertEqual(ch, " ")


class TestBaseSourceStreamSkipWhitespace(TestCase):

    def test_skip_spaces(self):
        s = BaseSourceStream(StringIO("   hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "   ")

    def test_skip_tabs(self):
        s = BaseSourceStream(StringIO("\t\thello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "\t\t")

    def test_skip_newlines(self):
        s = BaseSourceStream(StringIO("\n\nhello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "\n\n")

    def test_skip_mixed_whitespace(self):
        s = BaseSourceStream(StringIO(" \t\n hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, " \t\n ")

    def test_skip_no_whitespace(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "")

    def test_skip_updates_position(self):
        s = BaseSourceStream(StringIO("\n  hello"), "test.txt")
        s.skip_whitespace()
        self.assertEqual(s.position(), (2, 2))


class TestBaseSourceStreamReadline(TestCase):

    def test_readline_simple(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "hello\n")

    def test_readline_updates_position(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        s.readline()
        self.assertEqual(s.position(), (2, 0))

    def test_readline_at_eof(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "hello")

    def test_readline_empty_line(self):
        s = BaseSourceStream(StringIO("\nhello"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "\n")


class TestBaseSourceStreamProperties(TestCase):

    def test_stream_property(self):
        stream = StringIO("test")
        s = BaseSourceStream(stream, "test.txt")
        self.assertIs(s.stream, stream)

    def test_filename_property(self):
        s = BaseSourceStream(StringIO("test"), "myfile.txt")
        self.assertEqual(s.filename, "myfile.txt")

    def test_lin_property(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", line=5)
        self.assertEqual(s.lin, 5)

    def test_col_property(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", col=10)
        self.assertEqual(s.col, 10)


class TestBaseSourceStreamSubclass(TestCase):

    def test_can_subclass(self):
        class MyStream(BaseSourceStream):
            pass

        s = MyStream(StringIO("test"), "test.txt")
        self.assertIsInstance(s, BaseSourceStream)


class TestSourceStreamInheritance(TestCase):

    def test_inherits_from_base(self):
        s = source_str("test", "<unittest>")
        self.assertIsInstance(s, BaseSourceStream)

    def test_type_is_source_stream(self):
        from koskari.parse import SourceStream
        s = source_str("test", "<unittest>")
        self.assertIsInstance(s, SourceStream)

    def test_read_method_from_native(self):
        s = source_str("hello", "<unittest>")
        data = s.read(3)
        self.assertEqual(data, "hel")

    def test_peek_method_from_native(self):
        s = source_str("hello", "<unittest>")
        data = s.peek(3)
        self.assertEqual(data, "hel")
        # peek should not advance
        self.assertEqual(s.position(), (1, 0))

    def test_position_tracking(self):
        s = source_str("ab\ncd", "<unittest>")
        s.read(3)  # reads "ab\n"
        self.assertEqual(s.position(), (2, 0))

    def test_read_until_method(self):
        s = source_str("hello world", "<unittest>")
        data = s.read_until(lambda c: c == ' ')
        self.assertEqual(data, "hello")

    def test_skip_whitespace_native(self):
        from koskari.parse import SourceStream
        s = SourceStream(StringIO("  hello"), "test.txt", auto_skip_exec=False)
        # SourceStream uses native skip_whitespace from BaseSourceStream
        ws = s.skip_whitespace()
        self.assertEqual(ws, "  ")
        self.assertEqual(s.position(), (1, 2))


# The end.
