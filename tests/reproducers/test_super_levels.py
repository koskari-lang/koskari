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
(super) resolves to the defining class at every level of a hierarchy.

The zero-argument form used to expand to super(type(self), self), which
in a three-level hierarchy resolved from the instance's class every
time and recursed forever. It now reads __class__, which the class body
binds to the class object once it exists.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'super_levels.ki')


class TestSuperLevels(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.super_levels')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_four_levels(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_levels())


    async def test_three_levels(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_c_is_three_levels())


    async def test_class_is_the_defining_class(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_class_is_defining_class())


    async def test_super_through_an_inherited_method(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_widen_chain())


    async def test_super_from_a_nested_closure_is_the_defining_class(self):
        mod = await self.load_module()
        self.assertFalse(await mod.verify_super_from_nested_closure())


    async def test_explicit_form_unchanged(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_explicit_form())


# The end.
