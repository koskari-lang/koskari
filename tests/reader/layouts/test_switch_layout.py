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
Tests for koskari.reader.layouts.conditionals layout_switch

Switch / case / else block syntax parsing. Inner case and else blocks
are read by the default layout/block, which splices body forms after
the header.
"""


from unittest import IsolatedAsyncioTestCase

from koskari.exceptions import LayoutError
from koskari.lib import cons, keyword, symbol, nil
from koskari.compile.helpers import pycons

from .. import parse_layout

import pytest


pytestmark = pytest.mark.phase_infra


_symbol_switch = symbol('compile/switch')
_keyword_else = keyword('else')


class TestLayoutSwitch(IsolatedAsyncioTestCase):

    async def test_switch_basic_match(self):
        src = """
        switch x:
          case [1]:
            "one"
          case [2]:
            "two"
          else:
            "meh"
        """

        expected = pycons(
            (_symbol_switch, symbol('x'),
             (cons(1, nil), "one"),
             (cons(2, nil), "two"),
             (_keyword_else, "meh"))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_switch_no_else(self):
        src = """
        switch x:
          case [1]:
            "one"
          case [2]:
            "two"
        """

        expected = pycons(
            (_symbol_switch, symbol('x'),
             (cons(1, nil), "one"),
             (cons(2, nil), "two"))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_switch_else_only(self):
        src = """
        switch x:
          else:
            "meh"
        """

        expected = pycons(
            (_symbol_switch, symbol('x'),
             (_keyword_else, "meh"))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


    async def test_switch_multi_body(self):
        src = """
        switch x:
          case [1]:
            (foo)
            (bar)
          else:
            (baz)
            (qux)
        """

        expected = pycons(
            (_symbol_switch, symbol('x'),
             (cons(1, nil), (symbol('foo'),), (symbol('bar'),)),
             (_keyword_else,
              (symbol('baz'),), (symbol('qux'),)))
        )

        result = parse_layout(src)

        print("result   :", result)
        print("expected :", expected)

        self.assertEqual(result, expected)


class TestLayoutSwitchValidation(IsolatedAsyncioTestCase):

    async def test_switch_missing_value(self):
        src = """
        switch:
          case [1]:
            "one"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_extra_value(self):
        src = """
        switch x y:
          case [1]:
            "one"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_invalid_body_form(self):
        src = """
        switch x:
          (foo 1)
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_case_missing_cond(self):
        src = """
        switch x:
          case:
            "one"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_case_extra_cond(self):
        src = """
        switch x:
          case 1 2:
            "one"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_case_after_else(self):
        src = """
        switch x:
          else:
            "meh"
          case 1:
            "one"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_switch_multiple_else(self):
        src = """
        switch x:
          else:
            "meh"
          else:
            "more"
        """

        self.assertRaises(LayoutError, parse_layout, src)


# The end.
