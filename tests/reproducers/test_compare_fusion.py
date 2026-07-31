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
tests.reproducers.test_compare_fusion

Full-stack reproducer for comparison fusion: loop heads compile to a
single CHECK_COMPARE_FAST dispatch, the bounds idiom compiles to
OBJECT_COMPARE_FAST, and results stay correct through the fused paths.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'compare_fusion.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestCompareFusion(unittest.IsolatedAsyncioTestCase):
    """
    Comparison fusion compiles the documented shapes and evaluates
    correctly
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.compare_fusion')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_loop_head_is_one_dispatch(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_loop())
        ops = opcodes_of(mod.count_below)
        self.assertIn(OpCode.CHECK_COMPARE_FAST, ops)
        self.assertNotIn(OpCode.CHECK_COMPARE, ops)


    async def test_bounds_branch_splits_to_bool_and_jump(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_bounds())
        ops = opcodes_of(mod.bounded)
        self.assertIn(OpCode.OBJECT_COMPARE_FAST, ops)
        self.assertIn(OpCode.JUMP_FALSE, ops)
        self.assertNotIn(OpCode.CHECK_COMPARE, ops)


    async def test_bounds_value_is_one_dispatch(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify_bounds_value())
        ops = opcodes_of(mod.bounded_value)
        self.assertEqual(ops, [
            OpCode.OBJECT_COMPARE_FAST,
            OpCode.RETURN,
        ])


# The end.
