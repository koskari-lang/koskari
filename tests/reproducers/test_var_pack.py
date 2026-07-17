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
tests.reproducers.test_var_pack

Full-stack reproducer for bulk local loads: consecutive local loads
compile to FAST2/FAST4 rows and evaluate in source order through the
bulk handlers.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'var_pack.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestVarPack(unittest.IsolatedAsyncioTestCase):
    """
    Bulk local loads compile as packed rows and evaluate correctly
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.var_pack')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_fast2(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_fast2())
        self.assertIn(OpCode.KOS_OP_FAST2, opcodes_of(mod.verify_fast2))


    async def test_fast4(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_fast4())
        self.assertIn(OpCode.KOS_OP_FAST4, opcodes_of(mod.verify_fast4))


    async def test_fast4_fast2(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_fast4_fast2())
        ops = opcodes_of(mod.verify_fast4_fast2)
        self.assertIn(OpCode.KOS_OP_FAST4, ops)
        self.assertIn(OpCode.KOS_OP_FAST2, ops)


    async def test_eight(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_eight())
        ops = opcodes_of(mod.verify_eight)
        self.assertEqual(
            2, sum(1 for op in ops if op == OpCode.KOS_OP_FAST4))


    async def test_closure_mix(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_closure_mix())


# The end.
