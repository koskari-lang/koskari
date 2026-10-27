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
Tests for koskari.reader.layouts.basics layout_global.

A global block reads through the layout of the word after global, and
through the default block layout when that word has none.
"""


from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.compile.helpers import pycons
from koskari.lib import symbol
from koskari.types import get_position

from .. import parse_layout


pytestmark = pytest.mark.phase_infra


class TestLayoutGlobal(IsolatedAsyncioTestCase):

    async def test_global_class_omit_supers(self):
        src = """
        global class C:
          x
        """
        expected = pycons(
            (symbol('global'),
             symbol('class'),
             symbol('C'),
             (),
             (symbol('begin'), symbol('x'))))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_global_class_with_supers(self):
        src = """
        global class C (Base):
          x
        """
        expected = pycons(
            (symbol('global'),
             symbol('class'),
             symbol('C'),
             (symbol('Base'),),
             (symbol('begin'), symbol('x'))))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_global_def_falls_to_block(self):
        src = """
        global def f [x]:
          x
        """
        expected = pycons(
            (symbol('global'),
             symbol('def'),
             symbol('f'),
             (symbol('x'),),
             symbol('x')))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_global_var_falls_to_block(self):
        src = """
        global var x:
          y
        """
        expected = pycons(
            (symbol('global'), symbol('var'), symbol('x'), symbol('y')))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_global_alone_falls_to_block(self):
        src = """
        global:
          x
        """
        expected = pycons((symbol('global'), symbol('x')))
        result = parse_layout(src)
        self.assertEqual(result, expected)


    async def test_global_keeps_block_position(self):
        src = """
        global class C:
          x
        """
        pos = get_position(parse_layout(src))
        self.assertIsNotNone(pos)
        self.assertEqual(2, pos[0])


# The end.
