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

Match / case / else block syntax parsing. A raw layout: each case
header (however many tokens) is wrapped whole into the clause's
pattern delimiter, so the emitted clause is [[PATTERN...] BODY...] --
the same shape an s-expr author writes directly.
"""


from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.compile.helpers import pycons
from koskari.exceptions import LayoutError
from koskari.lib import cons, keyword, nil, symbol

from .. import parse_layout


pytestmark = pytest.mark.phase_infra


_symbol_match = symbol('match')
_keyword_else = keyword('else')


class TestLayoutMatch(IsolatedAsyncioTestCase):

    async def test_match_single_form_patterns(self):
        src = """
        match x:
          case #[a b]:
            "seq"
          case 5:
            "five"
          else:
            "meh"
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             (((symbol('#list'), symbol('a'), symbol('b')),), "seq"),
             ((5,), "five"),
             (_keyword_else, "meh"))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_multi_token_pattern(self):
        src = """
        match x:
          case y :: int:
            y
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             ((symbol('y'), keyword('::'), symbol('int')), symbol('y')))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_when_guard(self):
        src = """
        match x:
          case y when (big y):
            y
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             ((symbol('y'),), keyword('when'),
              (symbol('big'), symbol('y')), symbol('y')))
        )

        self.assertEqual(parse_layout(src), expected)


    async def test_match_unless_guard_multi_token_pattern(self):
        src = """
        match x:
          case y :: int unless (big y):
            y
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             ((symbol('y'), keyword('::'), symbol('int')),
              keyword('unless'),
              (symbol('big'), symbol('y')), symbol('y')))
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
          case y:
            (foo)
            (bar)
          else:
            (baz)
            (qux)
        """

        expected = pycons(
            (_symbol_match, symbol('x'),
             ((symbol('y'),), (symbol('foo'),), (symbol('bar'),)),
             (_keyword_else,
              (symbol('baz'),), (symbol('qux'),)))
        )

        self.assertEqual(parse_layout(src), expected)


class TestLayoutMatchValidation(IsolatedAsyncioTestCase):

    async def test_match_missing_value(self):
        src = """
        match:
          case y:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_extra_value(self):
        src = """
        match x y:
          case z:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_case_missing_pattern(self):
        src = """
        match x:
          case:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_else_with_arguments(self):
        src = """
        match x:
          else y:
            "a"
        """

        self.assertRaises(LayoutError, parse_layout, src)


    async def test_match_guard_without_pattern(self):
        src = """
        match x:
          case when (big x):
            x
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
