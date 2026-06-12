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
tests.reader.test_parse_ng

Unit tests for koskari.reader.parser_ng stream layer and reader.

Tests focus on the new interface contracts introduced in parser_ng:
- SourceStream.pushback(text, position)
- IndentAwareStream.dedented
- IndentAwareStream as a proper SourceStream subclass (not a wrapper)
- Reader and LayoutReader behaviour through the new streams

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Sonnet 4.6 via Cursor
"""

from io import StringIO
from unittest import IsolatedAsyncioTestCase

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.compile.helpers import pycons
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.reader.parser import (READ_EOF, IndentAwareStream, LayoutReader,
                                   Reader, SourceStream, default_reader,
                                   source_str)
from koskari.lib import BaseSourceStream, cons, keyword, nil, symbol

import pytest

pytestmark = pytest.mark.phase_infra


def _stream(text, line_no=1, col_no=0):
    return SourceStream(StringIO(text), '<test>',
                        auto_skip_exec=False,
                        line_no=line_no, col_no=col_no)


def _indent_stream(text, max_indent=None, line_no=1, col_no=0):
    return IndentAwareStream(StringIO(text), '<test>',
                             auto_skip_exec=False,
                             line_no=line_no, col_no=col_no,
                             max_indent=max_indent)


def _parse_ng(src_str):
    stream = source_str(src_str, '<test>')
    return default_reader.read(stream)


def _parse_layout_ng(src_str, **defns):
    builtins = dict(_BUILTIN_REGISTRY)
    builtins.update(defns)
    reader = LayoutReader(builtins)
    stream = source_str(src_str, '<test>')
    return reader.read(stream)


# ===========================================================================
# SourceStream subclass identity
# ===========================================================================


class TestSourceStreamIsBaseSubclass(IsolatedAsyncioTestCase):

    async def test_source_stream_is_base_source_stream(self):
        s = _stream("hello")
        self.assertIsInstance(s, BaseSourceStream)

    async def test_source_stream_type(self):
        s = _stream("hello")
        self.assertIsInstance(s, SourceStream)

    async def test_indent_stream_is_source_stream(self):
        s = _indent_stream("hello")
        self.assertIsInstance(s, SourceStream)

    async def test_indent_stream_is_base_source_stream(self):
        s = _indent_stream("hello")
        self.assertIsInstance(s, BaseSourceStream)

    async def test_indent_stream_is_not_just_object(self):
        s = _indent_stream("hello")
        self.assertTrue(isinstance(s, IndentAwareStream))


# ===========================================================================
# SourceStream.pushback
# ===========================================================================


class TestSourceStreamPushback(IsolatedAsyncioTestCase):

    async def test_pushback_restores_text(self):
        s = _stream("hello")
        s.read(3)
        s.pushback("hel", (1, 0))
        self.assertEqual(s.read(3), "hel")

    async def test_pushback_restores_position(self):
        s = _stream("hello")
        s.read(3)
        s.pushback("hel", (1, 0))
        self.assertEqual(s.position(), (1, 0))

    async def test_pushback_empty_string_is_noop(self):
        s = _stream("hello")
        s.read(2)
        pos_before = s.position()
        s.pushback("", pos_before)
        self.assertEqual(s.position(), pos_before)
        self.assertEqual(s.read(3), "llo")

    async def test_pushback_then_peek(self):
        s = _stream("hello")
        s.read(3)
        s.pushback("hel", (1, 0))
        self.assertEqual(s.peek(3), "hel")
        self.assertEqual(s.position(), (1, 0))

    async def test_pushback_drain_then_continues_from_stream(self):
        s = _stream("hello")
        s.read(2)
        s.pushback("he", (1, 0))
        self.assertEqual(s.read(2), "he")
        self.assertEqual(s.read(3), "llo")

    async def test_pushback_position_restored_after_drain(self):
        s = _stream("hello")
        s.read(5)
        s.pushback("hello", (1, 0))
        s.read(5)
        # Buffer is now empty; position comes from the underlying stream
        self.assertEqual(s.position(), (1, 5))

    async def test_pushback_multiple_calls_prepend(self):
        s = _stream("xyz")
        s.read(3)
        s.pushback("z", (1, 2))
        s.pushback("xy", (1, 0))
        self.assertEqual(s.read(3), "xyz")

    async def test_pushback_interacts_with_read_until_chars(self):
        s = _stream("hello world")
        s.read(5)
        s.pushback("hello", (1, 0))
        result = s.read_until_chars(" ")
        self.assertEqual(result, "hello")
        # The space should now be next
        self.assertEqual(s.read(1), " ")

    async def test_pushback_peek_extends_into_underlying_stream(self):
        s = _stream("world")
        s.pushback("hel", (1, 0))
        # peek(6) should see "helwor"
        peeked = s.peek(6)
        self.assertEqual(peeked, "helwor")
        # position still at pushback start
        self.assertEqual(s.position(), (1, 0))


# ===========================================================================
# IndentAwareStream.dedented
# ===========================================================================


class TestIndentAwareStreamDedented(IsolatedAsyncioTestCase):

    async def test_dedented_false_initially(self):
        s = _indent_stream("  hello", max_indent=0)
        self.assertFalse(s.dedented)

    async def test_dedented_false_when_indented_enough(self):
        # "  hello" has indent 2; max_indent=1 means content at col 2 is inside
        s = _indent_stream("  hello", max_indent=1)
        s.skip_whitespace()
        self.assertFalse(s.dedented)

    async def test_dedented_true_when_at_boundary(self):
        # "\nhello" — next content at col 0; max_indent=0 means col 0 is outside
        s = _indent_stream("\nhello", max_indent=0)
        s.skip_whitespace()
        self.assertTrue(s.dedented)

    async def test_dedented_true_when_below_max_indent(self):
        # Content at col 2, max_indent=4 means col 2 is outside
        s = _indent_stream("\n  hello", max_indent=4)
        s.skip_whitespace()
        self.assertTrue(s.dedented)

    async def test_dedented_false_when_above_max_indent(self):
        # Content at col 6, max_indent=4 means col 6 is inside
        s = _indent_stream("\n      hello", max_indent=4)
        s.skip_whitespace()
        self.assertFalse(s.dedented)

    async def test_dedented_true_at_eof(self):
        s = _indent_stream("", max_indent=0)
        s.skip_whitespace()
        self.assertTrue(s.dedented)

    async def test_dedented_reset_on_each_call(self):
        s = _indent_stream("\nhello\n      world", max_indent=4)
        s.skip_whitespace()
        self.assertTrue(s.dedented)
        # Consume the newline and "hello" to get to the next line check
        s.read(1)  # '\n'
        s.read(5)  # 'hello'
        s.skip_whitespace()
        # Now content is at col 6 which is > 4
        self.assertFalse(s.dedented)

    async def test_no_max_indent_never_dedents(self):
        # With max_indent=None, native skip_whitespace runs; dedented stays False
        s = _indent_stream("\n  hello", max_indent=None)
        s.skip_whitespace()
        self.assertFalse(s.dedented)

    async def test_dedented_skips_blank_lines(self):
        # Blank line between; next real content is at col 0
        s = _indent_stream("\n\nhello", max_indent=2)
        s.skip_whitespace()
        self.assertTrue(s.dedented)

    async def test_dedented_does_not_consume_when_true(self):
        s = _indent_stream("\nhello", max_indent=0)
        s.skip_whitespace()
        self.assertTrue(s.dedented)
        # The newline must still be in the stream
        self.assertEqual(s.peek(1), '\n')


# ===========================================================================
# Reader through new SourceStream
# ===========================================================================


class TestReaderWithSourceStreamNg(IsolatedAsyncioTestCase):

    async def test_read_symbol(self):
        result = _parse_ng("hello")
        self.assertIs(result, symbol("hello"))

    async def test_read_integer(self):
        self.assertEqual(_parse_ng("42"), 42)

    async def test_read_string(self):
        self.assertEqual(_parse_ng('"hello"'), "hello")

    async def test_read_list(self):
        result = _parse_ng("(a b c)")
        self.assertEqual(result, cons(symbol("a"), symbol("b"), symbol("c"), nil))

    async def test_read_quote(self):
        result = _parse_ng("'foo")
        self.assertEqual(result, cons(symbol("compile/quote"), symbol("foo"), nil))

    async def test_read_quasiquote(self):
        result = _parse_ng("`foo")
        self.assertEqual(result, cons(symbol("compile/quasiquote"), symbol("foo"), nil))

    async def test_read_ref_atom(self):
        result = _parse_ng("&foo")
        self.assertEqual(
            result,
            cons(symbol("compile/ref"), symbol("foo"), nil))

    async def test_read_ref_atom_dotted(self):
        result = _parse_ng("&foo.bar")
        expected = cons(
            symbol("compile/ref"),
            cons(symbol("compile/attr"), symbol("foo"), symbol("bar"), nil),
            nil)
        self.assertEqual(result, expected)

    async def test_read_ref_atom_bare_ampersand(self):
        result = _parse_ng("(& 8 1)")
        expected = cons(symbol("&"), 8, 1, nil)
        self.assertEqual(result, expected)

    async def test_read_ref_atom_dotted_multi(self):
        result = _parse_ng("&foo.bar.baz")
        expected = cons(
            symbol("compile/ref"),
            cons(
                symbol("compile/attr"),
                cons(symbol("compile/attr"), symbol("foo"), symbol("bar"), nil),
                symbol("baz"),
                nil),
            nil)
        self.assertEqual(result, expected)

    async def test_read_dotted_attr_atom(self):
        result = _parse_ng("foo.bar")
        self.assertEqual(
            result,
            cons(symbol("compile/attr"), symbol("foo"), symbol("bar"), nil))

    async def test_read_dotted_attr_atom_multi(self):
        result = _parse_ng("obj.foo.bar")
        expected = cons(
            symbol("compile/attr"),
            cons(symbol("compile/attr"), symbol("obj"), symbol("foo"), nil),
            symbol("bar"),
            nil)
        self.assertEqual(result, expected)

    async def test_read_eof_returns_sentinel(self):
        self.assertIs(_parse_ng(""), READ_EOF)

    async def test_read_comment_skipped(self):
        result = _parse_ng("; comment\nhello")
        self.assertIs(result, symbol("hello"))

    async def test_read_keyword(self):
        result = _parse_ng(":x")
        self.assertIs(result, keyword("x"))


# ===========================================================================
# LayoutReader through new IndentAwareStream
# ===========================================================================


class TestLayoutReaderNg(IsolatedAsyncioTestCase):

    async def test_simple_block(self):
        src = """
        foo x:
          y
        """
        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             symbol('y')))
        result = _parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_if_else_block(self):
        src = """
        if x:
          1
        else:
          2
        """
        expected = pycons(
            (symbol('compile/cond'),
             (symbol('x'), 1),
             (keyword('else'), 2))
        )
        result = _parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_nested_blocks(self):
        src = """
        foo x:
          bar y:
            z
        """
        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('bar'),
              symbol('y'),
              symbol('z')))
        )
        result = _parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_colon_no_space(self):
        src_a = """
        unknown x:
          (print x)
        """
        src_b = """
        unknown x :
          (print x)
        """
        result_a = _parse_layout_ng(src_a)
        result_b = _parse_layout_ng(src_b)
        self.assertEqual(result_a, result_b)

    async def test_quote_block(self):
        src = """
        'begin:
          foo
          bar
        """
        _kw_block = keyword(':block:')
        expected = pycons(
            (symbol("compile/quote"),
             (_kw_block,
              (symbol("begin"),),
              (symbol("foo"), symbol("bar"))))
        )
        result = _parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_begin_colon_shorthand(self):
        src = """
        begin:
          x
          y
        """
        expected = pycons(
            (symbol("begin"),
             symbol("x"),
             symbol("y"))
        )
        result = _parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_stream_is_indent_aware_no_getattr(self):
        # This test verifies that LayoutReader.read wraps a plain SourceStream
        # in an IndentAwareStream so that stream.dedented is always a real
        # attribute and not discovered through getattr.
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        plain = source_str("foo x:\n  y\n", '<test>')
        self.assertIsInstance(plain, SourceStream)
        self.assertNotIsInstance(plain, IndentAwareStream)
        # read() should still work correctly by wrapping internally
        result = reader.read(plain)
        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             symbol('y')))
        self.assertEqual(result, expected)


# ===========================================================================
# SourceStream.read_until_chars with pushback buffer
# ===========================================================================


class TestSourceStreamReadUntilCharsWithBuffer(IsolatedAsyncioTestCase):

    async def test_read_until_chars_from_buffer(self):
        s = _stream("xyz")
        s.read(3)
        s.pushback("hello world", (1, 0))
        result = s.read_until_chars(" ")
        self.assertEqual(result, "hello")
        self.assertEqual(s.read(1), " ")

    async def test_read_until_chars_spanning_buffer_and_stream(self):
        s = _stream(" world")
        s.pushback("hello", (1, 0))
        result = s.read_until_chars(" ")
        self.assertEqual(result, "hello")

    async def test_terminator_pushed_back_after_read_until(self):
        s = _stream("hello world")
        s.read_until_chars(" ")
        # Terminator (space) must be next readable character
        self.assertEqual(s.peek(1), " ")
        self.assertEqual(s.read(1), " ")


# The end.
