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
tests.reproducers.test_abc_shapes_repro

Full-stack reproducer: a Koskari define-class hierarchy rooted at abc.ABC
with @abstractmethod methods.  Exercises abstract-property declaration,
abstract-instantiation enforcement, and a concrete subclass that chains
through (super) __init__.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import os
import unittest

from koskari.module import init_module, load_module_async, new_module

import pytest


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'abc_shapes.ki')


class TestAbcShapesReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Koskari class hierarchy inheriting abc.ABC with @abstractmethod methods.
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.abc_shapes')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_point_is_concrete(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_point())


    async def test_rectangle_super_init_and_area(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_rectangle())


    async def test_abstract_methods_registered(self):
        mod = await self.load_module()
        self.assertEqual(mod.Shape.__abstractmethods__, frozenset({'area'}))
        self.assertEqual(mod.Shape2D.__abstractmethods__, frozenset({'area'}))
        self.assertEqual(mod.Point2D.__abstractmethods__, frozenset())
        self.assertEqual(mod.Rectangle.__abstractmethods__, frozenset())


    async def test_abstract_shape_cannot_instantiate(self):
        mod = await self.load_module()
        with self.assertRaises(TypeError):
            mod.Shape()


    async def test_abstract_shape2d_cannot_instantiate(self):
        mod = await self.load_module()
        with self.assertRaises(TypeError):
            mod.Shape2D(mod.Point2D(0, 0))


    async def test_inheritance_relationships(self):
        mod = await self.load_module()
        self.assertTrue(issubclass(mod.Rectangle, mod.Shape2D))
        self.assertTrue(issubclass(mod.Rectangle, mod.Shape))
        self.assertTrue(issubclass(mod.Point2D, mod.Shape))
        rect = mod.Rectangle(mod.Point2D(0, 0), 2, 3)
        self.assertIsInstance(rect, mod.Shape)
        self.assertIsInstance(rect, mod.Shape2D)
        self.assertEqual(rect.area, 6)


# The end.
