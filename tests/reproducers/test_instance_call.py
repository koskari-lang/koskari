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
tests.reproducers.test_instance_call

Full-stack reproducer: an instance whose __call__ is a KosCallable is
called with CPython's binding rule. A function bound as __call__ takes
the instance as its receiver; a partial bound as __call__ is not a
descriptor and is called without it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'instance_call.ki')


class TestInstanceCall(unittest.IsolatedAsyncioTestCase):
    """
    Instance calls from Koskari and from Python
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.instance_call')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_method_from_koskari(self):
        mod = await self.load_module()
        self.assertEqual(await mod.method_from_koskari(), ("m", 2))


    async def test_method_from_python(self):
        mod = await self.load_module()
        m = mod.Method()
        m.tag = "p"
        self.assertEqual(await m(2), ("p", 2))


    async def test_bound_from_koskari(self):
        mod = await self.load_module()
        self.assertEqual(await mod.bound_from_koskari(), (1, 2))


    async def test_bound_in_tail(self):
        mod = await self.load_module()
        self.assertEqual(await mod.bound_in_tail(), (1, 3))


    async def test_bound_from_python(self):
        mod = await self.load_module()
        self.assertEqual(await mod.Bound()(2), (1, 2))


# The end.
