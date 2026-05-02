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

Tests for stacked reader macros and terminating-only dispatch

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""

from unittest import IsolatedAsyncioTestCase

from koskari.lib import symbol
from koskari.reader import DEFER, Reader, source_str
from koskari.reader.parser import ATOM_RESUME, VALUE

import pytest

pytestmark = pytest.mark.phase_infra


class TestStackedMacros(IsolatedAsyncioTestCase):

    async def test_prepend_order_defers_to_next(self):
        reader = Reader()

        def outer(stream, c):
            return DEFER, None

        def inner(stream, c):
            return VALUE, symbol('second')

        reader.set_event_macro('z', inner, False)
        reader.set_event_macro('z', outer, False)

        stack = reader.get_event_macros('z')
        self.assertEqual(len(stack), 2)

        v = reader.read(source_str('zw', '<t>'))
        self.assertEqual(v, symbol('second'))

    async def test_temporary_removes_only_that_pair(self):
        reader = Reader()

        def permanent(stream, c):
            return VALUE, symbol('perm')

        def temp(stream, c):
            return DEFER, None

        reader.set_event_macro('z', permanent, False)
        with reader.temporary_event_macro('z', temp, False):
            v = reader.read(source_str('zw', '<t>'))
            self.assertEqual(v, symbol('perm'))
        v2 = reader.read(source_str('zw', '<t>'))
        self.assertEqual(v2, symbol('perm'))
        self.assertEqual(len(reader.get_event_macros('z')), 1)


class TestTerminatingOnlyDispatch(IsolatedAsyncioTestCase):

    async def test_terminating_chain_fallback_atom_resume(self):
        reader = Reader()

        def term_macro(stream, c):
            return DEFER, None

        reader.set_event_macro(':', term_macro, True)
        stream = source_str('a:b', '<t>')
        stream.read(1)
        event, value = reader._dispatch_event_macros(
            stream, ':', terminating_only=True)
        self.assertIs(event, ATOM_RESUME)
        self.assertEqual(value, ':')


# The end.
