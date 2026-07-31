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
tests.reproducers.test_compare_const_slot_bounds

Full-stack reproducer: a CHECK_COMPARE_FAST comparing a fast var
against a constant must not range-check the constant's pool index as
if it were a fast slot. The CMPSRC source bits reclassify the operand
to CONST; the native FAST-slot validator has to honor them, otherwise
a constant whose pool index exceeds the function's fast-slot count is
rejected with a spurious "FAST index N out of bounds" and the code
fails to build.

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
    os.path.dirname(__file__), 'scripts', 'compare_const_slot_bounds.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestCompareConstSlotBounds(unittest.IsolatedAsyncioTestCase):
    """
    A constant-sided CHECK_COMPARE_FAST builds and evaluates even when
    the constant's pool index is past the function's fast-slot count
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.compare_const_slot_bounds')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_builds_and_uses_check_compare_fast(self):
        # Loading compiles classify; the bug made this raise
        # UncaughtCompilerError during native Code construction.
        mod = await self.load_module()
        ops = opcodes_of(mod.classify)
        self.assertIn(OpCode.CHECK_COMPARE_FAST, ops)


    async def test_evaluates_correctly(self):
        mod = await self.load_module()
        self.assertTrue(await mod.verify())


# The end.
