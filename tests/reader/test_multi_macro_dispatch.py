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
tests.reader.test_multi_macro_dispatch

Tests for stacked reader macros

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from unittest import IsolatedAsyncioTestCase

import pytest

from koskari.lib import symbol
from koskari.reader import DEFER, Reader, source_str
from koskari.reader.parser import VALUE


pytestmark = pytest.mark.phase_infra


class TestStackedMacros(IsolatedAsyncioTestCase):

    async def test_prepend_order_defers_to_next(self):
        reader = Reader()

        def outer(stream, c):
            return DEFER, None

        def inner(stream, c):
            return VALUE, symbol('second')

        reader.add_event_macro('z', inner, False)
        reader.add_event_macro('z', outer, False)

        stack = reader.get_event_macros('z')
        self.assertEqual(len(stack), 2)
        self.assertEqual(len(stack[0]), 3)
        self.assertIsNone(stack[0][2])

        v = reader.read(source_str('zw', '<t>'))
        self.assertEqual(v, symbol('second'))

    async def test_temporary_removes_only_that_pair(self):
        reader = Reader()

        def permanent(stream, c):
            return VALUE, symbol('perm')

        def temp(stream, c):
            return DEFER, None

        reader.add_event_macro('z', permanent, False)
        with reader.temporary_event_macro('z', temp, False):
            v = reader.read(source_str('zw', '<t>'))
            self.assertEqual(v, symbol('perm'))
        v2 = reader.read(source_str('zw', '<t>'))
        self.assertEqual(v2, symbol('perm'))
        self.assertEqual(len(reader.get_event_macros('z')), 1)
        self.assertEqual(len(reader.get_event_macros('z')[0]), 3)

    async def test_temporary_removed_when_body_raises(self):
        reader = Reader()

        def temp(stream, c):
            return VALUE, symbol('temp')

        with self.assertRaises(RuntimeError):
            with reader.temporary_event_macro('z', temp, False):
                raise RuntimeError("mid-form")
        self.assertEqual(reader.get_event_macros('z'), [])
        self.assertEqual(reader.read(source_str('zw', '<t>')), symbol('zw'))

    async def test_incomplete_quasiquote_does_not_leave_unquote_installed(self):
        """
        A quasiquote cut short by end of input raises ReaderEOF from inside
        the temporary unquote macro; a later read on the same reader must
        see a comma as ordinary text again
        """

        from koskari.reader import ReaderEOF

        reader = Reader()
        before = reader.get_event_macros(',')
        with self.assertRaises(ReaderEOF):
            reader.read(source_str('`(a ,', '<t>'))
        self.assertEqual(reader.get_event_macros(','), before)

        complete = reader.read(source_str('`(a ,b)', '<t>'))
        plain = reader.read(source_str('(a ,b)', '<t>'))
        self.assertNotEqual(str(complete), str(plain))


# The end.
