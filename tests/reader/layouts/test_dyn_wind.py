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
Tests for koskari.reader.layouts.dyn_wind layout_dyn_wind

dyn-wind/before/after block syntax parsing

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.exceptions import LayoutError
from koskari.lib import cons, nil, symbol

from .. import parse_layout


pytestmark = pytest.mark.phase_infra

_symbol_dyn_wind = symbol('dyn-wind')
_symbol_begin = symbol('begin')


class TestLayoutDynWind(IsolatedAsyncioTestCase):

    async def test_body_before_after(self):
        src = """
        dyn-wind:
          x
        before:
          b
        after:
          a
        """
        expected = cons(
            _symbol_dyn_wind,
            cons(_symbol_begin, symbol('b'), nil),
            cons(_symbol_begin, symbol('x'), nil),
            cons(_symbol_begin, symbol('a'), nil),
            nil)
        self.assertEqual(parse_layout(src), expected)


    async def test_peer_order_does_not_matter(self):
        src = """
        dyn-wind:
          x
        after:
          a
        before:
          b
        """
        expected = cons(
            _symbol_dyn_wind,
            cons(_symbol_begin, symbol('b'), nil),
            cons(_symbol_begin, symbol('x'), nil),
            cons(_symbol_begin, symbol('a'), nil),
            nil)
        self.assertEqual(parse_layout(src), expected)


    async def test_body_after_only(self):
        src = """
        dyn-wind:
          x
          y
        after:
          a
        """
        expected = cons(
            _symbol_dyn_wind,
            nil,
            cons(_symbol_begin, symbol('x'), symbol('y'), nil),
            cons(_symbol_begin, symbol('a'), nil),
            nil)
        self.assertEqual(parse_layout(src), expected)


    async def test_body_before_only(self):
        src = """
        dyn-wind:
          x
        before:
          b
        """
        expected = cons(
            _symbol_dyn_wind,
            cons(_symbol_begin, symbol('b'), nil),
            cons(_symbol_begin, symbol('x'), nil),
            nil,
            nil)
        self.assertEqual(parse_layout(src), expected)


    async def test_header_arguments_rejected(self):
        src = """
        dyn-wind foo:
          x
        after:
          a
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


    async def test_duplicate_before_rejected(self):
        src = """
        dyn-wind:
          x
        before:
          b
        before:
          c
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


    async def test_duplicate_before_rejected_when_first_is_empty(self):
        src = """
        dyn-wind:
          x
        before:
        before:
          c
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


    async def test_duplicate_after_rejected_when_first_is_empty(self):
        src = """
        dyn-wind:
          x
        after:
        after:
          c
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


    async def test_peer_arguments_rejected(self):
        src = """
        dyn-wind:
          x
        after foo:
          a
        """
        with self.assertRaises(LayoutError):
            parse_layout(src)


# The end.
