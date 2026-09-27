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
tests.reproducers.test_join_constants

Full-stack reproducer for join-constant elision: an if whose arms all
push None into a loop's continue or a discarding pop compiles without
those constants, while an arm that pushes a call result keeps the pop.

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
    os.path.dirname(__file__), 'scripts', 'join_constants.ki')


def rows_of(fn):
    code = fn.code
    return [(OpCode(code.oparg(i)[0]), code.oparg(i)[1])
            for i in range(code.length())]


def const_values(fn):
    code = fn.code
    return [code.constant(arg) for op, arg in rows_of(fn)
            if op == OpCode.CONST]


class TestJoinConstants(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.join_constants')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod

    async def test_loop_arms_continue_without_none(self):
        mod = await self.load_module()
        rows = rows_of(mod.branchy)
        self.assertIn((OpCode.CONTINUE, 0), rows)
        self.assertNotIn((OpCode.CONTINUE, 1), rows)
        self.assertNotIn(None, const_values(mod.branchy))
        expected = sum(
            1 if i % 3 == 0 else 10 if i % 5 == 0 else 100
            for i in range(30))
        self.assertEqual(await mod.branchy(30), expected)

    async def test_discarded_if_value_has_no_pop(self):
        mod = await self.load_module()
        rows = rows_of(mod.discarded)
        self.assertNotIn(OpCode.POP, [op for op, _ in rows])
        self.assertNotIn(None, const_values(mod.discarded))
        self.assertEqual(await mod.discarded(True), 7)
        self.assertEqual(await mod.discarded(False), 7)

    async def test_call_arm_keeps_pop(self):
        mod = await self.load_module()
        rows = rows_of(mod.kept)
        self.assertIn((OpCode.POP, 1), rows)
        calls = []
        self.assertEqual(await mod.kept(lambda: calls.append(1), True), 7)
        self.assertEqual(await mod.kept(lambda: calls.append(1), False), 7)
        self.assertEqual(calls, [1])


# The end.
