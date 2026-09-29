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
tests.reproducers.test_readonly_member_store

A cached attribute store on a C type whose member descriptor is
READONLY must raise the way setattr does, not write the raw field.
The range type declares start, stop and step as READONLY object
members with inherited generic setattr, so it is the subject.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'readonly_member_store.ki')


class TestReadonlyMemberStore(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.readonly_member_store')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_set_attr_raises_and_leaves_the_range_alone(self):
        mod = await self.load_module()
        r = range(5)
        for _ in range(3):
            with self.assertRaises(AttributeError):
                await mod.poke(r, 7)
        self.assertEqual(r.start, 0)
        self.assertEqual(list(r), [0, 1, 2, 3, 4])
        self.assertEqual(await mod.peek(r), 0)


    async def test_inplace_attr_raises_and_leaves_the_range_alone(self):
        mod = await self.load_module()
        r = range(5)
        for _ in range(3):
            with self.assertRaises(AttributeError):
                await mod.bump(r, 1)
        self.assertEqual(r.start, 0)
        self.assertEqual(list(r), [0, 1, 2, 3, 4])


# The end.
