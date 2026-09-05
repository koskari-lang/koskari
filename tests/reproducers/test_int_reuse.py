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
tests.reproducers.test_int_reuse

Full-stack reproducer for in-place int results: every operator applied
to a temporary left operand agrees with Python across the digit grid,
a second reference to the would-be target is never disturbed, and a
result in the small-int range keeps the cached identity.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import operator
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module

from tests.reproducers.test_int_digits import DIGIT_EDGE_VALUES, SHIFT_COUNTS


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'int_reuse.ki')


OPERATORS = [
    ('kos_add_t', operator.add), ('kos_sub_t', operator.sub),
    ('kos_mul_t', operator.mul), ('kos_mod_t', operator.mod),
    ('kos_floordiv_t', operator.floordiv), ('kos_truediv_t', operator.truediv),
    ('kos_and_t', operator.and_), ('kos_or_t', operator.or_),
    ('kos_xor_t', operator.xor),
]


class TestIntReuse(unittest.IsolatedAsyncioTestCase):
    """
    Results written into a temporary operand match Python and leave
    every other reference alone
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.int_reuse')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def check_pair(self, fn, pyop, a, b):
        try:
            expected = pyop(a, b)
        except Exception as exc:
            with self.assertRaises(type(exc)):
                await fn(a, b)
            return
        result = await fn(a, b)
        self.assertEqual(repr(result), repr(expected), (fn.__name__, a, b))
        self.assertIs(type(result), type(expected), (fn.__name__, a, b))


    async def test_operators_on_a_temporary_left_operand(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS:
            fn = getattr(mod, name)
            for a in DIGIT_EDGE_VALUES:
                for b in DIGIT_EDGE_VALUES:
                    await self.check_pair(fn, pyop, a, b)


    async def test_shifts_and_powers_on_a_temporary(self):
        mod = await self.load_module()
        for a in DIGIT_EDGE_VALUES:
            for b in SHIFT_COUNTS + [-1]:
                await self.check_pair(mod.kos_lshift_t, operator.lshift, a, b)
                await self.check_pair(mod.kos_rshift_t, operator.rshift, a, b)
            for b in (0, 1, 2, 3, 5, 7, -1):
                await self.check_pair(mod.kos_pow_t, operator.pow, a, b)


    async def test_second_binding_is_untouched(self):
        mod = await self.load_module()
        for a in (300, 2 ** 40, -(2 ** 40), 2 ** 61):
            t, u, v = await mod.alias_binding(a)
            self.assertEqual(t, a)
            self.assertEqual(u, a)
            self.assertIs(t, u)
            self.assertEqual(v, a + 1)


    async def test_list_element_is_untouched(self):
        mod = await self.load_module()
        for a in (300, 2 ** 40, -(2 ** 40)):
            xs, y = await mod.alias_list(a)
            self.assertEqual(xs, [a])
            self.assertEqual(y, a + 1)


    async def test_slot_update_leaves_the_alias(self):
        mod = await self.load_module()
        for a in (300, 2 ** 40, -(2 ** 40)):
            t, u = await mod.alias_slot(a)
            self.assertEqual(t, a + 1)
            self.assertEqual(u, a)


    async def test_chained_accumulation(self):
        mod = await self.load_module()
        for a, n in ((300, 500), (2 ** 40, 500), (7, 2000)):
            acc = a
            for i in range(n):
                acc = (acc * 3 + i) & 0xffffffff
            self.assertEqual(await mod.chained(a, n), acc)


    async def test_small_result_keeps_the_cached_identity(self):
        mod = await self.load_module()
        self.assertIs(await mod.small_result(300), 44)
        self.assertIs(await mod.small_result(256), 0)
        self.assertIs(await mod.small_result(251), -5)


# The end.
