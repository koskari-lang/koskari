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
tests.reader.test_layout_ng

Pass 1: strict layout_ng header semantics TDD.
"""

from unittest import IsolatedAsyncioTestCase, TestCase

import pytest

import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile.helpers import pycons
from koskari.exceptions import ReaderSyntaxError
from koskari.lib import cons, keyword, kos_repr, nil, symbol
from koskari.reader import LINE_END, LayoutReader, source_str


pytestmark = pytest.mark.phase_infra


def parse_layout_ng(src_str, **defns):
    builtins = dict(_BUILTIN_REGISTRY)
    builtins.update(defns)
    reader = LayoutReader(builtins)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


def parse_layout_ng_mode(src_str, **reader_kwargs):
    builtins = dict(_BUILTIN_REGISTRY)
    reader = LayoutReader(builtins, **reader_kwargs)
    stream = source_str(src_str, '<unittest>')
    return reader.read(stream)


class TestLayoutNgStrictHeaders(IsolatedAsyncioTestCase):

    async def test_missing_trailing_colon_is_error(self):
        src = """
        define foo []
          (print 1)
          (do-something-dangerous)
        """

        expected = cons(symbol('define'),
                        symbol('foo'),
                        nil,
                        nil)

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_begin_without_colon_is_symbol_only(self):
        src = """
        begin
          x
        """

        result = parse_layout_ng(src)
        self.assertEqual(result, symbol('begin'))

    async def test_head_comment_disappears_from_commit_decision(self):
        src = """
        begin ;; comment
          x
        """

        result = parse_layout_ng(src)
        self.assertEqual(result, symbol('begin'))

    async def test_committing_to_header_requires_structural_colon(self):
        src = """
        define [x]
          y
        """

        expected = cons(symbol('define'),
                        cons(symbol('x'), nil),
                        nil)

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)


class TestLayoutNgReaderParity(IsolatedAsyncioTestCase):

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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)

    async def test_crlf_header_terminator(self):
        src = "unknown x:\r\n  y\n"

        expected = pycons(
            (symbol('unknown'),
             symbol('x'),
             symbol('y'),
             )
        )

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)


class TestLayoutNgHeaderNestingParity(IsolatedAsyncioTestCase):

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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
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

        result = parse_layout_ng(src)
        self.assertEqual(result, expected)


class TestLayoutCallSemantics(IsolatedAsyncioTestCase):

    async def test_single_symbol_line_is_value(self):
        result = parse_layout_ng('foo')
        self.assertEqual(result, symbol('foo'))


    async def test_symbol_headed_line_with_args_becomes_call(self):
        result = parse_layout_ng('foo x y')
        self.assertEqual(result, cons(symbol('foo'), symbol('x'), symbol('y'), nil))


    async def test_explicit_list_form_remains_call(self):
        result = parse_layout_ng('(foo)')
        self.assertEqual(result, cons(symbol('foo'), nil))


    async def test_single_symbol_value_without_dot(self):
        result = parse_layout_ng('x')
        self.assertEqual(result, symbol('x'))


    async def test_parenthesized_expr_without_dot(self):
        result = parse_layout_ng('(foo x)')
        self.assertEqual(result, cons(symbol('foo'), symbol('x'), nil))


    async def test_bare_dot_is_invalid(self):
        with self.assertRaises(ReaderSyntaxError):
            parse_layout_ng('.')


    async def test_multi_symbol_line_is_implicit_call(self):
        result = parse_layout_ng('foo bar')
        self.assertEqual(result, cons(symbol('foo'), symbol('bar'), nil))


    async def test_pair_head_line_with_arg_becomes_call(self):
        result = parse_layout_ng('(foo bar) 123')
        self.assertEqual(
            result,
            cons(cons(symbol('foo'), symbol('bar'), nil), 123, nil))


    async def test_pair_head_line_with_multiple_args_becomes_call(self):
        result = parse_layout_ng('(get-callee) a b')
        self.assertEqual(
            result,
            cons(cons(symbol('get-callee'), nil), symbol('a'), symbol('b'), nil))


    async def test_number_head_line_implicit_fold(self):
        result = parse_layout_ng('123 456')
        self.assertEqual(result, cons(123, 456, nil))


    async def test_string_head_line_implicit_fold(self):
        result = parse_layout_ng('"x" 1')
        self.assertEqual(result, cons('x', 1, nil))


class TestLayoutLineEvents(IsolatedAsyncioTestCase):

    async def test_read_header_form_line_end_at_semicolon(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = source_str(';; tail', '<unittest>')
        stream.skip_whitespace()
        event, _pos, _val = reader._read_header_form(stream)
        self.assertIs(event, LINE_END)

    async def test_quote_open_paren_uses_reader_quote_not_raw_block(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = source_str("' (1 2)\n", '<unittest>')
        result = reader.read(stream)
        expected = cons(
            symbol('quote'),
            cons(1, 2, nil),
            nil)
        self.assertEqual(result, expected)


class TestLayoutLineOrientedRead(IsolatedAsyncioTestCase):

    async def test_one_line_pair_plus_paren_folds_like_cons_of_parts(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        src = '(lambda [f] (print f)) (+ 1 10)'
        combined = reader.read(source_str(src, '<t>'))
        head = reader.read(source_str('(lambda [f] (print f))', '<t>'))
        arg = reader.read(source_str('(+ 1 10)', '<t>'))
        self.assertEqual(combined, cons(head, arg, nil))

    async def test_blank_logical_lines_are_skipped(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        stream = source_str('\n\nfoo\n', '<t>')
        self.assertEqual(reader.read(stream), symbol('foo'))

    async def test_implicit_false_errors_on_two_values_one_line(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins, layout_implicit_calls=False)
        stream = source_str('foo bar', '<t>')
        with self.assertRaises(ReaderSyntaxError):
            reader.read(stream)

    async def test_pair_head_block(self):
        builtins = dict(_BUILTIN_REGISTRY)
        reader = LayoutReader(builtins)
        src = '(foo) a:\n  y\n'
        result = reader.read(source_str(src, '<t>'))
        expected = pycons(
            (cons(symbol('foo'), nil),
             symbol('a'),
             symbol('y'),
             )
        )
        self.assertEqual(result, expected)


class TestLayoutCompatibilityModes(TestCase):

    def test_disable_implicit_calls_preserves_bare_symbol(self):
        result = parse_layout_ng_mode('begin', layout_implicit_calls=False)
        self.assertEqual(result, symbol('begin'))


    def test_disable_layout_mode_bypasses_layout_block_parsing(self):
        result = parse_layout_ng_mode('foo x:\n  y\n', layout_enabled=False)
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

        result = parse_layout_ng_mode(src, layout_enabled=True)
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

        result = parse_layout_ng_mode(src, layout_enabled=True)
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

        result = parse_layout_ng_mode(src, layout_enabled=True)
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
