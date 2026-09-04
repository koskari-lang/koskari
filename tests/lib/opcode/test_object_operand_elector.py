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
Tests for the OBJECT_OPERAND elector: a binary arithmetic site elects
its operator's int body on the first exact-int pair it sees and keeps
serving through it, falls to the generic body for operands the int
fast path declines, counts type-guard misses toward a demotion, and
returns the same results as the ladder alone throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code

import pytest


pytestmark = pytest.mark.phase_lib


OPERATORS = [
    ('OBJECT_ADD', operator.add),
    ('OBJECT_SUBTRACT', operator.sub),
    ('OBJECT_MULTIPLY', operator.mul),
    ('OBJECT_MODULO', operator.mod),
    ('OBJECT_BIT_AND', operator.and_),
    ('OBJECT_BIT_OR', operator.or_),
    ('OBJECT_BIT_XOR', operator.xor),
    ('OBJECT_BIT_LEFT', operator.lshift),
    ('OBJECT_BIT_RIGHT', operator.rshift),
    ('OBJECT_TRUE_DIVIDE', operator.truediv),
    ('OBJECT_FLOOR_DIVIDE', operator.floordiv),
    ('OBJECT_POWER', operator.pow),
]


def make_site(opname='OBJECT_ADD', count=2, cache=True):
    """
    A function of count positionals applying one operator to them at a
    single OBJECT_OPERAND site

    :return: (function, code)
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr.append([opname, count])
    instr.append(['RETURN', 1])
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


class TestObjectOperandElector(CevalTest):

    def test_object_operand_requests_one_cache_slot(self):
        """
        Each OBJECT_OPERAND site reserves one per-site cache slot when
        caching is on, and none when it is gated off
        """

        _, code = make_site(cache=True)
        self.assertTrue(code.caches)
        self.assertEqual(code.cache_slots, 1)

        _, code = make_site(cache=False)
        self.assertFalse(code.caches)
        self.assertEqual(code.cache_slots, 0)


    async def test_int_site_repeats(self):
        """
        Repeated int pairs through one site return the sum every time
        """

        site, _ = make_site()
        for i in range(30):
            self.assertEqual(await site(i, 2), i + 2)


    async def test_ungated_site_matches(self):
        """
        With caching gated off the site returns the same results
        """

        site, _ = make_site(cache=False)
        for i in range(30):
            self.assertEqual(await site(i, 2), i + 2)


    async def test_every_operator_through_an_elected_site(self):
        """
        Each operator agrees with Python through a site that first saw
        an int pair, for small operands, operands the fast path declines
        (negative modulo, negative shifts), and wide ones
        """

        pairs = [
            (7, 3), (-7, 3), (7, -3), (0, 5), (2 ** 40, 2 ** 40),
            (2 ** 62, 2 ** 62), (2 ** 100, 3), (-(2 ** 60), 2 ** 61),
        ]
        power_pairs = [
            (7, 3), (-7, 3), (7, -3), (0, 5), (2 ** 40, 2),
            (2 ** 62, 2), (2 ** 100, 3), (-(2 ** 60), 3),
        ]
        for opname, pyop in OPERATORS:
            site, _ = make_site(opname)
            chosen = power_pairs if opname == 'OBJECT_POWER' else pairs
            for a, b in chosen:
                try:
                    expected = pyop(a, b)
                except Exception as exc:
                    with self.assertRaises(type(exc)):
                        await site(a, b)
                    continue
                result = await site(a, b)
                self.assertEqual(result, expected, (opname, a, b))
                self.assertIs(type(result), type(expected), (opname, a, b))


    async def test_wide_ints_at_an_elected_site(self):
        """
        An elected int site takes ints the inline read and the fast path
        decline, then small ints again
        """

        site, _ = make_site('OBJECT_MULTIPLY')
        self.assertEqual(await site(3, 4), 12)
        self.assertEqual(await site(2 ** 62, 2 ** 62), 2 ** 124)
        self.assertEqual(await site(2 ** 100, 2), 2 ** 101)
        self.assertEqual(await site(2 ** 30, 2 ** 30), 2 ** 60)
        self.assertEqual(await site(3, 4), 12)


    async def test_polymorphic_site_serves_every_type(self):
        """
        A site alternating between ints, floats, and strings returns the
        right results throughout the churn, before and after the miss
        limit demotes it
        """

        site, _ = make_site()
        for i in range(30):
            self.assertEqual(await site(i, 1), i + 1)
            self.assertEqual(await site(float(i), 0.5), i + 0.5)
            self.assertEqual(await site('a', 'b'), 'ab')


    async def test_demoted_site_still_serves_ints(self):
        """
        After more type misses than the limit, a site keeps returning the
        right sums for int pairs
        """

        site, _ = make_site()
        self.assertEqual(await site(1, 2), 3)
        for _ in range(20):
            self.assertEqual(await site('a', 'b'), 'ab')
        for i in range(20):
            self.assertEqual(await site(i, 2), i + 2)


    async def test_site_elected_generic_serves_ints_later(self):
        """
        A site whose first operands were not ints stays on the generic
        body and still returns int results afterwards
        """

        site, _ = make_site()
        self.assertEqual(await site(0.5, 0.25), 0.75)
        for i in range(10):
            self.assertEqual(await site(i, 1), i + 1)


    async def test_non_binary_site_reduces(self):
        """
        A site of three operands is not electable and reduces as before
        """

        site, _ = make_site(count=3)
        for _ in range(5):
            self.assertEqual(await site(1, 2, 3), 6)
            self.assertEqual(await site('a', 'b', 'c'), 'abc')


    async def test_error_at_an_elected_site_propagates(self):
        """
        A ZeroDivisionError from the generic path at an elected modulo
        site propagates, and the site serves the next pair
        """

        site, _ = make_site('OBJECT_MODULO')
        self.assertEqual(await site(7, 3), 1)
        with self.assertRaises(ZeroDivisionError):
            await site(7, 0)
        self.assertEqual(await site(8, 3), 2)


    async def test_unsupported_types_at_an_elected_site_raise(self):
        """
        A TypeError from operands the operator rejects propagates after
        the election as before it
        """

        site, _ = make_site('OBJECT_BIT_AND')
        self.assertEqual(await site(6, 3), 2)
        with self.assertRaises(TypeError):
            await site(6, 'x')
        self.assertEqual(await site(6, 3), 2)


# The end.
