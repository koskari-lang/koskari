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
tests.reader.test_defer_reader_macro

Unit tests for reader event macro DEFER (miss) dispatch

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""

from unittest import IsolatedAsyncioTestCase

from koskari.lib import symbol
from koskari.reader import DEFER, Reader, source_str
from koskari.reader.parser import VALUE

import pytest

pytestmark = pytest.mark.phase_infra


def z_macro(stream, c):
    """
    When followed by ! consume it and return a sentinel symbol; else defer to
    default atom reading for that starting character
    """

    if stream.peek(1) == '!':
        stream.read(1)
        return VALUE, symbol('bang')
    return DEFER, None


class TestDeferReaderMacro(IsolatedAsyncioTestCase):

    async def test_defer_falls_through_to_default_atom(self):
        reader = Reader()
        reader.set_event_macro('z', z_macro)

        self.assertEqual(
            reader.read(source_str('zap', '<t>')),
            symbol('zap'))

    async def test_macro_handles_when_predicate_matches(self):
        reader = Reader()
        reader.set_event_macro('z', z_macro)

        self.assertEqual(
            reader.read(source_str('z!', '<t>')),
            symbol('bang'))

    async def test_hash_without_collection_opener_still_atom(self):
        reader = Reader()
        self.assertEqual(
            reader.read(source_str('#foo', '<t>')),
            symbol('#foo'))


# The end.
