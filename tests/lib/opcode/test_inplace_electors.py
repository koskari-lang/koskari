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
Tests for the in-place electors: OBJECT_INPLACE, FAST_INPLACE, and
FAST_INPLACE_CONST sites elect their operator's int body on the first
exact-int operands they see, fall to the generic body for operands the
int fast path declines, count type-guard misses toward a demotion, and
return the same results as the ladder alone throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount

import pytest


pytestmark = pytest.mark.phase_lib


OPERATORS = [
    ('ADD', operator.add),
    ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul),
    ('MODULO', operator.mod),
    ('BIT_AND', operator.and_),
    ('BIT_OR', operator.or_),
    ('BIT_XOR', operator.xor),
    ('BIT_LEFT', operator.lshift),
    ('BIT_RIGHT', operator.rshift),
    ('TRUE_DIVIDE', operator.truediv),
    ('FLOOR_DIVIDE', operator.floordiv),
    ('POWER', operator.pow),
]

PAIRS = [
    (7, 3), (-7, 3), (7, -3), (0, 5), (2 ** 40, 2 ** 40),
    (2 ** 62, 2 ** 62), (2 ** 100, 3), (-(2 ** 60), 2 ** 61),
]

POWER_PAIRS = [
    (7, 3), (-7, 3), (7, -3), (0, 5), (2 ** 40, 2),
    (2 ** 62, 2), (2 ** 100, 3), (-(2 ** 60), 3),
]


def make_object_site(opname='ADD', cache=True):
    """
    A function of (a, b) reducing them through one OBJECT_INPLACE site
    """

    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['OBJECT_INPLACE_' + opname, 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))
    return fn, code


def make_fast_site(opname='ADD', cache=True):
    """
    A function of (a, b) applying a OP= b through one FAST_INPLACE site
    and returning a
    """

    code = build_code([
        ['FAST', 1],
        ['FAST_INPLACE', (opcount(Operand[opname], 1), 0, 0, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))
    return fn, code


def make_const_site(opname='ADD', const=3, cache=True):
    """
    A function of (a) applying a OP= const through one FAST_INPLACE_CONST
    site and returning a
    """

    code = build_code([
        ['FAST_INPLACE_CONST', (int(Operand[opname]), 0, const, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a',), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a',), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
    return fn, code


class InplaceElectorMixin:
    """
    The checks every in-place family runs; the subclass supplies the
    site builder and the pairs each site takes
    """

    async def check_operator(self, opname, pyop, pairs):
        site, _ = self.make(opname)
        for a, b in pairs:
            try:
                expected = pyop(a, b)
            except Exception as exc:
                with self.assertRaises(type(exc)):
                    await self.apply(site, a, b)
                continue
            result = await self.apply(site, a, b)
            self.assertEqual(result, expected, (opname, a, b))
            self.assertIs(type(result), type(expected), (opname, a, b))


    def test_site_requests_one_cache_slot(self):
        _, code = self.make('ADD', cache=True)
        self.assertTrue(code.caches)
        self.assertEqual(code.cache_slots, 1)

        _, code = self.make('ADD', cache=False)
        self.assertFalse(code.caches)
        self.assertEqual(code.cache_slots, 0)


    async def test_int_site_repeats(self):
        site, _ = self.make('ADD')
        for i in range(30):
            self.assertEqual(await self.apply(site, i, 3), i + 3)


    async def test_ungated_site_matches(self):
        site, _ = self.make('ADD', cache=False)
        for i in range(30):
            self.assertEqual(await self.apply(site, i, 3), i + 3)


    async def test_every_operator_through_an_elected_site(self):
        for opname, pyop in OPERATORS:
            pairs = POWER_PAIRS if opname == 'POWER' else PAIRS
            await self.check_operator(opname, pyop, self.pairs_for(pairs))


    async def test_polymorphic_site_serves_every_type(self):
        site, _ = self.make('ADD')
        for i in range(30):
            self.assertEqual(await self.apply(site, i, 1), i + 1)
            self.assertEqual(await self.apply(site, float(i), 0.5), i + 0.5)


    async def test_demoted_site_still_serves_ints(self):
        site, _ = self.make('ADD')
        self.assertEqual(await self.apply(site, 1, 2), 3)
        for _ in range(20):
            self.assertEqual(await self.apply(site, 1.5, 2.0), 3.5)
        for i in range(20):
            self.assertEqual(await self.apply(site, i, 2), i + 2)


    async def test_error_at_an_elected_site_propagates(self):
        site, _ = self.make('MODULO')
        self.assertEqual(await self.apply(site, 7, 3), 1)
        with self.assertRaises(ZeroDivisionError):
            await self.apply(site, 7, 0)
        self.assertEqual(await self.apply(site, 8, 3), 2)


class TestObjectInplaceElector(InplaceElectorMixin, CevalTest):
    """
    OBJECT_INPLACE: a stack pair reduced in place
    """

    def make(self, opname, cache=True):
        return make_object_site(opname, cache=cache)

    async def apply(self, site, a, b):
        return await site(a, b)

    def pairs_for(self, pairs):
        return pairs


class TestFastInplaceElector(InplaceElectorMixin, CevalTest):
    """
    FAST_INPLACE: a fast slot updated from the top of the stack
    """

    def make(self, opname, cache=True):
        return make_fast_site(opname, cache=cache)

    async def apply(self, site, a, b):
        return await site(a, b)

    def pairs_for(self, pairs):
        return pairs


    async def test_list_accumulator_takes_the_generic_body(self):
        """
        A site whose slot holds a list extends in place through the
        in-place protocol, as the ladder does
        """

        site, _ = make_fast_site('ADD')
        self.assertEqual(await site(1, 2), 3)
        self.assertEqual(await site([1], [2]), [1, 2])
        self.assertEqual(await site(3, 4), 7)


class TestFastInplaceConstElector(InplaceElectorMixin, CevalTest):
    """
    FAST_INPLACE_CONST: a fast slot updated by a pool constant. Each
    (operator, constant) is its own site, so the pairs become sites
    """

    def make(self, opname, cache=True):
        return make_const_site(opname, cache=cache)

    async def apply(self, site, a, b):
        if b == 3:
            return await site(a)
        fresh, _ = make_const_site(self.current_opname, b)
        return await fresh(a)

    def pairs_for(self, pairs):
        return pairs


    async def check_operator(self, opname, pyop, pairs):
        self.current_opname = opname
        await super().check_operator(opname, pyop, pairs)


    async def test_int_site_repeats(self):
        site, _ = make_const_site('ADD', 3)
        for i in range(30):
            self.assertEqual(await site(i), i + 3)


    async def test_ungated_site_matches(self):
        site, _ = make_const_site('ADD', 3, cache=False)
        for i in range(30):
            self.assertEqual(await site(i), i + 3)


    async def test_polymorphic_site_serves_every_type(self):
        site, _ = make_const_site('ADD', 3)
        for i in range(30):
            self.assertEqual(await site(i), i + 3)
            self.assertEqual(await site(float(i)), i + 3.0)


    async def test_demoted_site_still_serves_ints(self):
        site, _ = make_const_site('ADD', 3)
        self.assertEqual(await site(1), 4)
        for _ in range(20):
            self.assertEqual(await site(1.5), 4.5)
        for i in range(20):
            self.assertEqual(await site(i), i + 3)


    async def test_error_at_an_elected_site_propagates(self):
        site, _ = make_const_site('MODULO', 3)
        self.assertEqual(await site(7), 1)
        zero, _ = make_const_site('MODULO', 0)
        with self.assertRaises(ZeroDivisionError):
            await zero(7)
        self.assertEqual(await site(8), 2)


    async def test_non_int_constant_stays_generic(self):
        """
        A site whose constant is a float never elects an int body and
        serves ints and floats alike
        """

        site, _ = make_const_site('ADD', 0.5)
        for i in range(10):
            self.assertEqual(await site(i), i + 0.5)
            self.assertEqual(await site(float(i)), i + 0.5)


    async def test_unbound_slot_raises(self):
        code = build_code([
            ['FAST_INPLACE_CONST', (int(Operand.ADD), 0, 1, 0)],
            ['CONST', None],
            ['RETURN', 1],
        ], local_syms=('i',))
        nspec = NamespaceSpec(None, ('i',), ())
        fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(()))
        with self.assertRaises(NameError):
            await fn()
        with self.assertRaises(NameError):
            await fn()


# The end.
