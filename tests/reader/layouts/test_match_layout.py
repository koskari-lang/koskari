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
Tests for koskari.reader.layouts.conditionals layout_match

Match / case / else block syntax parsing. Inner case and else blocks are
read by the default layout/block, which splices body forms after the
header. Each case carries a single pattern form.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.exceptions import LayoutError
from koskari.lib import cons, keyword, symbol, nil
from koskari.compile.helpers import pycons

from .. import parse_layout

import pytest


pytestmark = pytest.mark.phase_infra


_symbol_match = symbol('compile/match')
_keyword_else = keyword('else')


class TestLayoutMatch(IsolatedAsyncioTestCase):

    async def test_match_basic(self):
        src = """
        match x:
          case [a b c]:
            "seq"
          case 5:
            "five"
          else:
            "meh"
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             (cons(symbol('a'), symbol('b'), symbol('c'), nil), "seq"),
             (5, "five"),
             (_keyword_else, "meh"))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_no_else(self):
        src = """
        match x:
          case [a b]:
            "two"
          case y:
            "bound"
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             (cons(symbol('a'), symbol('b'), nil), "two"),
             (symbol('y'), "bound"))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_else_only(self):
        src = """
        match x:
          else:
            "meh"
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             (_keyword_else, "meh"))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_multi_body(self):
        src = """
        match x:
          case [a]:
            (foo)
            (bar)
          else:
            (baz)
            (qux)
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             (cons(symbol('a'), nil), (symbol('foo'),), (symbol('bar'),)),
             (_keyword_else,
              (symbol('baz'),), (symbol('qux'),)))
        )

        self.assertEqual(parse_layout(src), expected)


class TestLayoutMatchValidation(IsolatedAsyncioTestCase):

    async def test_match_missing_value(self):
        src = """
        match:
          case [a]:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_extra_value(self):
        src = """
        match x y:
          case [a]:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_invalid_body_form(self):
        src = """
        match x:
          (foo 1)
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_case_after_else(self):
        src = """
        match x:
          else:
            "meh"
          case y:
            "y"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_multiple_else(self):
        src = """
        match x:
          else:
            "meh"
          else:
            "more"
        """

        self.assertRaises(LayoutError, parse_layout, src)


# The end.
