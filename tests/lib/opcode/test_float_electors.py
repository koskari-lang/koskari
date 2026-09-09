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
Tests for the float electors: the arithmetic and compare families
elect their float body on the first exact-float pair, agree with
Python on a grid that includes the signed zeros, the infinities, NaN,
and the extremes, fall to the generic body for a zero divisor, mixed
types, and the operators without a float path, and re-elect after a
demotion.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

import pytest

from koskari.lib import Compare, FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount


pytestmark = pytest.mark.phase_lib


INF = float('inf')
NAN = float('nan')

FLOATS = [
    0.0, -0.0, 1.0, -1.0, 1.5, -2.25, 0.1, 0.2, 3.0, 7.5, -7.5,
    1e300, -1e300, 1e-300, 2.0 ** 53, INF, -INF, NAN,
]

OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('TRUE_DIVIDE', operator.truediv),
    ('MODULO', operator.mod), ('FLOOR_DIVIDE', operator.floordiv),
    ('POWER', operator.pow), ('BIT_AND', operator.and_),
]

SELECTORS = [
    ('LT', operator.lt), ('LE', operator.le), ('EQ', operator.eq),
    ('NE', operator.ne), ('GT', operator.gt), ('GE', operator.ge),
]


def same(result, expected):
    return repr(result) == repr(expected) and type(result) is type(expected)


def make_pair_site(opname, cache=True):
    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['OBJECT_' + opname, 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


def make_inplace_site(opname):
    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['OBJECT_INPLACE_' + opname, 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


def make_fast_site(opname):
    code = build_code([
        ['FAST', 1],
        ['FAST_INPLACE', (opcount(Operand[opname], 1), 0, 0, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


def make_const_site(opname, const):
    code = build_code([
        ['FAST_INPLACE_CONST', (int(Operand[opname]), 0, const, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a',), name='site')
    nspec = NamespaceSpec(None, ('a',), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))


def make_check_site(selname):
    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['CHECK_COMPARE', (int(Compare[selname]), 2, 'fail', 0)],
        ['CONST', 1],
        ['RETURN', 1],
        ['LABEL', 'fail'],
        ['CONST', 0],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


def make_object_compare_site(selname):
    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['OBJECT_COMPARE', (int(Compare[selname]) << 8) | 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


class FloatGridMixin:

    async def check(self, site, pyop, a, b, label):
        try:
            expected = pyop(a, b)
        except Exception as exc:
            with self.assertRaises(type(exc)):
                await site(a, b)
            return
        result = await site(a, b)
        self.assertTrue(same(result, expected), (label, a, b, result, expected))


    async def sweep(self, make, opname, pyop):
        site = make(opname)
        for a in FLOATS:
            for b in FLOATS:
                await self.check(site, pyop, a, b, opname)


class TestPairFloatElector(FloatGridMixin, CevalTest):

    async def test_every_operator_on_the_grid(self):
        for opname, pyop in OPERATORS:
            await self.sweep(make_pair_site, opname, pyop)


    async def test_inplace_every_operator_on_the_grid(self):
        for opname, pyop in OPERATORS:
            await self.sweep(make_inplace_site, opname, pyop)


    async def test_float_site_repeats(self):
        site = make_pair_site('MULTIPLY')
        for i in range(30):
            self.assertTrue(same(await site(i * 0.5, 2.0), i * 0.5 * 2.0))


    async def test_mixed_pair_at_a_float_site(self):
        site = make_pair_site('ADD')
        self.assertTrue(same(await site(1.5, 2.5), 4.0))
        self.assertTrue(same(await site(1.5, 2), 3.5))
        self.assertTrue(same(await site(2, 1.5), 3.5))
        self.assertTrue(same(await site(1.5, 2.5), 4.0))


    async def test_polymorphic_site_serves_every_type(self):
        site = make_pair_site('ADD')
        for i in range(30):
            self.assertTrue(same(await site(float(i), 0.5), i + 0.5))
            self.assertTrue(same(await site(i, 1), i + 1))
            self.assertEqual(await site('a', 'b'), 'ab')


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site = make_pair_site('SUBTRACT')
        for _ in range(10):
            self.assertTrue(same(await site(1.5, 0.5), 1.0))
            self.assertTrue(same(await site(3, 1), 2))
        for i in range(600):
            self.assertTrue(same(await site(float(i), 0.5), i - 0.5))
        self.assertTrue(same(await site(3, 1), 2))


    async def test_zero_divisor_raises_through_the_generic_body(self):
        for opname in ('TRUE_DIVIDE', 'MODULO', 'FLOOR_DIVIDE'):
            site = make_pair_site(opname)
            self.assertTrue(same(await site(7.5, 2.0), OPERATORS_BY_NAME[opname](7.5, 2.0)))
            with self.assertRaises(ZeroDivisionError):
                await site(7.5, 0.0)
            with self.assertRaises(ZeroDivisionError):
                await site(7.5, -0.0)
            self.assertTrue(same(await site(7.5, 2.0), OPERATORS_BY_NAME[opname](7.5, 2.0)))


    async def test_ungated_site_matches(self):
        site = make_pair_site('MULTIPLY', cache=False)
        for a in FLOATS:
            self.assertTrue(same(await site(a, 2.0), a * 2.0))


OPERATORS_BY_NAME = dict(OPERATORS)


class TestSlotFloatElectors(FloatGridMixin, CevalTest):

    async def test_fast_inplace_every_operator_on_the_grid(self):
        for opname, pyop in OPERATORS:
            await self.sweep(make_fast_site, opname, pyop)


    async def test_fast_inplace_const_every_operator_on_the_grid(self):
        for opname, pyop in OPERATORS:
            for b in FLOATS:
                site = make_const_site(opname, b)
                for a in FLOATS:
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(a)
                        continue
                    result = await site(a)
                    self.assertTrue(same(result, expected), (opname, a, b, result, expected))


    async def test_accumulator_repeats(self):
        site = make_fast_site('ADD')
        for i in range(30):
            self.assertTrue(same(await site(float(i), 0.25), i + 0.25))
        const = make_const_site('MULTIPLY', 0.5)
        for i in range(30):
            self.assertTrue(same(await const(float(i)), i * 0.5))


    async def test_int_accumulator_at_a_float_const_site(self):
        site = make_const_site('ADD', 0.5)
        self.assertTrue(same(await site(1.5), 2.0))
        self.assertTrue(same(await site(2), 2.5))
        self.assertTrue(same(await site(1.5), 2.0))


class TestCompareFloatElectors(CevalTest):

    async def test_check_compare_on_the_grid(self):
        for selname, pyop in SELECTORS:
            site = make_check_site(selname)
            for a in FLOATS:
                for b in FLOATS:
                    self.assertEqual(await site(a, b), int(pyop(a, b)), (selname, a, b))


    async def test_object_compare_on_the_grid(self):
        for selname, pyop in SELECTORS:
            site = make_object_compare_site(selname)
            for a in FLOATS:
                for b in FLOATS:
                    self.assertIs(await site(a, b), pyop(a, b), (selname, a, b))


    async def test_nan_compares_false_but_not_equal(self):
        site = make_object_compare_site('NE')
        self.assertIs(await site(NAN, NAN), True)
        site = make_object_compare_site('EQ')
        self.assertIs(await site(NAN, NAN), False)
        site = make_check_site('LT')
        self.assertEqual(await site(NAN, 1.0), 0)
        self.assertEqual(await site(1.0, NAN), 0)


    async def test_mixed_pair_at_a_float_compare_site(self):
        site = make_check_site('LT')
        self.assertEqual(await site(1.5, 2.5), 1)
        self.assertEqual(await site(1, 2.5), 1)
        self.assertEqual(await site(2.5, 2), 0)
        self.assertEqual(await site(1.5, 2.5), 1)


# The end.
