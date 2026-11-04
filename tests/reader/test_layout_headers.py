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
tests.reader.test_layout_headers

Strict layout header semantics.
"""

from textwrap import dedent

from unittest import IsolatedAsyncioTestCase, TestCase
from unittest.mock import patch

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import LayoutError, ReaderSyntaxError
from koskari.lib import cons, keyword, kos_repr, nil, symbol
from koskari.reader import (
    LINE_END, READ_EOF, IndentAwareStream, LayoutReader, source_str)


pytestmark = pytest.mark.phase_infra


def read_layout(src_str, **defns):
    builtins = dict(_BUILTIN_REGISTRY)
    builtins.update(defns)
    reader = LayoutReader(builtins)
    stream = IndentAwareStream.from_source(
        source_str(dedent(src_str), '<unittest>'))
    return reader.read(stream)


def read_layout_mode(src_str, **reader_kwargs):
    builtins = dict(_BUILTIN_REGISTRY)
    reader = LayoutReader(builtins, **reader_kwargs)
    stream = IndentAwareStream.from_source(
        source_str(dedent(src_str), '<unittest>'))
    return reader.read(stream)


class TestLayoutStrictHeaders(IsolatedAsyncioTestCase):

    async def test_missing_trailing_colon_is_error(self):
        src = """
        def foo []
          (print 1)
          (do-something-dangerous)
        """

        expected = cons(symbol('def'),
                        symbol('foo'),
                        nil,
                        nil)

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_begin_colon_is_header(self):
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

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_begin_without_colon_is_symbol_only(self):
        src = """
        begin
          x
        """

        result = read_layout(src)
        self.assertEqual(result, symbol('begin'))

    async def test_head_comment_disappears_from_commit_decision(self):
        src = """
        begin ;; comment
          x
        """

        result = read_layout(src)
        self.assertEqual(result, symbol('begin'))

    async def test_committing_to_header_requires_structural_colon(self):
        src = """
        define [x]
          y
        """

        expected = cons(symbol('define'),
                        cons(symbol('x'), nil),
                        nil)

        result = read_layout(src)
        self.assertEqual(result, expected)


class TestLayoutReaderParity(IsolatedAsyncioTestCase):

    async def test_simple_block(self):
        src = """
        foo x:
          y
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             symbol('y'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_header_colon_terminator_with_comment_after(self):
        src = """
        unknown x:  ;; comment
          y
        """

        expected = pycons(
            (symbol('unknown'),
             symbol('x'),
             symbol('y'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_keyword_atoms_at_depth0_in_header_args(self):
        src = """
        unknown as: x:
          y
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('as'),
             symbol('x'),
             symbol('y'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_keyword_colon_not_structural_when_followed_by_token(self):
        src = """
        unknown x: y:
          z
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('x'),
             symbol('y'),
             symbol('z'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_trailing_keyword(self):
        src = """
        unknown x::
          z
        """

        expected = pycons(
            (symbol('unknown'),
             keyword('x'),
             symbol('z'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_crlf_header_terminator(self):
        src = "unknown x:\r\n  y\n"

        expected = pycons(
            (symbol('unknown'),
             symbol('x'),
             symbol('y'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_keyword_inside_brackets_does_not_terminate(self):
        src = """
        unknown [(foo) as: x]:
          y
        """

        expected = pycons(
            (symbol('unknown'),
             ((symbol('foo'),), keyword('as'), symbol('x')),
             symbol('y'),
             )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_try_except_only(self):
        src = """
        try:
          x
        except [Exception]:
          y
        """

        try_body = cons(symbol('begin'), symbol('x'), nil)
        except_clause = cons(cons(symbol('Exception'), nil), symbol('y'), nil)
        expected = cons(symbol('try'), try_body, except_clause, nil)

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_try_full_block(self):
        src = """
        try:
          x
        except [Exception]:
          y
        else:
          a
        finally:
          z
        """

        try_body = cons(symbol('begin'), symbol('x'), nil)
        except_clause = cons(cons(symbol('Exception'), nil), symbol('y'), nil)
        else_clause = cons(keyword('else'), symbol('a'), nil)
        finally_clause = cons(keyword('finally'), symbol('z'), nil)
        expected = cons(
            symbol('try'),
            try_body,
            except_clause,
            else_clause,
            finally_clause,
            nil)

        result = read_layout(src)
        self.assertEqual(result, expected)


class TestLayoutHeaderNestingParity(IsolatedAsyncioTestCase):

    async def test_block_header_s_expr_spanning_lines(self):
        src = """
        foo [[x 1000]
             [y 2000]]:
          (do_something x y)
        """

        expected = pycons(
            (symbol('foo'),
             ((symbol('x'), 1000),
              (symbol('y'), 2000),),
             (symbol('do_something'), symbol('x'), symbol('y'))
            )
        )

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_block_header_keyword_inside_brackets(self):
        src = """
        unknown [(foo) as: x]:
          y
        """

        expected = pycons(
            (symbol('unknown'),
             ((symbol('foo'),), keyword('as'), symbol('x')),
             symbol('y')))

        result = read_layout(src)
        self.assertEqual(result, expected)

    async def test_block_header_keyword_inside_parens(self):
        src = """
        unknown (foo as: x):
          y
        """

        expected = pycons(
            (symbol('unknown'),
             (symbol('foo'), keyword('as'), symbol('x')),
             symbol('y')))

        result = read_layout(src)
        self.assertEqual(result, expected)


class TestLayoutCallSemantics(IsolatedAsyncioTestCase):

    async def test_single_symbol_line_is_value(self):
        result = read_layout('foo')
        self.assertEqual(result, symbol('foo'))


    async def test_symbol_headed_line_with_args_becomes_call(self):
        result = read_layout('foo x y')
        self.assertEqual(result, cons(symbol('foo'), symbol('x'), symbol('y'), nil))


    async def test_explicit_list_form_remains_call(self):
        result = read_layout('(foo)')
        self.assertEqual(result, cons(symbol('foo'), nil))


    async def test_single_symbol_value_without_dot(self):
        result = read_layout('x')
        self.assertEqual(result, symbol('x'))


    async def test_parenthesized_expr_without_dot(self):
        result = read_layout('(foo x)')
        self.assertEqual(result, cons(symbol('foo'), symbol('x'), nil))


    async def test_bare_dot_is_invalid(self):
        with self.assertRaises(ReaderSyntaxError):
            read_layout('.')


    async def test_multi_symbol_line_is_implicit_call(self):
        result = read_layout('foo bar')
        self.assertEqual(result, cons(symbol('foo'), symbol('bar'), nil))


    async def test_pair_head_line_with_arg_becomes_call(self):
        result = read_layout('(foo bar) 123')
        self.assertEqual(
            result,
            cons(cons(symbol('foo'), symbol('bar'), nil), 123, nil))


    async def test_pair_head_line_with_multiple_args_becomes_call(self):
        result = read_layout('(get-callee) a b')
        self.assertEqual(
            result,
            cons(cons(symbol('get-callee'), nil), symbol('a'), symbol('b'), nil))


    async def test_number_head_line_implicit_fold(self):
        result = read_layout('123 456')
        self.assertEqual(result, cons(123, 456, nil))


    async def test_string_head_line_implicit_fold(self):
        result = read_layout('"x" 1')
        self.assertEqual(result, cons('x', 1, nil))


class TestLayoutLineEvents(IsolatedAsyncioTestCase):

    async def test_read_header_form_line_end_at_semicolon(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = IndentAwareStream.from_source(
            source_str(';; tail', '<unittest>'))
        stream.skip_whitespace()
        event, _pos, _val = reader._read_header_form(stream)
        self.assertIs(event, LINE_END)

    async def test_quote_open_paren_uses_reader_quote_not_raw_block(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = IndentAwareStream.from_source(
            source_str("' (1 2)\n", '<unittest>'))
        result = reader.read(stream)
        expected = cons(
            symbol('quote'),
            cons(1, 2, nil),
            nil)
        self.assertEqual(result, expected)


class TestQuoteBlockHeaderScan(IsolatedAsyncioTestCase):
    """
    The scan that decides whether a quote-prefixed line opens a block
    reads past string literals and bracketed comments on the header
    """

    async def test_string_on_the_header_does_not_hide_the_block_colon(self):
        for literal in ('"s:"', '"("', '"x ; y"'):
            with self.subTest(literal=literal):
                src = f"'a {literal} :\n  d\n"
                expected = cons(
                    symbol('quote'),
                    cons(keyword('#block'),
                         cons(symbol('a'), literal[1:-1], nil),
                         cons(keyword('#line'), symbol('d'), nil),
                         nil),
                    nil)
                self.assertEqual(read_layout(src), expected)


    async def test_colon_inside_a_triple_string_is_not_structural(self):
        src = "'a \"\"\"s:\n\"\"\" d\n"
        expected = cons(
            cons(symbol('quote'), symbol('a'), nil),
            "s:\n",
            symbol('d'),
            nil)
        self.assertEqual(read_layout(src), expected)


    async def test_bracketed_comment_on_the_header_does_not_count(self):
        src = "'a (x ; )\n   y) :\n  d\n"
        expected = cons(
            symbol('quote'),
            cons(keyword('#block'),
                 cons(symbol('a'), cons(symbol('x'), symbol('y'), nil), nil),
                 cons(keyword('#line'), symbol('d'), nil),
                 nil),
            nil)
        self.assertEqual(read_layout(src), expected)


class TestQuotePrefixMidLine(IsolatedAsyncioTestCase):
    """
    A block quote prefix opens a quote block only as the first element
    of its line; elsewhere it quotes the next form and the line's colon
    is the line's own block colon
    """

    async def test_quoted_argument_on_a_block_header(self):
        src = "case 'north:\n  (print 1)\n"
        result = read_layout(src, **{'layout/case': None})
        expected = pycons(
            (symbol('case'),
             (symbol('quote'), symbol('north')),
             (symbol('print'), 1)))
        self.assertEqual(result, expected)


    async def test_elements_before_the_prefix_are_kept(self):
        src = "show 1 'tag:\n  body\n"
        expected = pycons(
            (symbol('show'), 1,
             (symbol('quote'), symbol('tag')),
             symbol('body')))
        self.assertEqual(read_layout(src), expected)


    async def test_sharp_quote_argument_on_a_block_header(self):
        src = "show #'tag:\n  body\n"
        expected = pycons(
            (symbol('show'),
             (symbol('quote-syntax'), symbol('tag')),
             symbol('body')))
        self.assertEqual(read_layout(src), expected)


class TestQuotePrefixInFirstElement(IsolatedAsyncioTestCase):
    """
    Only the prefix at the first character of a line opens a quote
    block; a prefix inside that first element quotes its operand
    """

    async def test_stacked_prefix_quotes_the_head(self):
        for prefix, head in (("'", 'quote'), ("#'", 'quote-syntax')):
            with self.subTest(prefix=prefix):
                src = f"{prefix}'x a:\n  d\n"
                expected = cons(
                    symbol(head),
                    cons(keyword('#block'),
                         cons(cons(symbol('quote'), symbol('x'), nil),
                              symbol('a'), nil),
                         cons(keyword('#line'), symbol('d'), nil),
                         nil),
                    nil)
                self.assertEqual(read_layout(src), expected)


    async def test_prefix_under_a_ref_is_not_a_block(self):
        src = "&'x a:\n  d\n"
        expected = pycons(
            ((symbol('ref'), (symbol('quote'), symbol('x'))),
             symbol('a'),
             symbol('d')))
        self.assertEqual(read_layout(src), expected)


    async def test_stacked_prefixes_scan_the_line_once(self):
        reader = LayoutReader(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str("'" * 50 + "x\n", '<unittest>'))
        with patch.object(
                reader, '_structural_colon_before_line_end',
                wraps=reader._structural_colon_before_line_end) as scan:
            result = reader.read(stream)

        expected = symbol('x')
        for _ in range(50):
            expected = cons(symbol('quote'), expected, nil)
        self.assertEqual(result, expected)
        self.assertEqual(scan.call_count, 1)


class TestQuoteLineEndsAtNewline(IsolatedAsyncioTestCase):
    """
    A line opened by a quote prefix ends at its newline: the block colon
    scan and the quote block header do not read into later lines
    """

    async def test_quoted_line_before_a_block_in_a_body(self):
        expected = pycons(
            (symbol('def'), symbol('f'), nil,
             (symbol('quote'), symbol('c')),
             (symbol('when'), symbol('x'), symbol('y'))))
        for between in ('', '\n', '  ; note\n'):
            with self.subTest(between=between):
                src = f"def f []:\n  'c\n{between}  when x:\n    y\n"
                self.assertEqual(read_layout(src), expected)


    async def test_quoted_line_before_a_top_level_block(self):
        reader = LayoutReader(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str("'c\nwhen x:\n  y\n", '<unittest>'))
        first = reader.read(stream)
        second = reader.read(stream)
        self.assertEqual(first, pycons((symbol('quote'), symbol('c'))))
        self.assertEqual(
            second, pycons((symbol('when'), symbol('x'), symbol('y'))))
        self.assertIs(reader.read(stream), READ_EOF)


    async def test_header_does_not_continue_on_the_next_line(self):
        reader = LayoutReader(dict(_BUILTIN_REGISTRY))
        stream = IndentAwareStream.from_source(
            source_str("'a\n  b:\n  c\n", '<unittest>'))
        with self.assertRaises(ReaderSyntaxError):
            while reader.read(stream) is not READ_EOF:
                pass


class TestLayoutLineOrientedRead(IsolatedAsyncioTestCase):

    async def test_one_line_pair_plus_paren_folds_like_cons_of_parts(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        src = '(lambda [f] (print f)) (+ 1 10)'
        combined = reader.read(IndentAwareStream.from_source(
            source_str(src, '<t>')))
        head = reader.read(IndentAwareStream.from_source(
            source_str('(lambda [f] (print f))', '<t>')))
        arg = reader.read(IndentAwareStream.from_source(
            source_str('(+ 1 10)', '<t>')))
        self.assertEqual(combined, cons(head, arg, nil))

    async def test_blank_logical_lines_are_skipped(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = IndentAwareStream.from_source(source_str('\n\nfoo\n', '<t>'))
        self.assertEqual(reader.read(stream), symbol('foo'))

    async def test_implicit_false_errors_on_two_values_one_line(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins, layout_implicit_calls=False)
        stream = IndentAwareStream.from_source(source_str('foo bar', '<t>'))
        with self.assertRaises(LayoutError):
            reader.read(stream)

    async def test_pair_head_block(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        src = '(foo) a:\n  y\n'
        result = reader.read(IndentAwareStream.from_source(
            source_str(src, '<t>')))
        expected = pycons(
            (cons(symbol('foo'), nil),
             symbol('a'),
             symbol('y'),
             )
        )
        self.assertEqual(result, expected)


class TestLayoutCompatibilityModes(TestCase):

    def test_disable_implicit_calls_preserves_bare_symbol(self):
        result = read_layout_mode('begin', layout_implicit_calls=False)
        self.assertEqual(result, symbol('begin'))


    def test_disable_layout_mode_bypasses_layout_block_parsing(self):
        result = read_layout_mode('foo x:\n  y\n', layout_enabled=False)
        self.assertEqual(result, symbol('foo'))


class TestLayoutReaderWhitespace(TestCase):

    def test_sexpr_crazy_whitespace(self):
        src = """
        (SomeWords

            foo
        bar  baz:        quux
            squids

            wibble:
        100
            )
        """

        result = read_layout_mode(src, layout_enabled=True)
        self.assertEqual(result, pycons(
            (symbol('SomeWords'),
             symbol('foo'),
             symbol('bar'),
             keyword('baz'),
             symbol('quux'),
             symbol('squids'),
             keyword('wibble'),
             100,
            )))


    def test_sexpr_with_header_inline(self):
        src = """
        SomeWords (vals

            foo
        bar  baz:        quux
            squids

            wibble:
        100
            )
        """

        result = read_layout_mode(src, layout_enabled=True)
        expected = pycons(
            (symbol('SomeWords'),
             (symbol('vals'),
                symbol('foo'),
                symbol('bar'),
                keyword('baz'),
                symbol('quux'),
                symbol('squids'),
                keyword('wibble'),
                100,
            )))

        print("expected:", kos_repr(expected))
        print("result  :", kos_repr(result))

        self.assertEqual(result, expected)


    def test_sexpr_with_header_newline(self):
        src = """
        SomeWords:
            (vals

            foo
        bar  baz:        quux
            squids

            wibble:
        100
            )
        """

        result = read_layout_mode(src, layout_enabled=True)
        expected = pycons(
            (symbol('SomeWords'),
             (symbol('vals'),
                symbol('foo'),
                symbol('bar'),
                keyword('baz'),
                symbol('quux'),
                symbol('squids'),
                keyword('wibble'),
                100,
            )))

        print("expected:", kos_repr(expected))
        print("result  :", kos_repr(result))

        self.assertEqual(result, expected)


# The end.
