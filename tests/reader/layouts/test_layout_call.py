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
tests.reader.layouts.test_layout_call

Reproducer for layout/call: positionals, *: splice, and keyword pairs
interleaved in the block body.
"""

from unittest import IsolatedAsyncioTestCase

from koskari.lib import keyword, symbol
from koskari.compile.helpers_ng import pycons

from .. import parse_layout

import pytest

pytestmark = pytest.mark.phase_infra

_sym_call = symbol('compile/call')


class TestLayoutCallReproducer(IsolatedAsyncioTestCase):

    async def test_call_values_positionals_star_splice_and_keywords(self):
        """
        Block call with literals, *: (range ...), and a:/b: lines.

        Mirrors Python:

            values(1, 2, 3, 10, 13, a='Hello', b='World')

        for range(10, 15, 3) -> (10, 13).
        """

        src = """
        call values:
          1
          2
          3
          *: (range 10 15 3)
          a: "Hello"
          b: "World"
        """

        expected = pycons(
            (_sym_call,
             symbol('values'),
             1,
             2,
             3,
             keyword('*:'),
             pycons(
                 (symbol('range'), 10, 15, 3)
             ),
             keyword('a:'),
             'Hello',
             keyword('b:'),
             'World')
        )

        result = parse_layout(src)
        self.assertEqual(result, expected)


# The end.
