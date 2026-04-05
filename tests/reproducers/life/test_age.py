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
tests.reproducers.life

Full-stack reproducer: load sibling life.ki, build a 5x5 LifeBoard,
randomize, run one age step.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""


import os
import unittest

from koskari.importer.module import init_module, load_module_async, new_module
from koskari.dis import dis

import pytest


pytestmark = pytest.mark.phase_reproducers

_LIFE_KI = os.path.join(os.path.dirname(__file__), 'life.ki')


class TestBadGeneratorReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Reproducer for a bad generator
    """

    async def load_module(self):
        mod = await new_module('tests.reproducers.life_repro')
        init_module(mod, os.path.abspath(_LIFE_KI))
        await load_module_async(mod)
        return mod


    async def test_bad_generator(self):
        mod = await self.load_module()

        print("==== running bad_generator ====")
        dis(mod.bad_generator)
        result = [x async for x in mod.bad_generator(None)]

        self.assertEqual(result, [])


    async def test_use_bad_generator(self):
        mod = await self.load_module()

        print("==== running use_bad_generator ====")
        dis(mod.use_bad_generator)
        result = await mod.use_bad_generator()

        self.assertEqual(result, None)


    async def test_foreach_return(self):
        mod = await self.load_module()

        print("==== running working_return ====")
        dis(mod.working_return)
        result = await mod.working_return()

        self.assertEqual(result, 0)


# The end.
