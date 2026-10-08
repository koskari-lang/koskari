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
Tests for the const-operand elector families: OBJECT_OPERAND_CONST takes
its left operand from the stack, FAST_OPERAND_CONST from a fast slot,
and both take the right operand from the pool. Each site elects the
operator's body for the kind it first sees, declines to the generic
body where the fast path cannot answer, and agrees with Python
throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import operator

import pytest

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


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

FLOAT_OPERATORS = [
    ('ADD', operator.add),
    ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul),
    ('MODULO', operator.mod),
    ('TRUE_DIVIDE', operator.truediv),
    ('FLOOR_DIVIDE', operator.floordiv),
]


def make_stack_site(opname='ADD', const=3, cache=True):
    """
    A function of (a) computing a OP const through one
    OBJECT_OPERAND_CONST site with a pushed by FAST
    """

    code = build_code([
        ['FAST', 0],
        ['OBJECT_OPERAND_CONST', (int(Operand[opname]), const, 0, 0)],
        ['RETURN', 1],
    ], local_syms=('a',), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a',), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
    return fn, code


def make_fast_site(opname='ADD', const=3, cache=True):
    """
    A function of (a) computing a OP const through one
    FAST_OPERAND_CONST site reading a from its slot
    """

    code = build_code([
        ['FAST_OPERAND_CONST', (int(Operand[opname]), 0, const, 0)],
        ['RETURN', 1],
    ], local_syms=('a',), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a',), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
    return fn, code


def make_stack_fast_site(opname='ADD', cache=True):
    """
    A function of (a, b) computing a OP b through one
    OBJECT_OPERAND_FAST site with a pushed by FAST and b read from its
    slot
    """

    code = build_code([
        ['FAST', 0],
        ['OBJECT_OPERAND_FAST', (int(Operand[opname]), 1, 0, 0)],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    fn = make_callable(code, nspec.get_namespace(None),
                       FormalsSpec(('a', 'b')))
    return fn, code


def make_fast_fast_site(opname='ADD', cache=True):
    """
    A function of (a, b) computing a OP b through one FAST_OPERAND_FAST
    site reading both from their slots
    """

    code = build_code([
        ['FAST_OPERAND_FAST', (int(Operand[opname]), 0, 1, 0)],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    fn = make_callable(code, nspec.get_namespace(None),
                       FormalsSpec(('a', 'b')))
    return fn, code


class SlotOperandMixin:
    """
    The checks both slot-right families run; the subclass supplies the
    site builder
    """

    def test_site_requests_one_cache_slot(self):
        _, code = self.make_site(cache=True)
        self.assertEqual(code.cache_slots, 1)
        _, code = self.make_site(cache=False)
        self.assertEqual(code.cache_slots, 0)

    async def test_int_site_repeats(self):
        site, _ = self.make_site('ADD')
        for i in range(30):
            self.assertEqual(await site(i, 2), i + 2)

    async def test_every_operator_agrees_with_python(self):
        lefts = [7, -7, 0, 2 ** 40, 2 ** 62, 2 ** 100, -(2 ** 60)]
        rights = {'BIT_LEFT': [3, 0], 'BIT_RIGHT': [3, 70], 'POWER': [3, 0]}
        for opname, pyop in OPERATORS:
            site, _ = self.make_site(opname)
            for a in lefts:
                for b in rights.get(opname, [3, -3, 0, 2 ** 70]):
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(a, b)
                        continue
                    result = await site(a, b)
                    self.assertEqual(result, expected, (opname, a, b))
                    self.assertIs(type(result), type(expected), (opname, a, b))

    async def test_float_operators_agree_with_python(self):
        lefts = [7.5, -7.5, 0.0, 1e300, -1e-300]
        for opname, pyop in FLOAT_OPERATORS:
            site, _ = self.make_site(opname)
            for a in lefts:
                for b in (2.5, -0.5):
                    expected = pyop(a, b)
                    result = await site(a, b)
                    self.assertEqual(result, expected, (opname, a, b))
                    self.assertIs(type(result), float)

    async def test_polymorphic_site_serves_every_type(self):
        site, _ = self.make_site('ADD')
        for i in range(30):
            self.assertEqual(await site(i, 1), i + 1)
            self.assertEqual(await site(float(i), 1.0), i + 1.0)
            self.assertEqual(await site('a' * i, 'x'), 'a' * i + 'x')
        with self.assertRaises(TypeError):
            await site(3, 'x')

    async def test_mixed_kinds_take_the_generic_body(self):
        site, _ = self.make_site('MULTIPLY')
        self.assertEqual(await site(3, 2), 6)
        self.assertEqual(await site(2.5, 2), 5.0)
        self.assertEqual(await site([1], 2), [1, 1])
        self.assertEqual(await site(True, 2), 2)
        self.assertEqual(await site(4, 2.0), 8.0)
        self.assertEqual(await site(4, 2), 8)

    async def test_error_at_an_elected_site_propagates(self):
        site, _ = self.make_site('TRUE_DIVIDE')
        self.assertEqual(await site(8, 2), 4.0)
        with self.assertRaises(ZeroDivisionError):
            await site(8, 0)
        with self.assertRaises(TypeError):
            await site(None, 2)


class TestObjectOperandFast(SlotOperandMixin, CevalTest):
    """
    OBJECT_OPERAND_FAST: the left operand on the stack, the right in a
    fast slot, the result replacing the operand and the slot untouched
    """

    make_site = staticmethod(make_stack_fast_site)

    async def test_result_replaces_the_operand_and_slot_keeps_its_value(self):
        code = build_code([
            ['CONST', 'below'],
            ['FAST', 0],
            ['OBJECT_OPERAND_FAST', (int(Operand.ADD), 1, 0, 0)],
            ['FAST', 1],
            ['BUILD_ARGS', 3],
            ['RETURN', 1],
        ], local_syms=('a', 'b'), name='site')
        nspec = NamespaceSpec(None, ('a', 'b'), ())
        fn = make_callable(code, nspec.get_namespace(None),
                           FormalsSpec(('a', 'b')))
        self.assertEqual(await fn(20, 22), ('below', 42, 22))


class TestFastOperandFast(SlotOperandMixin, CevalTest):
    """
    FAST_OPERAND_FAST: both operands in fast slots, the result pushed
    and both slots untouched
    """

    make_site = staticmethod(make_fast_fast_site)

    async def test_slots_keep_their_values(self):
        code = build_code([
            ['FAST_OPERAND_FAST', (int(Operand.ADD), 0, 1, 0)],
            ['FAST', 0],
            ['FAST', 1],
            ['BUILD_ARGS', 3],
            ['RETURN', 1],
        ], local_syms=('a', 'b'), name='site')
        nspec = NamespaceSpec(None, ('a', 'b'), ())
        fn = make_callable(code, nspec.get_namespace(None),
                           FormalsSpec(('a', 'b')))
        self.assertEqual(await fn(41, 1), (42, 41, 1))

    async def test_one_slot_as_both_operands(self):
        site, _ = self.make_site('MULTIPLY')
        code = build_code([
            ['FAST_OPERAND_FAST', (int(Operand.MULTIPLY), 0, 0, 0)],
            ['RETURN', 1],
        ], local_syms=('a',), name='site')
        nspec = NamespaceSpec(None, ('a',), ())
        fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
        for i in range(30):
            self.assertEqual(await fn(i), i * i)
        self.assertEqual(await fn(1.5), 2.25)


class ConstOperandMixin:
    """
    The checks both families run; the subclass supplies the site builder
    """

    def test_site_requests_one_cache_slot(self):
        _, code = self.make_site(cache=True)
        self.assertEqual(code.cache_slots, 1)
        _, code = self.make_site(cache=False)
        self.assertEqual(code.cache_slots, 0)

    async def test_int_site_repeats(self):
        site, _ = self.make_site('ADD', 2)
        for i in range(30):
            self.assertEqual(await site(i), i + 2)

    async def test_ungated_site_matches(self):
        site, _ = self.make_site('ADD', 2, cache=False)
        for i in range(30):
            self.assertEqual(await site(i), i + 2)

    async def test_every_operator_agrees_with_python(self):
        lefts = [7, -7, 0, 2 ** 40, 2 ** 62, 2 ** 100, -(2 ** 60)]
        consts = {'BIT_LEFT': 3, 'BIT_RIGHT': 3, 'POWER': 3}
        for opname, pyop in OPERATORS:
            const = consts.get(opname, 3)
            site, _ = self.make_site(opname, const)
            for a in lefts:
                try:
                    expected = pyop(a, const)
                except Exception as exc:
                    with self.assertRaises(type(exc)):
                        await site(a)
                    continue
                result = await site(a)
                self.assertEqual(result, expected, (opname, a))
                self.assertIs(type(result), type(expected), (opname, a))

    async def test_float_operators_agree_with_python(self):
        lefts = [7.5, -7.5, 0.0, 1e300, -1e-300]
        for opname, pyop in FLOAT_OPERATORS:
            site, _ = self.make_site(opname, 2.5)
            for a in lefts:
                expected = pyop(a, 2.5)
                result = await site(a)
                self.assertEqual(result, expected, (opname, a))
                self.assertIs(type(result), float)

    async def test_negative_and_zero_consts_decline_to_python(self):
        site, _ = self.make_site('MODULO', -3)
        for a in (7, -7, 0, 2 ** 70):
            self.assertEqual(await site(a), a % -3)
        site, _ = self.make_site('FLOOR_DIVIDE', 0)
        with self.assertRaises(ZeroDivisionError):
            await site(7)
        site, _ = self.make_site('BIT_LEFT', -1)
        with self.assertRaises(ValueError):
            await site(7)

    async def test_wide_const_never_elects_but_computes(self):
        site, _ = self.make_site('ADD', 2 ** 100)
        for a in (1, 2 ** 100, -5):
            self.assertEqual(await site(a), a + 2 ** 100)

    async def test_polymorphic_site_serves_every_type(self):
        site, _ = self.make_site('ADD', 1)
        for i in range(30):
            self.assertEqual(await site(i), i + 1)
            self.assertEqual(await site(float(i)), i + 1.0)
        site, _ = self.make_site('ADD', 'x')
        for i in range(30):
            self.assertEqual(await site('a' * i), 'a' * i + 'x')
        with self.assertRaises(TypeError):
            await site(3)

    async def test_mixed_kinds_take_the_generic_body(self):
        site, _ = self.make_site('MULTIPLY', 2)
        self.assertEqual(await site(3), 6)
        self.assertEqual(await site(2.5), 5.0)
        self.assertEqual(await site([1]), [1, 1])
        self.assertEqual(await site(True), 2)
        self.assertEqual(await site(4), 8)

    async def test_error_at_an_elected_site_propagates(self):
        site, _ = self.make_site('TRUE_DIVIDE', 2)
        self.assertEqual(await site(8), 4.0)
        with self.assertRaises(TypeError):
            await site(None)


class TestObjectOperandConst(ConstOperandMixin, CevalTest):
    """
    OBJECT_OPERAND_CONST: the left operand on the stack, the constant
    on the row, the result replacing the operand
    """

    make_site = staticmethod(make_stack_site)

    async def test_result_replaces_the_operand(self):
        code, ns = build_expr([
            ['CONST', 'below'],
            ['CONST', 20],
            ['OBJECT_OPERAND_CONST', (int(Operand.ADD), 22, 0, 0)],
            ['BUILD_ARGS', 2],
            ['RETURN', 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), ('below', 42))


class TestFastOperandConst(ConstOperandMixin, CevalTest):
    """
    FAST_OPERAND_CONST: the left operand in a fast slot, the constant
    on the row, the result pushed and the slot untouched
    """

    make_site = staticmethod(make_fast_site)

    async def test_slot_keeps_its_value(self):
        code = build_code([
            ['FAST_OPERAND_CONST', (int(Operand.ADD), 0, 1, 0)],
            ['FAST', 0],
            ['BUILD_ARGS', 2],
            ['RETURN', 1],
        ], local_syms=('a',), name='site')
        nspec = NamespaceSpec(None, ('a',), ())
        fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
        self.assertEqual(await fn(41), (42, 41))

    async def test_unbound_slot_raises(self):
        code, ns = build_expr([
            ['FAST_OPERAND_CONST', (int(Operand.ADD), 0, 1, 0)],
            ['RETURN', 1],
        ], local_syms=('a',))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.
