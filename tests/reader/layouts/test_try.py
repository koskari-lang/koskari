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
from .. import parse_layout


_symbol_try = symbol('try')
_compile_begin = symbol('compile/begin')


class TestLayoutTry(TestCase):

    def test_try_except_only(self):
        src = """
        try:
          x
        except Exception:
          y
        """
        try_body = cons(_compile_begin, cons(symbol('x'), nil))
        except_clause = cons(
            cons(keyword('except'), symbol('Exception')),
            cons(symbol('y'), nil))
        expected = cons(_symbol_try, cons(try_body, cons(except_clause, nil)))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    def test_try_finally_only(self):
        src = """
        try:
          x
        finally:
          z
        """
        try_body = cons(_compile_begin, cons(symbol('x'), nil))
        finally_clause = cons(keyword('finally'), cons(symbol('z'), nil))
        expected = cons(_symbol_try, cons(try_body, cons(finally_clause, nil)))
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
        try_body = cons(_compile_begin, cons(symbol('x'), nil))
        except_clause = cons(
            cons(keyword('except'), symbol('Exception')),
            cons(symbol('y'), nil))
        else_clause = cons(keyword('else'), cons(symbol('a'), nil))
        finally_clause = cons(keyword('finally'), cons(symbol('z'), nil))
        catches = cons(except_clause, cons(else_clause, cons(finally_clause, nil)))
        expected = cons(_symbol_try, cons(try_body, catches))
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
