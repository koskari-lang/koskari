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
tests.reproducers.test_call_inlining

Full-stack reproducer for __call__ inlining: calling a Koskari instance whose
__call__ is a KosCallable dispatches through the install slot (the
kos_instance_call sentinel + KosContext_Call divert) instead of re-entering
the evaluator via tp_call.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'call_inlining.ki')


class TestCallInlining(unittest.IsolatedAsyncioTestCase):
    """
    Calling an instance with a Koskari __call__ returns the right value through
    the inline dispatch path.
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.call_inlining')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_call_inlines(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_call_inlines())


    async def test_call_multiple(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_call_multiple())


    async def test_call_keywords(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_call_keywords())


    async def test_nested_call(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_nested_call())


# The end.
