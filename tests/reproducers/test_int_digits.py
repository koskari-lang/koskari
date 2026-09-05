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
tests.reproducers.test_int_digits

Full-stack reproducer for the int fast paths: every arithmetic and
comparison operator evaluated through its opcode agrees with Python
across the digit boundaries of the inline int read (one 30-bit digit,
two digits, the long long limit, and beyond), including the results
that overflow the fast path and the errors both sides raise.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import operator
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'int_digits.ki')


DIGIT_EDGE_VALUES = [
    0, 1, -1, 7, -7,
    2 ** 30 - 1, 2 ** 30, -(2 ** 30), 2 ** 30 + 1,
    2 ** 31 - 1, 2 ** 32 - 1, -(2 ** 32),
    2 ** 60 - 1, 2 ** 60, -(2 ** 60), -(2 ** 60) + 1,
    2 ** 62, 2 ** 63 - 1, -(2 ** 63), 2 ** 63, 2 ** 64, 2 ** 100,
]

SHIFT_COUNTS = [0, 1, 3, 29, 30, 31, 33, 60, 62, 63, 64, 100]

OPERATORS = [
    ('kos_add', operator.add), ('kos_sub', operator.sub),
    ('kos_mul', operator.mul), ('kos_mod', operator.mod),
    ('kos_floordiv', operator.floordiv), ('kos_truediv', operator.truediv),
    ('kos_and', operator.and_), ('kos_or', operator.or_),
    ('kos_xor', operator.xor),
    ('kos_lt', operator.lt), ('kos_le', operator.le),
    ('kos_eq', operator.eq), ('kos_ne', operator.ne),
    ('kos_gt', operator.gt), ('kos_ge', operator.ge),
]


class TestIntDigits(unittest.IsolatedAsyncioTestCase):
    """
    Opcode arithmetic on ints matches Python at the digit boundaries
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.int_digits')
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


    async def test_operators_at_digit_edges(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS:
            fn = getattr(mod, name)
            for a in DIGIT_EDGE_VALUES:
                for b in DIGIT_EDGE_VALUES:
                    await self.check_pair(fn, pyop, a, b)


    async def test_powers_at_digit_edges(self):
        mod = await self.load_module()
        for a in DIGIT_EDGE_VALUES:
            for b in (0, 1, 2, 3, 5, 7, -1, -2):
                await self.check_pair(mod.kos_pow, operator.pow, a, b)


    async def test_shifts_at_digit_edges(self):
        mod = await self.load_module()
        for a in DIGIT_EDGE_VALUES:
            for b in SHIFT_COUNTS + [-1]:
                await self.check_pair(mod.kos_lshift, operator.lshift, a, b)
                await self.check_pair(mod.kos_rshift, operator.rshift, a, b)


# The end.
