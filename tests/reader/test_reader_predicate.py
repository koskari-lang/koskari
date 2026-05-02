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
tests.reader.test_reader_predicate

Tests for reader macro optional predicates (atom boundaries and early defer)

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


class TestPredicatedAtomBoundary(IsolatedAsyncioTestCase):

    async def test_fold_when_predicate_false(self):
        reader = Reader()

        def pipe_macro(stream, c):
            return VALUE, symbol('pipe-macro')

        def never_split(stream, c, atom_prefix):
            return False

        reader.set_event_macro('|', pipe_macro, True, predicate=never_split)

        v = reader.read(source_str('a|b', '<t>'))
        self.assertEqual(v, symbol('a|b'))

    async def test_split_when_predicate_true(self):
        reader = Reader()

        def pipe_macro(stream, c):
            return VALUE, symbol('pipe-macro')

        def always_split(stream, c, atom_prefix):
            return True

        reader.set_event_macro('|', pipe_macro, True, predicate=always_split)

        s = source_str('a|b', '<t>')
        self.assertEqual(reader.read(s), symbol('a'))
        self.assertEqual(reader.read(s), symbol('pipe-macro'))


class TestPredicateEarlyDefer(IsolatedAsyncioTestCase):

    async def test_predicate_skips_macro_like_defer(self):
        reader = Reader()

        def z_macro(stream, c):
            stream.read(1)
            return VALUE, symbol('bang')

        def z_pred(stream, c, atom_prefix):
            return stream.peek(1) == '!'

        reader.set_event_macro('z', z_macro, False, predicate=z_pred)

        self.assertEqual(
            reader.read(source_str('zap', '<t>')),
            symbol('zap'))

        self.assertEqual(
            reader.read(source_str('z!', '<t>')),
            symbol('bang'))


class TestPredicateStackComposition(IsolatedAsyncioTestCase):

    async def test_outer_predicate_false_folds_before_inner_hard(self):
        reader = Reader()

        def inner_macro(stream, c):
            return DEFER, None

        def outer_macro(stream, c):
            return DEFER, None

        reader.set_event_macro('|', inner_macro, True)
        reader.set_event_macro(
            '|', outer_macro, True, predicate=lambda s, c, a: False)

        v = reader.read(source_str('a|b', '<t>'))
        self.assertEqual(v, symbol('a|b'))

    async def test_outer_predicate_true_then_inner_hard_splits(self):
        reader = Reader()

        def inner_macro(stream, c):
            return DEFER, None

        def outer_macro(stream, c):
            return DEFER, None

        reader.set_event_macro('|', inner_macro, True)
        reader.set_event_macro(
            '|', outer_macro, True, predicate=lambda s, c, a: True)

        s = source_str('a|b', '<t>')
        self.assertEqual(reader.read(s), symbol('a'))
        self.assertEqual(reader.read(s), symbol('|b'))


# The end.
