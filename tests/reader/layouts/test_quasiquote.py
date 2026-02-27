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

from unittest import TestCase

from koskari.lib import nil
from koskari.types import keyword, symbol
from koskari.compile.helpers import pycons


from .. import parse_layout


class TestQuasiquoteLayout(TestCase):
    """
    Atoms and simple lists inside quasiquote via layout syntax
    """

    def test_backtick_atom_in_layout_block(self):
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


    def test_unquote_in_layout_block(self):
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


    def test_unquote_splicing_in_layout_block(self):
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


    def test_quote_block_mid_heading(self):
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


    def skipme_test_quote_block(self):
        src = """
        define get-name []:
          `begin:
            ,@hello
        """

        expected = pycons(
          (symbol("define"), symbol("get-name"), nil,
           (symbol("compile/begin"),
            (symbol("compile/quasiquote"),
             (symbol("begin"),
              (symbol("compile/unquote-splicing"), symbol("hello"))))))
        )

        result = parse_layout(src)
        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


# The end.
