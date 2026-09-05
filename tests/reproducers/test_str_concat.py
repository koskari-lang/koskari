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
tests.reproducers.test_str_concat

Full-stack reproducer for string concatenation through the electors:
accumulators grown by incr! in every home, the reducing forms of +
over three and five operands, a nested + with a temporary left, a
value the outside still holds, and #str through the opcode and through
its runtime binding, each against the same concatenation in Python over
every string kind.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import unittest

import pytest

from koskari.lib import build_str
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'str_concat.ki')

PIECES = ["", "ab", "caf\xe9", "中文", "\U0001f600!", "x" * 100]


class TestStrConcat(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.str_concat')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_accumulators_in_every_home(self):
        mod = await self.load_module()
        grows = (mod.grow, mod.grow_captured, mod.grow_dict, mod.grow_slots)
        for grow in grows:
            for piece in PIECES:
                for n in (0, 1, 2, 300, 2000):
                    result = await grow(n, piece)
                    self.assertEqual(result, piece * n, (grow, piece, n))
                    self.assertIs(type(result), str)


    async def test_reducing_forms(self):
        mod = await self.load_module()
        for a in PIECES:
            for b in PIECES:
                for c in ("", "z", "\U0001f600"):
                    self.assertEqual(await mod.join3(a, b, c), a + b + c)
                    self.assertEqual(await mod.nested(a, b, c), a + b + c)
                    self.assertEqual(await mod.build3(a, b, c), a + b + c)
                    self.assertEqual(
                        await mod.join5(a, b, c, b, a), a + b + c + b + a)


    async def test_str_builder_through_its_runtime_binding(self):
        mod = await self.load_module()
        self.assertIs(await mod.build_value(), build_str)
        for a in PIECES:
            for b in PIECES:
                rest = (b, 1, None, a)
                expected = a + b + "1None" + a
                self.assertEqual(await mod.build_splat(a, rest), expected)
                self.assertEqual(await mod.build_apply((a,) + rest), expected)
                self.assertIs(type(await mod.build_apply((a, b))), str)


    async def test_non_str_operands(self):
        mod = await self.load_module()
        with self.assertRaises(TypeError):
            await mod.join3("a", 1, "c")
        with self.assertRaises(TypeError):
            await mod.nested("a", "b", 1)
        self.assertEqual(await mod.build3("a", 1, None), "a1None")


    async def test_shared_value_is_untouched(self):
        mod = await self.load_module()
        for piece in PIECES:
            seed = "seed" + piece
            before, after = await mod.shared(seed, piece)
            self.assertEqual(before, "seed" + piece)
            self.assertIs(before, seed)
            self.assertEqual(after, "seed" + piece * 3)


# The end.
