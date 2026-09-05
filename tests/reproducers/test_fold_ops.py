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
tests.reproducers.test_fold_ops

Full-stack reproducer for the reducing forms over three or more
operands through the fold electors: every operator over runs of ints
at the digit boundaries and of floats at the special values, against
functools.reduce over Python's operator, and the runtime + over the
same run.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import functools
import operator
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'fold_ops.ki')

INTS = [
    0, 1, -1, 7, -7, 255, 256, 257, 2 ** 30 - 1, 2 ** 30, -(2 ** 30),
    2 ** 40, 2 ** 60 - 1, 2 ** 62, -(2 ** 62), 2 ** 63 - 1, -(2 ** 63),
    2 ** 64, 2 ** 100,
]

INF = float('inf')
NAN = float('nan')

FLOATS = [
    0.0, -0.0, 1.0, -1.0, 1.5, -2.25, 0.1, 3.0, 7.5, 1e300, -1e300,
    1e-300, 2.0 ** 53, INF, -INF, NAN,
]

OPERATORS = [
    ('add3', operator.add), ('sub3', operator.sub), ('mul3', operator.mul),
    ('floordiv3', operator.floordiv), ('truediv3', operator.truediv),
]

INT_ONLY = [
    ('and3', operator.and_), ('or3', operator.or_), ('xor3', operator.xor),
]


class TestFoldOps(unittest.IsolatedAsyncioTestCase):

    async def load_module(self):
        mod = new_module('tests.reproducers.fold_ops')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def check_run(self, fn, pyop, run):
        try:
            expected = functools.reduce(pyop, run)
        except Exception as exc:
            with self.assertRaises(type(exc)):
                await fn(*run)
            return
        result = await fn(*run)
        self.assertEqual(repr(result), repr(expected), (fn.__name__, run))
        self.assertIs(type(result), type(expected), (fn.__name__, run))


    async def test_int_runs(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS + INT_ONLY:
            fn = getattr(mod, name)
            for a in INTS:
                for b in INTS[::3]:
                    for c in (3, -3, 0, 2 ** 40, 2 ** 64):
                        await self.check_run(fn, pyop, (a, b, c))


    async def test_float_runs(self):
        mod = await self.load_module()
        for name, pyop in OPERATORS:
            fn = getattr(mod, name)
            for a in FLOATS:
                for b in FLOATS:
                    for c in (2.0, -0.0, NAN):
                        await self.check_run(fn, pyop, (a, b, c))


    async def test_longer_runs_and_the_runtime_operator(self):
        mod = await self.load_module()
        runs = (
            (1, 2, 3, 4, 5), (2 ** 40, 2, 3, 4, 5), (0.5, 1.5, 2.5, 3.5, 4.5),
            (2 ** 62, 2, 2, 2, 2), (1e300, 1e10, 1.0, 1.0, 1.0),
        )
        for run in runs:
            await self.check_run(mod.add5, operator.add, run)
            await self.check_run(mod.mul5, operator.mul, run)
        runs = ((1, 2, 3), (1.5, 2.5, 3.5), ("a", "b", "c"),
                (2 ** 62, 2 ** 62, 1))
        for run in runs:
            await self.check_run(mod.add3_runtime, operator.add, run)


    async def test_temporary_first_operand_is_reused(self):
        mod = await self.load_module()
        runs = ((30, 40, 5, 6), (2 ** 20, 2 ** 20, 1, 2),
                (2 ** 40, 2 ** 20, 2 ** 62, 1), (7, 1000, 7, 7))
        for a, b, c, d in runs:
            for _ in range(3):
                self.assertEqual(
                    await mod.nested_int(a, b, c, d), a * b + c + d)
        for a, b, c, d in ((30, 40, -1199, -1), (1000, 1000, -999_999, -1)):
            for _ in range(3):
                result = await mod.nested_int(a, b, c, d)
                self.assertEqual(result, 0)
                self.assertIs(result, 0)
        for a, b, c, d in ((1.5, 2.0, 0.5, 0.25), (1e300, 1e10, 1.0, 1.0)):
            for _ in range(3):
                self.assertEqual(
                    await mod.nested_float(a, b, c, d), a * b + c + d)


    async def test_in_place_run_on_an_item(self):
        mod = await self.load_module()
        runs = (
            (1, 2, 3), (1, 2 ** 62, 2 ** 62), (1.0, 0.5, 0.25),
            ("a", "b", "c"), ([1], [2], [3]),
        )
        for seed, a, b in runs:
            for _ in range(3):
                cells = await mod.item_run(seed, a, b)
                self.assertEqual(cells[0], seed + a + b)
        with self.assertRaises(TypeError):
            await mod.item_run(1, 1, "x")


    async def test_nbody_shaped_expression(self):
        mod = await self.load_module()
        for x, y, z, m in ((1.0, 2.0, 3.0, 4.0), (0.1, -0.2, 0.3, 39.47),
                           (1e-3, 1e-3, 1e-3, 1e5), (0.0, 0.0, 0.0, 1.0)):
            expected = (x * x + y * y + z * z) * m * 0.5
            for _ in range(3):
                self.assertEqual(await mod.energy(x, y, z, m), expected)


# The end.
