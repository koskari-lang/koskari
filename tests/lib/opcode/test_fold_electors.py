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
Tests for the fold electors: an OBJECT_OPERAND or OBJECT_INPLACE site
of three or more operands elects a fold body for the operator on a
run of exact ints or exact floats, folds it left to right as the
reduce does, declines to the generic body mid-run on overflow, a zero
divisor, or an int too wide to read, falls to the generic body for a
mixed run, and returns what functools.reduce over Python's operator
returns throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import functools
import operator

from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code

import pytest


pytestmark = pytest.mark.phase_lib


# the operators the compiler emits a run for; remainder, the shifts,
# and power (whose third operand is a modulus) are binary forms
INT_OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('BIT_AND', operator.and_),
    ('BIT_OR', operator.or_), ('BIT_XOR', operator.xor),
    ('FLOOR_DIVIDE', operator.floordiv), ('TRUE_DIVIDE', operator.truediv),
]

FLOAT_OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('TRUE_DIVIDE', operator.truediv),
    ('FLOOR_DIVIDE', operator.floordiv),
]

INT_RUNS = [
    (1, 2, 3), (7, 3, 2), (-7, 3, 2), (7, -3, 2), (0, 5, 1),
    (2 ** 40, 2 ** 20, 3), (2 ** 62, 2, 2), (2 ** 100, 3, 5),
    (-(2 ** 60), 2 ** 61, 7), (1000, 3, 0), (3, 0, 4),
    (1, 2, 3, 4), (2 ** 30, 2 ** 30, 2 ** 30, 2 ** 30, 2 ** 30),
]

INF = float('inf')
NAN = float('nan')

FLOAT_RUNS = [
    (1.5, 2.0, 0.5), (-2.25, 0.5, 3.0), (1e300, 1e10, 1e10),
    (0.0, -1.0, 2.0), (7.5, -0.0, 1.0), (7.5, 1.0, 0.0),
    (INF, 1.0, 2.0), (NAN, 1.0, 2.0), (1.0, NAN, 2.0),
    (1.0, 2.0, 3.0, 4.0), (0.1, 0.2, 0.3, 0.4, 0.5),
]


def make_site(opcode, count, cache=True):
    """
    A function of count positionals reducing them at one site
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr.append([opcode, count])
    instr.append(['RETURN', 1])
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


class FoldMixin:
    """
    The checks both stack families run; the subclass names the opcode
    prefix
    """

    def opcode(self, opname):
        return self.prefix + opname


    async def check_run(self, site, pyop, run):
        try:
            expected = functools.reduce(pyop, run)
        except Exception as exc:
            with self.assertRaises(type(exc)):
                await site(*run)
            return
        result = await site(*run)
        self.assertEqual(repr(result), repr(expected), (pyop, run))
        self.assertIs(type(result), type(expected), (pyop, run))


    async def test_every_int_operator_over_runs(self):
        for opname, pyop in INT_OPERATORS:
            sites = {}
            for run in INT_RUNS * 2:
                if len(run) not in sites:
                    sites[len(run)] = make_site(self.opcode(opname), len(run))[0]
                await self.check_run(sites[len(run)], pyop, run)


    async def test_every_float_operator_over_runs(self):
        for opname, pyop in FLOAT_OPERATORS:
            sites = {}
            for run in FLOAT_RUNS * 2:
                if len(run) not in sites:
                    sites[len(run)] = make_site(self.opcode(opname), len(run))[0]
                await self.check_run(sites[len(run)], pyop, run)


    async def test_overflow_mid_run_declines(self):
        site, _ = make_site(self.opcode('MULTIPLY'), 4)
        for _ in range(3):
            self.assertEqual(await site(2, 3, 4, 5), 120)
            self.assertEqual(await site(2 ** 40, 2 ** 20, 2 ** 20, 3), 2 ** 80 * 3)
            self.assertEqual(await site(2 ** 62, 2, 2, 2), 2 ** 65)
            self.assertEqual(await site(2, 3, 4, 5), 120)


    async def test_zero_divisor_mid_run_raises(self):
        site, _ = make_site(self.opcode('FLOOR_DIVIDE'), 3)
        self.assertEqual(await site(100, 7, 3), 100 // 7 // 3)
        with self.assertRaises(ZeroDivisionError):
            await site(100, 0, 3)
        with self.assertRaises(ZeroDivisionError):
            await site(100, 7, 0)
        self.assertEqual(await site(100, 7, 3), 100 // 7 // 3)
        self.assertEqual(await site(100.0, 7.0, 3.0), 100.0 // 7.0 // 3.0)
        with self.assertRaises(ZeroDivisionError):
            await site(100.0, 7.0, 0.0)
        site, _ = make_site(self.opcode('TRUE_DIVIDE'), 3)
        self.assertEqual(await site(8.0, 2.0, 2.0), 2.0)
        with self.assertRaises(ZeroDivisionError):
            await site(8.0, 0.0, 2.0)
        self.assertEqual(await site(8.0, 2.0, 2.0), 2.0)


    async def test_int_true_divide_run_is_a_float(self):
        site, _ = make_site(self.opcode('TRUE_DIVIDE'), 3)
        for _ in range(5):
            result = await site(8, 2, 2)
            self.assertEqual(result, 2.0)
            self.assertIs(type(result), float)
            self.assertEqual(await site(7, 2, 2), 1.75)


    async def test_mixed_and_polymorphic_runs(self):
        site, _ = make_site(self.opcode('ADD'), 3)
        for i in range(30):
            self.assertEqual(await site(i, 2, 3), i + 5)
            self.assertEqual(await site(0.5, float(i), 1.0), i + 1.5)
            self.assertEqual(await site('a', str(i), 'c'), f'a{i}c')
            self.assertEqual(await site(i, 0.5, 1), i + 1.5)
            self.assertEqual(await site([i], [2], [3]), [i, 2, 3])
            result = await site(True, 1, 2)
            self.assertEqual(result, 4)
            self.assertIs(type(result), int)


    async def test_first_operand_shared_is_untouched(self):
        site, _ = make_site(self.opcode('ADD'), 3)
        seed = 2 ** 40
        for _ in range(10):
            self.assertEqual(await site(seed, 1, 2), 2 ** 40 + 3)
        self.assertEqual(seed, 2 ** 40)
        seed = 1.5
        for _ in range(10):
            self.assertEqual(await site(seed, 1.0, 2.0), 4.5)
        self.assertEqual(seed, 1.5)


    async def test_demoted_run_site_re_elects(self):
        site, _ = make_site(self.opcode('ADD'), 3)
        for _ in range(10):
            self.assertEqual(await site(1, 2, 3), 6)
            self.assertEqual(await site(1.0, 2.0, 3.0), 6.0)
        for i in range(600):
            self.assertEqual(await site(i, 2, 3), i + 5)
        self.assertEqual(await site(1.0, 2.0, 3.0), 6.0)
        self.assertEqual(await site('a', 'b', 'c'), 'abc')


    async def test_ungated_runs_match(self):
        for opname, pyop in INT_OPERATORS:
            site, _ = make_site(self.opcode(opname), 3, cache=False)
            for run in INT_RUNS:
                if len(run) == 3:
                    await self.check_run(site, pyop, run)


class TestObjectOperandFold(FoldMixin, CevalTest):
    prefix = 'OBJECT_'


    async def test_power_of_three_is_modular(self):
        site, _ = make_site('OBJECT_POWER', 3)
        for _ in range(5):
            self.assertEqual(await site(2, 10, 1000), 24)
            self.assertEqual(await site(3, 4, 5), 1)
            self.assertEqual(await site(2 ** 40, 3, 7), pow(2 ** 40, 3, 7))


class TestObjectInplaceFold(FoldMixin, CevalTest):
    prefix = 'OBJECT_INPLACE_'


# The end.
