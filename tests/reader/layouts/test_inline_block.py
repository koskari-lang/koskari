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
Tests for koskari.reader.layouts.basics layout_inline_block.

The handler splices every indented line onto the header. It walks the
body spine, so what a line evaluates to never decides where the walk
stops.
"""


from unittest import TestCase

import pytest

from koskari.lib import cons, nil, symbol
from koskari.reader.layouts import layout_inline_block


pytestmark = pytest.mark.phase_infra


def _read(header, body):
    """
    Drive the handler directly with a header and a body spine
    """

    return layout_inline_block.read(None, cons(header, body), None)


class TestInlineBlockWalk(TestCase):
    """
    Every body line reaches the result
    """

    def test_all_lines_spliced(self):
        header = cons(symbol('foo'), symbol('x'), nil)
        body = cons(symbol('a'), symbol('b'), symbol('c'), nil)
        self.assertEqual(
            _read(header, body),
            cons(symbol('foo'), symbol('x'),
                 symbol('a'), symbol('b'), symbol('c'), nil))

    def test_falsy_line_does_not_end_the_walk(self):
        """
        a line whose value is falsy is one element like any other
        """

        header = cons(symbol('foo'), symbol('x'), nil)
        body = cons(0, symbol('y'), nil)
        self.assertEqual(
            _read(header, body),
            cons(symbol('foo'), symbol('x'), 0, symbol('y'), nil))

    def test_several_falsy_lines(self):
        header = cons(symbol('foo'), nil)
        body = cons(0, False, None, "", symbol('tail'), nil)
        self.assertEqual(
            _read(header, body),
            cons(symbol('foo'), 0, False, None, "", symbol('tail'), nil))

    def test_empty_body_yields_the_header(self):
        header = cons(symbol('foo'), symbol('x'), nil)
        self.assertEqual(
            _read(header, nil),
            cons(symbol('foo'), symbol('x'), nil))


# The end.
