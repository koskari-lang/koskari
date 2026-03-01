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


from unittest import TestCase

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
from koskari.reader import LayoutReader, source_str
from koskari.reader.layouts import LAYOUTS
from koskari.compile.helpers import pycons

from . import parse_layout

_layout_block = symbol('layout/block')


class TestLayoutBlockSyntax(TestCase):

    def test_simple_block(self):
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


    def test_block_header_s_expr_spanning_lines(self):
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


    def test_nested_block_inside_block(self):
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


    def test_block_header_colon_no_space(self):
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


    def test_dedent_ends_body(self):
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


    def test_fallback_no_layout_head(self):
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


    def test_single_forms(self):
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


    def test_prefix_symbol_not_mangled_in_begin_body(self):
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


class TestLayoutReaderNamespace(TestCase):

    def test_accepts_module(self):
        mod_ns = ModuleNamespace('test', {})
        module = Module(mod_ns)
        reader = LayoutReader(module)
        self.assertIsNotNone(reader._ns)

    def test_accepts_dict(self):
        reader = LayoutReader({})
        self.assertIsNotNone(reader._ns)


# The end.
