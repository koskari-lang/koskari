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
tests.reproducers.test_attr_cache_foreign_descriptor

A __slots__ member descriptor reached from a type that is not a
subtype of the descriptor's own type describes another layout. An
attribute site raises the TypeError CPython raises, on the first
access and on every cached one after it, and never touches the
receiver's memory.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'attr_cache_foreign_descriptor.ki')


class TestAttrCacheForeignDescriptor(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('attr_cache_foreign_descriptor')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_read_through_a_foreign_slot_raises_every_time(self):
        ns = await self.load()
        b = ns['B']()
        for _ in range(4):
            with self.assertRaises(TypeError):
                await ns['read'](b)

    async def test_write_through_a_foreign_slot_raises_every_time(self):
        ns = await self.load()
        bs = [ns['B']() for _ in range(8)]
        for b in bs:
            with self.assertRaises(TypeError):
                await ns['write'](b, object())
        for b in bs:
            self.assertIs(type(b), ns['B'])


# The end.
