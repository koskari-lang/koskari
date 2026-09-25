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
tests.reproducers.test_pyiter_flatten

Full-stack reproducer: Koskari iteration through a KosPyIter flattens
into the running evaluator and applies demand only when the consumer
is itself on the demand path.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.lib import KoskariSyncViolation, demand_call
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'pyiter_flatten.ki')


class TestPyIterFlatten(unittest.IsolatedAsyncioTestCase):
    """
    Koskari for-each and yield-from over (iter (gen))
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.pyiter_flatten')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_bare_generator_sees_parameterize(self):
        mod = await self.load_module()
        self.assertEqual(await vars(mod)['consume-bare'](), [10, 10])


    async def test_pyiter_sees_parameterize(self):
        mod = await self.load_module()
        self.assertEqual(await vars(mod)['consume-iter'](), [10, 10])


    async def test_pyiter_yield_from_sees_parameterize(self):
        mod = await self.load_module()
        self.assertEqual(await vars(mod)['consume-relay'](), [10, 10])


    async def test_pyiter_suspends_off_demand_path(self):
        mod = await self.load_module()
        self.assertEqual(await vars(mod)['consume-sleeper'](), [1])


    async def test_pyiter_refuses_suspension_on_demand_path(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            demand_call(vars(mod)['consume-sleeper'])


    async def test_pyiter_from_python_unchanged(self):
        mod = await self.load_module()
        self.assertEqual(list(iter(vars(mod)['gen']())), [1, 1])
        with self.assertRaises(KoskariSyncViolation):
            list(iter(vars(mod)['sleeper']()))


# The end.
