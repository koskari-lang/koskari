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
tests.reproducers.test_inplace_electors

Full-stack reproducer for the in-place electors: the augmented
assignment forms as the compiler emits them (a local counter, a closure
cell, a list element, a site fed several types) return the same values
as the arithmetic done in Python.

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
    os.path.dirname(__file__), 'scripts', 'inplace_electors.ki')


def opcodes_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class TestInplaceElectors(unittest.IsolatedAsyncioTestCase):
    """
    Augmented assignments through the electors match Python
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.inplace_electors')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_local_counters(self):
        mod = await self.load_module()
        self.assertIn(OpCode.FAST_INPLACE_CONST, opcodes_of(mod.count_up))
        self.assertIn(OpCode.FAST_INPLACE, opcodes_of(mod.count_up))
        for n, step in ((0, 1), (1, 1), (1000, 3), (100, 2 ** 40)):
            self.assertEqual(await mod.count_up(n, step), n * step)
        self.assertEqual(await mod.count_down(500), 500)
        self.assertEqual(await mod.count_up(3, 2 ** 62), 3 * 2 ** 62)


    async def test_closure_cell_float_and_repeat(self):
        mod = await self.load_module()
        for n in (0, 1, 3, 300):
            self.assertEqual(
                await mod.closure_float(n), 0.5 + sum(i * 0.25 for i in range(n)))
            self.assertEqual(await mod.closure_repeat(n), 1000 - 2 * n)


    async def test_constant_ref_refuses_the_update(self):
        mod = await self.load_module()
        # the seed is doubled inside, so the ref alone holds the value
        for seed in (0, 7, 1000, 2 ** 40, 1.5, 1e300):
            for step in (1, 3, 0.5, 2 ** 40):
                for _ in range(3):
                    with self.assertRaises(ValueError):
                        await mod.constant_incr(seed, step)
                    self.assertEqual(
                        await mod.constant_value_survives(seed, step),
                        seed + seed)
            for _ in range(3):
                with self.assertRaises(ValueError):
                    await mod.constant_incr_by_one(seed)


    async def test_closure_cell(self):
        mod = await self.load_module()
        self.assertEqual(await mod.closure_counter(100), sum(range(100)))
        self.assertEqual(await mod.closure_counter(0), 0)


    async def test_list_element(self):
        mod = await self.load_module()
        self.assertIn(OpCode.OBJECT_INPLACE, opcodes_of(mod.item_accumulate))
        self.assertEqual(
            await mod.item_accumulate(50), [sum(range(50)), 100])


    async def test_mixed_site(self):
        mod = await self.load_module()
        ints = list(range(20))
        floats = [0.5] * 20
        self.assertEqual(await mod.mixed_site(ints), sum(ints))
        self.assertEqual(await mod.mixed_site(floats), 10.0)
        self.assertEqual(await mod.mixed_site(ints + floats), sum(ints) + 10.0)
        self.assertEqual(await mod.mixed_site(ints), sum(ints))
        with self.assertRaises(TypeError):
            await mod.mixed_site(['a', 'b'])


    async def test_unbound_accumulator_after_election(self):
        """
        A site elected on a bound int raises NameError when the slot is
        unbound on a later call, and serves the next bound call
        """

        mod = await self.load_module()
        self.assertEqual(await mod.unbound_after(True), 11)
        with self.assertRaises(NameError):
            await mod.unbound_after(False)
        self.assertEqual(await mod.unbound_after(True), 11)


    async def test_masked_accumulate(self):
        mod = await self.load_module()
        acc = 1
        for i in range(300):
            acc = (acc * 3) & 0xffffffff
            acc += i
        self.assertEqual(await mod.masked_accumulate(300), acc)


# The end.
