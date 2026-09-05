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
tests.reproducers.test_float_ops

Full-stack reproducer for float arithmetic through the electors: every
operator with a float fast path agrees with Python, bit for bit and
sign of zero included, on a grid of signed zeros, infinities, NaN, and
extremes, both with shared operands and with a temporary left operand
that the result is written into; a second reference to that temporary
is never disturbed; and an accumulating slot matches Python's sum.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import operator
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'float_ops.ki')

INF = float('inf')
NAN = float('nan')

FLOATS = [
    0.0, -0.0, 1.0, -1.0, 1.5, -2.25, 0.1, 0.2, 3.0, 7.5, -7.5,
    1e300, -1e300, 1e-300, 2.0 ** 53, INF, -INF, NAN,
]

OPERATORS = [
    ('add', operator.add), ('sub', operator.sub), ('mul', operator.mul),
    ('div', operator.truediv), ('mod', operator.mod),
    ('floordiv', operator.floordiv),
]


class TestFloatOps(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.float_ops')
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


    async def test_operators_on_shared_operands(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS:
            fn = getattr(mod, 'f_' + name)
            for a in FLOATS:
                for b in FLOATS:
                    await self.check_pair(fn, pyop, a, b)


    async def test_operators_on_a_temporary_left_operand(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS:
            fn = getattr(mod, 'f_' + name + '_t')
            for a in FLOATS:
                for b in FLOATS:
                    await self.check_pair(fn, pyop, a, b)


    async def test_second_binding_is_untouched(self):
        mod = await self.load_module()
        for a in (1.5, -2.25, 1e300):
            t, u, v = await mod.alias_binding(a)
            self.assertEqual(t, a)
            self.assertIs(t, u)
            self.assertEqual(v, a + 1.0)


    async def test_slot_update_leaves_the_alias(self):
        mod = await self.load_module()
        for a in (1.5, -2.25, 1e300):
            t, u = await mod.alias_slot(a)
            self.assertEqual(t, a + 1.0)
            self.assertEqual(u, a)


    async def test_accumulation_matches_python(self):
        mod = await self.load_module()
        e = 0.0
        for i in range(2000):
            e += 0.5 * i
            e -= i / 3.0
        self.assertEqual(await mod.accumulate(2000), e)


    async def test_mixed_site(self):
        mod = await self.load_module()
        values = [0.5, 2, 1.25, 3, 1e-3]
        self.assertEqual(await mod.mixed_site(values), sum(values, 0.0))
        self.assertEqual(await mod.mixed_site([1.0] * 300), 300.0)


# The end.
