# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.parse.test_layout

Unit tests for koskari.parse LayoutReader and block syntax.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.types import (
    car,
    cdr,
    is_pair,
    symbol,
    keyword,
    unpack,
    Module,
    ModuleNamespace,
)
from koskari.reader import READ_EOF, LayoutReader, source_str
from koskari.reader.layouts import LAYOUTS
from koskari.compile.helpers import pycons

from . import parse_layout

import pytest

pytestmark = pytest.mark.phase_infra

_layout_block = symbol('layout/block')


class TestLayoutBlockSyntax(IsolatedAsyncioTestCase):

    async def test_simple_block(self):
        src = """
        foo x:
          y
        """

        expected = pycons(
          (symbol('foo'),
           symbol('x'),
           (symbol('compile/begin'), symbol('y')),
          )
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


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
           (symbol('compile/begin'),
            (symbol('do_something'), symbol('x'), symbol('y')),
           )
          )
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_block_header_keyword_inside_brackets(self):
        """
        A trailing-colon keyword inside brackets in the block header is not
        treated as the block-opening colon
        """

        src = """
        unknown [(foo) as: x]:
          y
        """

        expected = pycons(
          (symbol('unknown'),
           ((symbol('foo'),), keyword('as'), symbol('x')),
           (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_block_header_keyword_inside_parens(self):
        """
        A trailing-colon keyword inside parentheses in the block header is not
        treated as the block-opening colon
        """

        src = """
        unknown (foo as: x):
          y
        """

        expected = pycons(
          (symbol('unknown'),
           (symbol('foo'), keyword('as'), symbol('x')),
           (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_nested_block_inside_block(self):
        src = """
        foo [[x 1]]:
          bar x:
            (baz x)
        """

        expected = pycons(
          (symbol('foo'),
           ((symbol('x'), 1),),
           (symbol('compile/begin'),
             ((symbol('bar'), symbol('x'),
              (symbol('compile/begin'),
               (symbol('baz'), symbol('x')))))))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_block_header_colon_no_space(self):
        """
        Both 'x:' and 'x :' parse identically
        """

        src_no_space = """
        unknown x:
          (print x)
        """
        src_with_space = """
        unknown x :
          (print x)
        """
        expected = pycons(
          (symbol('unknown'),
           symbol('x'),
           (symbol('compile/begin'),
            (symbol('print'), symbol('x'))))
        )

        result_no_space = parse_layout(src_no_space)
        result_with_space = parse_layout(src_with_space)

        self.assertEqual(result_no_space, expected)
        self.assertEqual(result_with_space, expected)
        self.assertEqual(result_no_space, result_with_space)


    async def test_dedent_ends_body(self):
        src = """
        foo x:
          a
          b
        bar
        """

        expected = pycons(
          (symbol('foo'),
           symbol('x'),
           (symbol('compile/begin'),
            symbol('a'),
            symbol('b'),))
          )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_fallback_no_layout_head(self):
        src = """
        unknown x:
          (print x)
        """

        expected = pycons(
          (symbol('unknown'),
           symbol('x'),
           (symbol('compile/begin'),
            (symbol('print'), symbol('x'))))
          )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_single_forms(self):
        src = """
        if x:
          1
        else :
          2
        """

        expected = pycons(
          (symbol('compile/cond'),
           (symbol('x'), 1),
           (keyword('else'), 2))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)

        # should by identical to above
        src = """
        if x:
          1
        else:
          2
        """

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_prefix_symbol_not_mangled_in_begin_body(self):
        src = """
        begin:
          define double [x]:
            (+ x x)
          define tagged-double [x]:
            (double-result (double x))
        """

        # This is a test for a parser regression where the second
        # "define" was being mangled to "efine"

        expected = pycons(
          (symbol("compile/begin"),

           (symbol("define"), symbol("double"), (symbol("x"),),
            (symbol("compile/begin"),
             (symbol("+"), symbol("x"), symbol("x")))),

           (symbol("define"), symbol("tagged-double"), (symbol("x"),),
            (symbol("compile/begin"),
             (symbol("double-result"), (symbol("double"), symbol("x"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


def _parse_layout_all(src_str):
    builtins = {}
    for name, value in LAYOUTS.items():
        builtins[str(name)] = value

    reader = LayoutReader(builtins)
    stream = source_str(src_str, '<unittest>')
    results = []
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break
        results.append(form)
    return results


class TestLayoutReaderComments(IsolatedAsyncioTestCase):

    async def test_comment_before_block_header(self):
        src = """
        ;; leading comment
        foo x:
          y
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_after_colon(self):
        src = """
        foo x: ;; trailing header comment
          y
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_as_first_body_line(self):
        src = """
        foo x:
          ;; first-body comment
          y
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('y')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_between_body_lines(self):
        src = """
        foo x:
          a
          ;; mid-body comment
          b
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('a'), symbol('b')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_after_last_body_line(self):
        src = """
        foo x:
          a
          ;; trailing body comment
        bar
        """

        expected = pycons(
            (symbol('foo'),
             symbol('x'),
             (symbol('compile/begin'), symbol('a')))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_in_multiline_formals(self):
        src = """
        define foobar [
              a  ; the a value
              b  ; the b value
              ]:
          (+ a b)
        """

        expected = pycons(
            (symbol('define'),
             symbol('foobar'),
             (symbol('a'), symbol('b')),
             (symbol('compile/begin'),
              (symbol('+'), symbol('a'), symbol('b'))))
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_comment_only_input_returns_read_eof(self):
        src = """
        ;; just a comment
        ;; another comment
        """

        result = parse_layout(src)
        self.assertIs(result, READ_EOF)


    async def test_comment_between_top_level_forms(self):
        src = """
        (+ 1 2)
        ;; between forms
        (* 3 4)
        """

        results = _parse_layout_all(src)

        self.assertEqual(len(results), 2)
        self.assertEqual(
            results[0],
            pycons((symbol('+'), 1, 2))
        )
        self.assertEqual(
            results[1],
            pycons((symbol('*'), 3, 4))
        )


class TestLayoutReaderNamespace(IsolatedAsyncioTestCase):

    async def test_accepts_module(self):
        mod_ns = ModuleNamespace('test', {})
        module = Module(mod_ns)
        reader = LayoutReader(module)
        self.assertIsNotNone(reader._ns)

    async def test_accepts_dict(self):
        reader = LayoutReader({})
        self.assertIsNotNone(reader._ns)


# The end.
