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
Tests for koskari.reader.layouts.exceptions layout_try.

try/except/else/finally block syntax parsing.
"""


from unittest import TestCase

from koskari.types import cons, keyword, nil, symbol
from koskari.compile.helpers import pycons
from .. import parse_layout


_symbol_try = symbol('try')
_compile_begin = symbol('compile/begin')


class TestLayoutTryExceptBindings(TestCase):

    def test_except_bare_type(self):
        src = """
        try:
          x
        except Exception:
          y
        """

        expected = pycons(
          (symbol('try'),
           (_compile_begin, symbol('x')),
           ((keyword('except'), symbol('Exception')), symbol('y')))
        )

        result = parse_layout(src)

        print(f"result  : {result}")
        print(f"expected: {expected}")

        self.assertEqual(result, expected)


    def test_except_simple_binding(self):
        src = """
        try:
          x
        except [e Exception]:
          y
        """

        expected = pycons(
          (symbol('try'),
           (_compile_begin, symbol('x')),
           ((symbol('e'), symbol('Exception')), symbol('y')))
        )

        result = parse_layout(src)

        print(f"result  : {result}")
        print(f"expected: {expected}")

        self.assertEqual(result, expected)


    def test_except_as_binding(self):
        src = """
        try:
          x
        except [Exception as: e]:
          y
        """

        expected = pycons(
          (symbol('try'),
           (_compile_begin, symbol('x')),
           ((symbol('e'), symbol('Exception')), symbol('y')))
        )

        result = parse_layout(src)

        print(f"result  : {result}")
        print(f"expected: {expected}")

        self.assertEqual(result, expected)


    def test_except_untyped_error(self):
        src = """
        try:
          x
        except:
          y
        """

        expected = pycons(
          (symbol('try'),
           (_compile_begin, symbol('x')),
           (keyword('except'), symbol('y')))
        )

        result = parse_layout(src)

        print(f"result  : {result}")
        print(f"expected: {expected}")

        self.assertEqual(result, expected)


class TestLayoutTry(TestCase):

    def test_try_except_only(self):
        src = """
        try:
          x
        except Exception:
          y
        """
        try_body = cons(_compile_begin, symbol('x'), nil)
        except_clause = cons(
            cons(keyword('except'), symbol('Exception'), nil),
            symbol('y'), nil)
        expected = cons(_symbol_try, try_body, except_clause, nil)
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_try_finally_only(self):
        src = """
        try:
          x
        finally:
          z
        """
        try_body = cons(_compile_begin, symbol('x'), nil)
        finally_clause = cons(keyword('finally'), symbol('z'), nil)
        expected = cons(_symbol_try, try_body, finally_clause, nil)
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_try_full_block(self):
        src = """
        try:
          x
        except Exception:
          y
        else:
          a
        finally:
          z
        """
        try_body = cons(_compile_begin, symbol('x'), nil)
        except_clause = cons(
            cons(keyword('except'), symbol('Exception'), nil),
            symbol('y'), nil)
        else_clause = cons(keyword('else'), symbol('a'), nil)
        finally_clause = cons(keyword('finally'), symbol('z'), nil)
        expected = cons(_symbol_try, try_body, except_clause, else_clause, finally_clause, nil)
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_try_no_handlers_error(self):
        src = """
        try:
          x
        """
        with self.assertRaises(Exception):
            parse_layout(src)


    def test_try_multiple_else_error(self):
        src = """
        try:
          x
        except Exception:
          y
        else:
          a
        else:
          b
        """
        with self.assertRaises(Exception):
            parse_layout(src)


    def test_try_multiple_finally_error(self):
        src = """
        try:
          x
        finally:
          a
        finally:
          b
        """
        with self.assertRaises(Exception):
            parse_layout(src)


# The end.
