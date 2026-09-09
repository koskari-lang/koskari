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
tests.reproducers.test_class_hierarchy_repro

Full-stack reproducers for class construction across inheritance shapes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module
from tests.reproducers.helpers import Animal, PlainBase


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'class_hierarchy.ki')


class TestClassHierarchyReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Class construction across diverse base-class paths
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.class_hierarchy')
        ns = vars(mod)
        ns['PlainBase'] = PlainBase
        ns['Animal'] = Animal
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_kos_inherits_kos(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_kos_inherits_kos())


    async def test_kos_inherits_python(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_kos_inherits_python())


    async def test_kos_inherits_intenum(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_kos_inherits_intenum())


    async def test_kos_inherits_abc(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_kos_inherits_abc())


    async def test_dataclass(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_dataclass())


    async def test_python_inherits_kos(self):
        mod = await self.load_module()
        PyChild = type('PyChild', (mod.KosBase,), {})
        obj = PyChild(42)
        self.assertEqual(obj.x, 42)


class TestPydanticHierarchyReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Pydantic BaseModel subclass remains covered alongside hierarchy reproducers
    """

    pydantic = pytest.importorskip('pydantic')

    _SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'pydantic_model.ki')


    async def load_module(self):
        mod = new_module('tests.reproducers.pydantic_model')
        init_module(mod, os.path.abspath(self._SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_kos_inherits_pydantic(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_pydantic_model())


# The end.
