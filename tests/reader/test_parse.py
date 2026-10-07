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
tests.test_parse

Unit tests for koskari.parse

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from io import StringIO
from unittest import IsolatedAsyncioTestCase, TestCase

import pytest

from koskari.exceptions import ReaderSyntaxError
from koskari.lib import BaseSourceStream, car, cdr, cons, keyword, nil, symbol
from koskari.reader import READ_EOF, default_reader, source_str


pytestmark = pytest.mark.phase_infra


def parse_source(src_str):
    reader = default_reader
    stream = source_str(src_str, "<unittest>")
    return reader.read(stream)


class TestAtomics(IsolatedAsyncioTestCase):

    async def test_nothing(self):
        src = ""
        col = parse_source(src)
        self.assertIs(col, READ_EOF)

    async def test_symbol(self):
        src = "lambda"
        col = parse_source(src)
        self.assertIs(col, symbol("lambda"))

        src = "Number123"
        col = parse_source(src)
        self.assertIs(col, symbol("Number123"))

        src = "nil"
        col = parse_source(src)
        self.assertIs(col, symbol("nil"))

        src = "taco:bell"
        col = parse_source(src)
        self.assertIs(col, symbol("taco:bell"))

    async def test_keyword(self):
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

    async def test_ellipsis(self):
        src = "..."
        col = parse_source(src)
        self.assertIs(col, ...)

        src = "(list ...)"
        col = parse_source(src)
        self.assertIs(car(col), symbol("list"))
        self.assertIs(car(cdr(col)), ...)
        self.assertIs(cdr(cdr(col)), nil)

    async def test_py_singleton_literals(self):
        src = "None"
        col = parse_source(src)
        self.assertIs(col, None)

        src = "True"
        col = parse_source(src)
        self.assertIs(col, True)

        src = "False"
        col = parse_source(src)
        self.assertIs(col, False)

        src = "(list None True False)"
        col = parse_source(src)
        self.assertIs(car(col), symbol("list"))
        rest = cdr(col)
        self.assertIs(car(rest), None)
        rest = cdr(rest)
        self.assertIs(car(rest), True)
        rest = cdr(rest)
        self.assertIs(car(rest), False)
        self.assertIs(cdr(rest), nil)

    async def test_integer(self):
        src = "123"
        col = parse_source(src)
        self.assertEqual(col, 123)

        src = "-9"
        col = parse_source(src)
        self.assertEqual(col, -9)

    async def test_hex(self):
        src = "0xfe"
        col = parse_source(src)
        self.assertEqual(col, 0xfe)

    async def test_octal(self):
        src = "0o74"
        col = parse_source(src)
        self.assertEqual(col, 0o74)

    async def test_binary(self):
        src = "0b101"
        col = parse_source(src)
        self.assertEqual(col, 0b101)


class TestFloats(IsolatedAsyncioTestCase):

    async def test_simple_float(self):
        src = "1.5"
        col = parse_source(src)
        self.assertEqual(col, 1.5)

    async def test_trailing_dot(self):
        src = "1."
        col = parse_source(src)
        self.assertEqual(col, 1.0)

    async def test_leading_dot(self):
        src = ".5"
        col = parse_source(src)
        self.assertEqual(col, 0.5)

    async def test_scientific_notation(self):
        src = "1.5e2"
        col = parse_source(src)
        self.assertEqual(col, 1.5e2)

        src = ".5e2"
        col = parse_source(src)
        self.assertEqual(col, 0.5e2)

        src = "5e-1"
        col = parse_source(src)
        self.assertEqual(col, 5e-1)

    async def test_float_suffix(self):
        src = "1.5f"
        col = parse_source(src)
        self.assertEqual(col, 1.5)

        src = "1.f"
        col = parse_source(src)
        self.assertEqual(col, 1.0)

        src = ".5f"
        col = parse_source(src)
        self.assertEqual(col, 0.5)


class TestComplex(IsolatedAsyncioTestCase):

    async def test_complex_i(self):
        src = "1+5i"
        col = parse_source(src)
        self.assertEqual(col, complex("1+5j"))

    async def test_complex_j(self):
        src = "1+5j"
        col = parse_source(src)
        self.assertEqual(col, complex("1+5j"))


class TestFraction(IsolatedAsyncioTestCase):

    async def test_fraction_like_tokens_are_symbols(self):
        for src in ("1/2/3", "1/2x", "3/4ths", "1/2.5"):
            self.assertEqual(parse_source(src), symbol(src), src)

    async def test_simple_fraction(self):
        src = "1/2"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("fraction"), 1, 2, nil))

    async def test_negative_fraction(self):
        src = "-1/2"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("fraction"), -1, 2, nil))

    async def test_zero_denominator_is_a_syntax_error(self):
        for src in ("1/0", "-3/0"):
            with self.assertRaises(ReaderSyntaxError) as caught:
                parse_source(src)
            self.assertIn("zero denominator", str(caught.exception))
            self.assertEqual(1, caught.exception.lineno)

    async def test_zero_denominator_dict_key(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source("#{1/0: 1}")


class TestDecimal(IsolatedAsyncioTestCase):

    async def test_decimal_with_fraction(self):
        src = "1.5d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (1, 5), -1), nil))

    async def test_leading_dot_decimal(self):
        src = ".5d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (5,), -1), nil))

    async def test_trailing_dot_decimal(self):
        src = "1.d"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("decimal"), (0, (1,), 0), nil))


class TestStrings(IsolatedAsyncioTestCase):

    async def test_empty_string(self):
        src = '""'
        col = parse_source(src)
        self.assertEqual(col, "")

        src = ' "" '
        col = parse_source(src)
        self.assertEqual(col, "")

    async def test_escaped_quote(self):
        src = r' "\"" '
        col = parse_source(src)
        self.assertEqual(col, "\"")

    async def test_simple_string(self):
        src = '"hello world"'
        col = parse_source(src)
        self.assertEqual(col, "hello world")

    async def test_escape_sequences(self):
        src = r' "hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, "hello\n \tworld")

        src = ' "hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, "hello\n \tworld")

    async def test_malformed_escape_names_the_escape(self):
        cases = (
            (r'"\N{NOPE}"', r'\N{NOPE}', "unknown Unicode character name"),
            (r'"\xZZ"', r'\xZZ', "truncated \\xXX escape"),
            (r'b"\xZZ"', r'\xZZ', "truncated \\xXX escape"),
            (r'f"\xZZ"', r'\xZZ', "truncated \\xXX escape"),
        )
        for src, escape, reason in cases:
            with self.assertRaises(ReaderSyntaxError) as caught:
                parse_source(src)
            message = str(caught.exception)
            self.assertIn(escape, message, src)
            self.assertIn(reason, message, src)

    async def test_unicode(self):
        src = '"hello"'
        col = parse_source(src)
        self.assertEqual(col, "hello")


class TestTripleStrings(IsolatedAsyncioTestCase):

    async def test_empty_3string(self):
        src = r'""""""'
        col = parse_source(src)
        self.assertEqual(col, "")

    async def test_3string_with_space(self):
        src = r' """ """ '
        col = parse_source(src)
        self.assertEqual(col, " ")

    async def test_3string_simple(self):
        src = r'"""hello world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello world')

    async def test_3string_with_quotes(self):
        src = r'"""hello "favorite" world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello "favorite" world')

    async def test_3string_escaped_quotes(self):
        src = r'"""hello \""" "\"" ""\" world"""'
        col = parse_source(src)
        self.assertEqual(col, 'hello """ """ """ world')

    async def test_3string_trailing_quote(self):
        src = r'"""hello "world\""""'
        col = parse_source(src)
        self.assertEqual(col, 'hello "world"')


class TestRawStrings(IsolatedAsyncioTestCase):

    async def test_empty_raw(self):
        src = r'r""'
        col = parse_source(src)
        self.assertEqual(col, "")

    async def test_raw_with_escapes(self):
        src = r' r"\"" '
        col = parse_source(src)
        self.assertEqual(col, r"\"")

        src = r' r"hello\n \tworld" '
        col = parse_source(src)
        self.assertEqual(col, r"hello\n \tworld")


class TestByteStrings(TestCase):

    def test_empty_bstring(self):
        self.assertEqual(parse_source('b""'), b'')

        self.assertEqual(parse_source(' b"" '), b'')

    def test_simple_bstring(self):
        self.assertEqual(parse_source('b"hello world"'), b'hello world')

    def test_bstring_escapes(self):
        self.assertEqual(parse_source(r'b"hello\n \tworld"'), b'hello\n \tworld')

    def test_bstring_hex_escape_yields_byte(self):
        self.assertEqual(parse_source(r'b"\x41"'), b'A')

    def test_bstring_high_byte_escapes(self):
        self.assertEqual(parse_source(r'b"\xff"'), b'\xff')
        self.assertEqual(parse_source(r'b"\x80\xfe\377"'), b'\x80\xfe\xff')
        self.assertEqual(parse_source(r'b"a\xffb"'), b'a\xffb')

    def test_bstring_literal_non_ascii_rejected(self):
        # a literal character above 127 is not a byte, even when it
        # would fit in one; the escape form is the byte
        with self.assertRaises(ReaderSyntaxError):
            parse_source('b"\u2603"')
        with self.assertRaises(ReaderSyntaxError):
            parse_source('b"caf\u00e9"')
        self.assertEqual(parse_source(r'b"caf\xe9"'), b'caf\xe9')

    def test_bstring_escape_above_byte_range_rejected(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source(r'b"\u2603"')

    def test_bstring_triple_quoted(self):
        src = r'b"""hello "x" """'
        self.assertEqual(parse_source(src), b'hello "x" ')

    def test_bstring_in_list(self):
        col = parse_source('(b"ab" b"cd")')
        self.assertEqual(car(col), b'ab')
        self.assertEqual(car(cdr(col)), b'cd')
        self.assertIs(cdr(cdr(col)), nil)

    def test_bstring_non_ascii_literal_raises(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_source('b"café"')

    def test_b_prefix_without_quote_is_not_bstring(self):
        self.assertEqual(parse_source('bogus'), symbol('bogus'))


class TestQuoted(IsolatedAsyncioTestCase):

    async def test_quote_symbol(self):
        src = """
        'foo
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("quote"), symbol("foo"), nil))

    async def test_quote_list(self):
        src = """
        '(foo bar)
        """
        col = parse_source(src)
        expected = cons(symbol("quote"),
                        cons(symbol("foo"), symbol("bar"), nil),
                        nil)
        self.assertEqual(col, expected)

    async def test_quasiquote(self):
        src = """
        `bar
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("quasiquote"), symbol("bar"), nil))

    async def test_unquote_splicing(self):
        src = """
        `(,@foo)
        """
        col = parse_source(src)
        exp = cons(
            symbol("quasiquote"),
            cons(cons(symbol("unquote-splicing"), symbol("foo"), nil), nil),
            nil)
        self.assertEqual(col, exp)

    async def test_unquote_operand_keeps_splice_and_at(self):
        # splicing is the @ right after the comma and nothing else: a
        # call to a function named splice is a call, an @ operator form
        # or an @ inside an atom in the operand reads as written
        def unquoted(src):
            # the comma macro is in force inside a quasiquote only
            form = parse_source("`(" + src + ")")
            return cdr(form).car.car

        self.assertEqual(
            unquoted(",(splice xs)"),
            cons(symbol("unquote"),
                 cons(symbol("splice"), symbol("xs"), nil), nil))
        self.assertEqual(
            unquoted(",(@ M N)"),
            cons(symbol("unquote"),
                 cons(symbol("@"), symbol("M"), symbol("N"), nil), nil))
        self.assertEqual(
            unquoted(",foo@bar"),
            cons(symbol("unquote"), symbol("foo@bar"), nil))
        self.assertEqual(
            unquoted(",@xs"),
            cons(symbol("unquote-splicing"), symbol("xs"), nil))

    async def test_quote_syntax_prefix(self):
        src = """
        #'foo
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("quote-syntax"), symbol("foo"), nil))

    async def test_quote_syntax_prefix_list(self):
        src = """
        #'(a b)
        """
        col = parse_source(src)
        exp = cons(symbol("quote-syntax"),
                   cons(symbol("a"), symbol("b"), nil), nil)
        self.assertEqual(col, exp)

    async def test_quasiquote_syntax_prefix(self):
        src = """
        #`(a ,b ,@c)
        """
        col = parse_source(src)
        exp = cons(
            symbol("quasiquote-syntax"),
            cons(symbol("a"),
                 cons(symbol("unquote"), symbol("b"), nil),
                 cons(symbol("unquote-splicing"), symbol("c"), nil),
                 nil),
            nil)
        self.assertEqual(col, exp)

    async def test_hash_collections_still_read(self):
        src = """
        #[1 2]
        """
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#list"), 1, 2, nil))

    async def test_bare_hash_at_end_of_input_is_a_symbol(self):
        self.assertEqual(parse_source("#"), symbol("#"))
        self.assertEqual(parse_source("# "), symbol("#"))


class TestLists(IsolatedAsyncioTestCase):

    async def test_simple_list(self):
        src = "(testing a thing)"
        col = parse_source(src)
        exp = cons(symbol("testing"),
                   symbol("a"),
                   symbol("thing"),
                   nil)
        self.assertEqual(col, exp)

    async def test_bracket_list(self):
        src = "[testing a thing]"
        col = parse_source(src)
        exp = cons(symbol("testing"),
                   symbol("a"),
                   symbol("thing"),
                   nil)
        self.assertEqual(col, exp)

    async def test_mismatched_delimiters(self):
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


class TestConsPairs(IsolatedAsyncioTestCase):

    async def test_curly_brace_is_gensym_thunk(self):
        src = "{testing a thing}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("gensym-thunk"),
                                   symbol("testing"),
                                   symbol("a"),
                                   symbol("thing"),
                                   nil))

    async def test_dot(self):
        src = "(testing . 123)"
        col = parse_source(src)
        exp = cons(symbol("testing"), 123)
        self.assertEqual(col, exp)

    async def test_newline(self):
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

    async def test_comments(self):
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

    async def test_multi(self):
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


class TestPositions(IsolatedAsyncioTestCase):

    async def test_position_1(self):
        src = """
        (hello world)
        """
        strm = source_str(src, "<unittest>")
        expr = default_reader.read(strm)

        self.assertEqual(expr.get_position(), (2, 8))
        expr = cdr(expr)
        self.assertEqual(expr.get_position(), (2, 15))

    async def test_position_2(self):
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


class TestCollections(IsolatedAsyncioTestCase):

    async def test_tuple(self):
        src = "#(1 2 3)"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#tuple"), 1, 2, 3, nil))

    async def test_list(self):
        src = "#[1 2 3]"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#list"), 1, 2, 3, nil))

    async def test_dict(self):
        src = "#{a 1 b 2}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#dict"),
                                   symbol("a"), 1,
                                   symbol("b"), 2,
                                   nil))


    async def test_dict_pairs(self):
        src = "#{(key . val) (key2 . val2)}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#dict"),
                                   cons(symbol("key"), symbol("val")),
                                   cons(symbol("key2"), symbol("val2")),
                                   nil))


    async def test_dict_keywords(self):
        # keyword keys are lowered into (#tuple KEY VALUE) entries
        src = "#{key: val key2: val2}"
        col = parse_source(src)
        self.assertEqual(col, cons(symbol("#dict"),
                                   cons(symbol("#tuple"),
                                        symbol("key"), symbol("val"), nil),
                                   cons(symbol("#tuple"),
                                        symbol("key2"), symbol("val2"), nil),
                                   nil))


class TestReader(IsolatedAsyncioTestCase):

    async def test_read_atom(self):
        from koskari.reader import Atom, Reader

        reader = Reader()
        stream = source_str("hello", "<unittest>")
        result = reader.read_atom(stream)
        self.assertIsInstance(result, Atom)
        self.assertEqual(result, "hello")

    async def test_read_and_position(self):
        from koskari.reader import Reader

        reader = Reader()
        stream = source_str("  hello", "<unittest>")
        value, pos = reader.read_and_position(stream)
        self.assertIs(value, symbol("hello"))
        self.assertEqual(pos, (1, 2))


class TestBaseSourceStreamCreate(IsolatedAsyncioTestCase):

    async def test_create_with_stream_only(self):
        s = BaseSourceStream(StringIO("test"))
        self.assertIsNotNone(s)

    async def test_create_with_filename(self):
        s = BaseSourceStream(StringIO("test"), "test.txt")
        self.assertEqual(s.filename, "test.txt")

    async def test_create_with_position(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", line=5, col=10)
        self.assertEqual(s.position(), (5, 10))

    async def test_default_position(self):
        s = BaseSourceStream(StringIO("test"), "test.txt")
        self.assertEqual(s.position(), (1, 0))


class TestBaseSourceStreamRead(IsolatedAsyncioTestCase):

    async def test_read_single(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ch = s.read(1)
        self.assertEqual(ch, "h")

    async def test_read_multiple(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read(3)
        self.assertEqual(data, "hel")

    async def test_read_updates_column(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        s.read(3)
        self.assertEqual(s.position(), (1, 3))

    async def test_read_newline_updates_line(self):
        s = BaseSourceStream(StringIO("ab\ncd"), "test.txt")
        s.read(3)
        self.assertEqual(s.position(), (2, 0))

    async def test_read_past_newline(self):
        s = BaseSourceStream(StringIO("ab\ncd"), "test.txt")
        s.read(4)
        self.assertEqual(s.position(), (2, 1))

    async def test_read_at_eof(self):
        s = BaseSourceStream(StringIO("hi"), "test.txt")
        s.read(2)
        data = s.read(1)
        self.assertEqual(data, "")

    async def test_read_unicode_position(self):
        # Multi-byte UTF-8 characters should count as 1 column each
        s = BaseSourceStream(StringIO("日本語"), "test.txt")
        s.read(3)
        # 3 characters = column 3, not 9 (which would be UTF-8 bytes)
        self.assertEqual(s.position(), (1, 3))

    async def test_read_mixed_unicode_position(self):
        # Mix of ASCII and multi-byte characters
        s = BaseSourceStream(StringIO("héllo"), "test.txt")
        s.read(5)
        # 5 characters = column 5, not 6 (é is 2 UTF-8 bytes)
        self.assertEqual(s.position(), (1, 5))


class TestBaseSourceStreamPeek(IsolatedAsyncioTestCase):

    async def test_peek_single(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ch = s.peek(1)
        self.assertEqual(ch, "h")

    async def test_peek_multiple(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.peek(3)
        self.assertEqual(data, "hel")

    async def test_peek_does_not_advance(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        s.peek(3)
        self.assertEqual(s.position(), (1, 0))

    async def test_peek_then_read(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        peeked = s.peek(2)
        read = s.read(2)
        self.assertEqual(peeked, read)


class TestBaseSourceStreamReadUntilChars(IsolatedAsyncioTestCase):

    async def test_read_until_space(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        data = s.read_until_chars(" ")
        self.assertEqual(data, "hello")

    async def test_read_until_newline(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        data = s.read_until_chars("\n")
        self.assertEqual(data, "hello")

    async def test_read_until_multiple_terminators(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        data = s.read_until_chars(" \n\t")
        self.assertEqual(data, "hello")

    async def test_read_until_no_match(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read_until_chars("x")
        self.assertEqual(data, "hello")

    async def test_read_until_updates_position(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until_chars(" ")
        self.assertEqual(s.position(), (1, 5))

    async def test_terminator_not_consumed(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until_chars(" ")
        ch = s.read(1)
        self.assertEqual(ch, " ")


class TestBaseSourceStreamReadUntil(IsolatedAsyncioTestCase):

    async def test_read_until_basic(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        data = s.read_until(lambda c: c == ' ')
        self.assertEqual(data, "hello")

    async def test_read_until_no_match(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read_until(lambda c: c == 'x')
        self.assertEqual(data, "hello")

    async def test_read_until_immediate(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        data = s.read_until(lambda c: c == 'h')
        self.assertEqual(data, "")

    async def test_read_until_lambda(self):
        s = BaseSourceStream(StringIO("abc123def"), "test.txt")
        data = s.read_until(lambda c: c.isdigit())
        self.assertEqual(data, "abc")

    async def test_read_until_method(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        terminators = " \t\n"
        data = s.read_until(terminators.__contains__)
        self.assertEqual(data, "hello")

    async def test_read_until_updates_position(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until(lambda c: c == ' ')
        self.assertEqual(s.position(), (1, 5))

    async def test_read_until_terminator_not_consumed(self):
        s = BaseSourceStream(StringIO("hello world"), "test.txt")
        s.read_until(lambda c: c == ' ')
        ch = s.read(1)
        self.assertEqual(ch, " ")

    async def test_read_until_requires_callable(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        with self.assertRaises(TypeError):
            s.read_until("not callable")


class TestBaseSourceStreamSkipWhitespace(IsolatedAsyncioTestCase):

    async def test_skip_spaces(self):
        s = BaseSourceStream(StringIO("   hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "   ")

    async def test_skip_tabs(self):
        s = BaseSourceStream(StringIO("\t\thello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "\t\t")

    async def test_skip_newlines(self):
        s = BaseSourceStream(StringIO("\n\nhello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "\n\n")

    async def test_skip_mixed_whitespace(self):
        s = BaseSourceStream(StringIO(" \t\n hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, " \t\n ")

    async def test_skip_no_whitespace(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        ws = s.skip_whitespace()
        self.assertEqual(ws, "")

    async def test_skip_updates_position(self):
        s = BaseSourceStream(StringIO("\n  hello"), "test.txt")
        s.skip_whitespace()
        self.assertEqual(s.position(), (2, 2))


class TestBaseSourceStreamReadline(IsolatedAsyncioTestCase):

    async def test_readline_simple(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "hello\n")

    async def test_readline_updates_position(self):
        s = BaseSourceStream(StringIO("hello\nworld"), "test.txt")
        s.readline()
        self.assertEqual(s.position(), (2, 0))

    async def test_readline_at_eof(self):
        s = BaseSourceStream(StringIO("hello"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "hello")

    async def test_readline_empty_line(self):
        s = BaseSourceStream(StringIO("\nhello"), "test.txt")
        line = s.readline()
        self.assertEqual(line, "\n")

    async def test_readline_over_a_lone_surrogate(self):
        """
        The newline scan reads code points, so a character that cannot
        be encoded is read through like any other
        """

        s = BaseSourceStream(StringIO("a \ud800 b\nnext"), "test.txt")
        self.assertEqual(s.readline(), "a \ud800 b\n")
        self.assertEqual(s.readline(), "next")


class TestBaseSourceStreamProperties(IsolatedAsyncioTestCase):

    async def test_stream_property(self):
        stream = StringIO("test")
        s = BaseSourceStream(stream, "test.txt")
        self.assertIs(s.stream, stream)

    async def test_filename_property(self):
        s = BaseSourceStream(StringIO("test"), "myfile.txt")
        self.assertEqual(s.filename, "myfile.txt")

    async def test_lin_property(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", line=5)
        self.assertEqual(s.lin, 5)

    async def test_col_property(self):
        s = BaseSourceStream(StringIO("test"), "test.txt", col=10)
        self.assertEqual(s.col, 10)


class TestBaseSourceStreamSubclass(IsolatedAsyncioTestCase):

    async def test_can_subclass(self):
        class MyStream(BaseSourceStream):
            pass

        s = MyStream(StringIO("test"), "test.txt")
        self.assertIsInstance(s, BaseSourceStream)


class TestSourceStreamInheritance(IsolatedAsyncioTestCase):

    async def test_inherits_from_base(self):
        s = source_str("test", "<unittest>")
        self.assertIsInstance(s, BaseSourceStream)

    async def test_type_is_source_stream(self):
        from koskari.reader import SourceStream
        s = source_str("test", "<unittest>")
        self.assertIsInstance(s, SourceStream)

    async def test_read_method_from_native(self):
        s = source_str("hello", "<unittest>")
        data = s.read(3)
        self.assertEqual(data, "hel")

    async def test_peek_method_from_native(self):
        s = source_str("hello", "<unittest>")
        data = s.peek(3)
        self.assertEqual(data, "hel")
        # peek should not advance
        self.assertEqual(s.position(), (1, 0))

    async def test_position_tracking(self):
        s = source_str("ab\ncd", "<unittest>")
        s.read(3)  # reads "ab\n"
        self.assertEqual(s.position(), (2, 0))

    async def test_read_until_method(self):
        s = source_str("hello world", "<unittest>")
        data = s.read_until(lambda c: c == ' ')
        self.assertEqual(data, "hello")

    async def test_skip_whitespace_native(self):
        from koskari.reader import SourceStream
        s = SourceStream(StringIO("  hello"), "test.txt", auto_skip_exec=False)
        # SourceStream uses native skip_whitespace from BaseSourceStream
        ws = s.skip_whitespace()
        self.assertEqual(ws, "  ")
        self.assertEqual(s.position(), (1, 2))


class TestBaseSourceStreamUninitialized(TestCase):
    """
    An instance built without __init__ has no stream, reports that
    plainly, and refuses to read
    """

    def test_fields_read_as_none(self):
        s = BaseSourceStream.__new__(BaseSourceStream)
        self.assertIsNone(s.stream)
        self.assertIsNone(s.filename)
        self.assertEqual(s.position(), (1, 0))

    def test_methods_raise_value_error(self):
        s = BaseSourceStream.__new__(BaseSourceStream)
        with self.assertRaises(ValueError):
            s.read()
        with self.assertRaises(ValueError):
            s.peek()
        with self.assertRaises(ValueError):
            s.mark()


# The end.
