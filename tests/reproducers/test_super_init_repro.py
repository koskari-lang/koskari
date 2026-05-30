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
tests.reproducers.test_super_init_repro

Full-stack reproducer: layout define-class, subclass calls superclass
__init__ via (super) with arguments.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import os
import unittest

from koskari.module import init_module, load_module_async, new_module

import pytest


pytestmark = pytest.mark.phase_reproducers

_SUPER_INIT_KI = os.path.join(os.path.dirname(__file__), 'scripts', 'super_init.ki')


class TestSuperInitReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Subclass passes constructor arguments through to the base __init__.
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.super_init_repro')
        init_module(mod, os.path.abspath(_SUPER_INIT_KI))
        await load_module_async(mod)
        return mod


    async def test_layout_subclass_super_init_chain(self):
        mod = await self.load_module()
        ok = await mod.verify_super_init()
        self.assertTrue(ok)


# The end.
