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
tests.reproducers.test_procedure_install

Full-stack reproducer: a Procedure-wrapped __init__ called from Koskari
installs into the calling evaluator instead of nesting a new one.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import unittest

import pytest

from koskari.lib import KoskariSyncViolation
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'procedure_install.ki')


async def async_init(self):
    """
    An __init__ body that suspends
    """

    await asyncio.sleep(0)


class TestProcedureInstall(unittest.IsolatedAsyncioTestCase):
    """
    A class __init__ invoked from Koskari bytecode
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.procedure_install')
        vars(mod)['async_init'] = async_init
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_result_is_none(self):
        mod = await self.load_module()
        self.assertEqual(await mod.init_result(), (1, None, 7))


    async def test_runaway_recursion_raises(self):
        mod = await self.load_module()
        with self.assertRaises(RecursionError):
            await mod.runaway()


    async def test_tail_delegation_costs_one_frame_per_level(self):
        """
        An __init__ delegating to another __init__ in tail position
        keeps one frame per level, the wrapper's continuation that
        replaces the return with None, as the lambda form of that
        wrapper would; the caller's frame is reused for it, not kept
        beneath it
        """

        mod = await self.load_module()
        self.assertIsNone(await mod.chain(800))
        with self.assertRaises(RecursionError):
            await mod.chain(1200)


    async def test_suspension_refused(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            await mod.sleeper()


    async def test_raise_propagates(self):
        mod = await self.load_module()
        with self.assertRaisesRegex(ValueError, "boom"):
            await mod.raiser()


    async def test_frameless_result_is_none(self):
        mod = await self.load_module()
        self.assertEqual(await mod.partial_result(), (5, None, 5))


    async def test_frameless_suspension_refused(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            await mod.async_partial()


    async def test_demand_escaping_continuation(self):
        mod = await self.load_module()
        self.assertEqual(await mod.demand_escape(), 5)


    async def test_demand_composable_continuation_twice(self):
        mod = await self.load_module()
        self.assertEqual(await mod.demand_shift_twice(), 6)


    async def test_composable_continuation_across_boundary(self):
        mod = await self.load_module()
        self.assertEqual(await mod.map_shift_boundary(), [2, 4])


    async def test_composable_continuation_outside_delimiter(self):
        mod = await self.load_module()
        self.assertEqual(await mod.escaped_shift_later(), (10, 10, [2, 4]))


    async def test_map_composable_continuation(self):
        mod = await self.load_module()
        self.assertEqual(await mod.map_shift_native(), [2, 4])


# The end.
