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
tests.compile.specials.test_quasiquote_layout

Integration tests exercising quasiquote through the LayoutReader
block syntax.  These verify that quasi-quoted forms written in indentation
style parse and compile correctly.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.compile.helpers import pycons
from koskari.lib import keyword, nil, symbol

from .. import parse_layout


pytestmark = pytest.mark.phase_infra

_keyword_block = keyword('#block')
_keyword_line = keyword('#line')


class TestQuasiquoteLayout(IsolatedAsyncioTestCase):
    """
    Atoms and simple lists inside quasiquote via layout syntax
    """

    async def test_backtick_atom_in_layout_block(self):
        """
        A backtick atom inside a def body compiles and evaluates correctly
        """

        src = """
        def get-name []:
          `hello
        """

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote"), symbol("hello")))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_unquote_in_layout_block(self):
        src = """
        def get-name []:
          `(hello ,world)
        """

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote"),
            (symbol('hello'),
             (symbol("unquote"), symbol("world")))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_unquote_splicing_in_layout_block(self):
        src = """
        def get-name []:
          `(hello ,@world)
        """

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote"),
            (symbol('hello'),
             (symbol("unquote-splicing"), symbol("world")))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_mid_heading(self):
        src = """
        def get-name []:
          without `(who ,@hello) we:
            sing
        """

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("without"),
            (symbol("quasiquote"),
             (symbol("who"),
              (symbol("unquote-splicing"), symbol("hello")))),
            symbol("we"),
            symbol("sing")))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block(self):
        src = """
        def get-name []:
          `begin:
            ,@hello
        """

        _kw_block = keyword('#block')

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote"),
            (_kw_block,
             (symbol("begin"),),
             (keyword('#splice'),
              (symbol("unquote-splicing"), symbol("hello")))))))

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quasiquote_syntax_block(self):
        src = """
        def get-name []:
          #`begin:
            ,@hello
        """

        _kw_block = keyword('#block')

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote-syntax"),
            (_kw_block,
             (symbol("begin"),),
             (keyword('#splice'),
              (symbol("unquote-splicing"), symbol("hello")))))))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_quote_syntax_block(self):
        src = """
        def get-name []:
          #'begin:
            hello
        """

        _kw_block = keyword('#block')

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quote-syntax"),
            (_kw_block,
             (symbol("begin"),),
             (_keyword_line, symbol("hello"))))))

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_quasiquote_syntax_atom_in_layout_block(self):
        src = """
        def get-name []:
          #`hello
        """

        expected = pycons(
          (symbol("def"), symbol("get-name"), nil,
           (symbol("quasiquote-syntax"), symbol("hello")))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_quote_block_with_peers(self):
        """
        A quasiquote-block whose body contains if/else sibling sub-blocks.
        Both sub-blocks appear as :#block: siblings in the outer body.
        Unquote sites in sub-block headers and bodies are parsed correctly.
        """

        src = """
        def get-cond []:
          `begin:
            if ,test-expr:
              ,result
            else:
              ,fallback
        """

        _kw_block = keyword('#block')

        expected = pycons(
          (symbol("def"), symbol("get-cond"), nil,
           (symbol("quasiquote"),
            (_kw_block,
             (symbol("begin"),),
             (_kw_block,
              (symbol("if"),
               (symbol("unquote"), symbol("test-expr"))),
              (_keyword_line, (symbol("unquote"), symbol("result")))),
             (_kw_block,
              (symbol("else"),),
              (_keyword_line, (symbol("unquote"), symbol("fallback"))))))))

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


class TestQuoteBlock(IsolatedAsyncioTestCase):
    """
    Tests for the 'head: raw block quote syntax that produces quote-block forms
    """

    async def test_simple_quote_block(self):
        """
        A leading ' before a block head reads the block as a :#block: cons
        wrapped in quote-block
        """

        src = """
        'begin:
          foo
          bar
        """

        expected = pycons(
            (symbol("quote"),
             (_keyword_block,
              (symbol("begin"),),
              (_keyword_line, symbol("foo")), (_keyword_line, symbol("bar"))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_empty_body(self):
        """
        A quoted block with no body produces a :#block: with nil body list
        """

        src = """
        'begin:
        """

        from koskari.lib import car, cdr, cons
        from koskari.types import is_nil, is_pair

        result = parse_layout(src)

        self.assertTrue(is_pair(result))
        self.assertEqual(car(result), symbol("quote"))

        block_repr = car(cdr(result))
        self.assertTrue(is_pair(block_repr))
        self.assertEqual(car(block_repr), _keyword_block)

        head_args = car(cdr(block_repr))
        self.assertTrue(is_pair(head_args))
        self.assertEqual(car(head_args), symbol("begin"))

        entries = cdr(cdr(block_repr))
        self.assertTrue(is_nil(entries))


    async def test_quote_block_nested_sub_block(self):
        """
        Sub-blocks inside a quoted block become nested :#block: cons nodes
        rather than being expanded through layout handlers
        """

        src = """
        'begin:
          if x:
            foo
        """

        expected = pycons(
            (symbol("quote"),
             (_keyword_block,
              (symbol("begin"),),
              (_keyword_block,
               (symbol("if"), symbol("x")),
               (_keyword_line, symbol("foo")))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_inside_normal_block(self):
        """
        A quoted block used as a body element inside a normal def block
        is returned as a quote-block form in the body
        """

        src = """
        def get-block []:
          'begin:
            foo
            bar
        """

        expected = pycons(
            (symbol("def"), symbol("get-block"), nil,
             (symbol("quote"),
              (_keyword_block,
               (symbol("begin"),),
               (_keyword_line, symbol("foo")), (_keyword_line, symbol("bar")))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


# The end.
