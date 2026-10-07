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
tests.reproducers.test_match_width_before_copy

A sequence match arm compares the subject's length with the pattern's
width before reading any element, so a subject whose length rules the
arm out is never copied.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest
from collections.abc import Sequence

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'match_width_before_copy.ki')


class Counted(Sequence):
    """
    A sequence that records every element read
    """

    def __init__(self, items):
        self.items = items
        self.reads = []

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        self.reads.append(i)
        return self.items[i]


class TestMatchWidthBeforeCopy(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('match_width_before_copy')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_too_long_for_every_arm_reads_nothing(self):
        mod = await self.load()
        subject = Counted([1, 2, 3, 4])
        self.assertEqual("other", await mod['classify'](subject))
        self.assertEqual([], subject.reads)

    async def test_too_short_for_the_fixed_arm_reads_nothing(self):
        mod = await self.load()
        subject = Counted([1])
        self.assertEqual("other", await mod['classify'](subject))
        self.assertEqual([], subject.reads)

    async def test_matching_width_still_reads_the_elements(self):
        mod = await self.load()
        subject = Counted([1, 2])
        self.assertEqual("two", await mod['classify'](subject))
        self.assertTrue(subject.reads)


# The end.
