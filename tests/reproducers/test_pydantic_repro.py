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
tests.reproducers.test_pydantic_repro

Full-stack reproducer: class subclassing pydantic.BaseModel, keyword
argument construction, and field access.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

pydantic = pytest.importorskip("pydantic")

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'pydantic_model.ki')


class TestPydanticModelReproducer(unittest.IsolatedAsyncioTestCase):
    """
    Pydantic BaseModel subclass defined in Koskari, instantiated with
    keyword arguments, field values readable as attributes.
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.pydantic_model')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_pydantic_model_field_access(self):
        mod = await self.load_module()
        ok = await mod.verify_pydantic_model()
        self.assertTrue(ok)


# The end.
