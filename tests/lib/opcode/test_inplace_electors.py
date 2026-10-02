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
FAST_INPLACE_CONST sites elect their operator's body for the kind of
the first operands they see, fall to the generic body for operands the
fast path declines, count type-guard misses toward a demotion, and
return the same results as the ladder alone throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator
import sys

import pytest

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code


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

# Shift counts stay small: a count of 2 ** 40 asks Python for a
# 128 GiB int, which raises MemoryError here and aborts a sanitizer
# build's allocator.
SHIFT_PAIRS = [
    (7, 3), (-7, 3), (7, -3), (0, 5), (2 ** 40, 29),
    (2 ** 62, 2), (2 ** 100, 3), (-(2 ** 60), 63),
]


def pairs_for_operator(opname):
    if opname == 'POWER':
        return POWER_PAIRS
    if opname in ('BIT_LEFT', 'BIT_RIGHT'):
        return SHIFT_PAIRS
    return PAIRS


def make_object_site(opname='ADD', cache=True, count=2):
    """
    A function of count positionals reducing them through one
    OBJECT_INPLACE site
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr.append(['OBJECT_INPLACE_' + opname, count])
    instr.append(['RETURN', 1])
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def make_fast_site(opname='ADD', cache=True):
    """
    A function of (a, b) applying a OP= b through one FAST_INPLACE site
    and returning a
    """

    code = build_code([
        ['FAST', 1],
        ['FAST_INPLACE', (int(Operand[opname]), 0, 0, 0)],
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
            await self.check_operator(
                opname, pyop, self.pairs_for(pairs_for_operator(opname)))


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


    async def test_bool_operand_at_an_elected_site(self):
        site, _ = self.make('ADD')
        self.assertEqual(await self.apply(site, 1, 2), 3)
        result = await self.apply(site, True, 1)
        self.assertEqual(result, 2)
        self.assertIs(type(result), int)
        self.assertEqual(await self.apply(site, 3, 4), 7)


    async def test_ungated_polymorphic_site(self):
        site, _ = self.make('ADD', cache=False)
        for i in range(30):
            self.assertEqual(await self.apply(site, i, 1), i + 1)
            self.assertEqual(await self.apply(site, float(i), 0.5), i + 0.5)


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site, _ = self.make('ADD')
        for _ in range(10):
            self.assertEqual(await self.apply(site, 1, 2), 3)
            self.assertEqual(await self.apply(site, 1.5, 2.0), 3.5)
        for i in range(600):
            self.assertEqual(await self.apply(site, i, 2), i + 2)
        self.assertEqual(await self.apply(site, 1.5, 2.0), 3.5)


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


    async def test_non_binary_site_reduces(self):
        """
        A site of three operands reduces in place through the fold
        body on ints and through the in-place protocol on the rest,
        and a protocol operand returning itself keeps its reference
        count
        """

        site, _ = make_object_site(count=3)
        for _ in range(5):
            self.assertEqual(await site(1, 2, 3), 6)
            self.assertEqual(await site([1], [2], [3]), [1, 2, 3])
        lst = [1]
        before = sys.getrefcount(lst)
        result = await site(lst, [2], [3])
        self.assertIs(result, lst)
        del result
        self.assertEqual(sys.getrefcount(lst), before)
        self.assertEqual(lst, [1, 2, 3])


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


    async def test_bool_operand_at_an_elected_site(self):
        site, _ = make_const_site('ADD', 3)
        self.assertEqual(await site(1), 4)
        result = await site(True)
        self.assertEqual(result, 4)
        self.assertIs(type(result), int)


    async def test_ungated_polymorphic_site(self):
        site, _ = make_const_site('ADD', 3, cache=False)
        for i in range(30):
            self.assertEqual(await site(i), i + 3)
            self.assertEqual(await site(float(i)), i + 3.0)


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site, _ = make_const_site('ADD', 3)
        for _ in range(10):
            self.assertEqual(await site(1), 4)
            self.assertEqual(await site(1.5), 4.5)
        for i in range(600):
            self.assertEqual(await site(i), i + 3)
        self.assertEqual(await site(1.5), 4.5)


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
