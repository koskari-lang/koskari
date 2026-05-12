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

Integration tests exercising compile/quasiquote through the LayoutReader
block syntax.  These verify that quasi-quoted forms written in indentation
style parse and compile correctly.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from unittest import IsolatedAsyncioTestCase

from koskari.lib import nil
from koskari.lib import keyword, symbol
from koskari.compile.helpers_ng import pycons


from .. import parse_layout

import pytest

pytestmark = pytest.mark.phase_infra

_keyword_block = keyword(':block:')


class TestQuasiquoteLayout(IsolatedAsyncioTestCase):
    """
    Atoms and simple lists inside quasiquote via layout syntax
    """

    async def test_backtick_atom_in_layout_block(self):
        """
        A backtick atom inside a define body compiles and evaluates correctly
        """

        src = """
        define get-name []:
          `hello
        """

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"), (symbol("compile/quasiquote"), symbol("hello"))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_unquote_in_layout_block(self):
        src = """
        define get-name []:
          `(hello ,world)
        """

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"),
            (symbol("compile/quasiquote"),
             (symbol('hello'),
              (symbol("compile/unquote"), symbol("world"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_unquote_splicing_in_layout_block(self):
        src = """
        define get-name []:
          `(hello ,@world)
        """

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"),
            (symbol("compile/quasiquote"),
             (symbol('hello'),
             (symbol("compile/unquote-splicing"), symbol("world"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_mid_heading(self):
        src = """
        define get-name []:
          without `(who ,@hello) we:
            sing
        """

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"),
            (symbol("without"),
             (symbol("compile/quasiquote"),
              (symbol("who"),
               (symbol("compile/unquote-splicing"), symbol("hello")))),
             symbol("we"),
             (symbol("compile/begin"),
              (symbol("sing"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block(self):
        src = """
        define get-name []:
          `begin:
            ,@hello
        """

        _kw_block = keyword(':block:')

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"),
            (symbol("compile/quasiquote-block"),
             (_kw_block,
              (symbol("begin"),),
              ((symbol("compile/unquote-splicing"), symbol("hello")),)))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_with_peers(self):
        """
        A quasiquote-block whose body contains if/else sibling sub-blocks.
        Both sub-blocks appear as :block: siblings in the outer body.
        Unquote sites in sub-block headers and bodies are parsed correctly.
        """

        src = """
        define get-cond []:
          `begin:
            if ,test-expr:
              ,result
            else:
              ,fallback
        """

        _kw_block = keyword(':block:')

        expected = pycons(
          (symbol("define"), symbol("get-cond"), nil,
           (symbol("compile/begin"),
            (symbol("compile/quasiquote-block"),
             (_kw_block,
              (symbol("begin"),),
              ((_kw_block,
                (symbol("if"),
                 (symbol("compile/unquote"), symbol("test-expr"))),
                ((symbol("compile/unquote"), symbol("result")),)),
               (_kw_block,
                (symbol("else"),),
                ((symbol("compile/unquote"), symbol("fallback")),)),)))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


class TestQuoteBlock(IsolatedAsyncioTestCase):
    """
    Tests for the 'head: raw block quote syntax that produces compile/quote-block forms
    """

    async def test_simple_quote_block(self):
        """
        A leading ' before a block head reads the block as a :block: cons
        wrapped in compile/quote-block
        """

        src = """
        'begin:
          foo
          bar
        """

        expected = pycons(
            (symbol("compile/quote"),
             (_keyword_block,
              (symbol("begin"),),
              (symbol("foo"), symbol("bar"))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_empty_body(self):
        """
        A quoted block with no body produces a :block: with nil body list
        """

        src = """
        'begin:
        """

        from koskari.lib import car, cdr, cons
        from koskari.types import is_nil, is_pair

        result = parse_layout(src)

        self.assertTrue(is_pair(result))
        self.assertEqual(car(result), symbol("compile/quote"))

        block_repr = car(cdr(result))
        self.assertTrue(is_pair(block_repr))
        self.assertEqual(car(block_repr), _keyword_block)

        head_args = car(cdr(block_repr))
        self.assertTrue(is_pair(head_args))
        self.assertEqual(car(head_args), symbol("begin"))

        body_exprs = car(cdr(cdr(block_repr)))
        self.assertTrue(is_nil(body_exprs))


    async def test_quote_block_nested_sub_block(self):
        """
        Sub-blocks inside a quoted block become nested :block: cons nodes
        rather than being expanded through layout handlers
        """

        src = """
        'begin:
          if x:
            foo
        """

        expected = pycons(
            (symbol("compile/quote"),
             (_keyword_block,
              (symbol("begin"),),
              ((_keyword_block,
                (symbol("if"), symbol("x")),
                (symbol("foo"),)),)))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_quote_block_inside_normal_block(self):
        """
        A quoted block used as a body element inside a normal define block
        is returned as a compile/quote-block form in the body
        """

        src = """
        define get-block []:
          'begin:
            foo
            bar
        """

        expected = pycons(
            (symbol("define"), symbol("get-block"), nil,
             (symbol("compile/begin"),
              (symbol("compile/quote"),
               (_keyword_block,
                (symbol("begin"),),
                (symbol("foo"), symbol("bar"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


# The end.
