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
Tests for the compare electors: CHECK_COMPARE and OBJECT_COMPARE sites
elect their int body on the first exact-int pair under a richcompare
selector, keep serving through it, fall to the generic body for other
types and selectors, and return the same results and take the same
branches as the ladder alone.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

import pytest

from koskari.lib import Compare, FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code


pytestmark = pytest.mark.phase_lib


SELECTORS = [
    ('LT', operator.lt), ('LE', operator.le), ('EQ', operator.eq),
    ('NE', operator.ne), ('GT', operator.gt), ('GE', operator.ge),
]

PAIRS = [
    (1, 2), (2, 1), (3, 3), (-1, 1), (0, 0), (-7, -7),
    (2 ** 30, 2 ** 30 - 1), (2 ** 60, 2 ** 60), (2 ** 100, 3),
    (-(2 ** 63), 2 ** 63), (2 ** 63 - 1, -(2 ** 63)),
]


def cmpcount(sel, count):
    return (int(sel) << 8) | count


def make_check_site(selname='LT', count=2, cache=True):
    """
    A function of count positionals that returns 1 when the chain holds
    and 0 when CHECK_COMPARE jumps
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr += [
        ['CHECK_COMPARE', (int(Compare[selname]), count, 'fail', 0)],
        ['CONST', 1],
        ['RETURN', 1],
        ['LABEL', 'fail'],
        ['CONST', 0],
        ['RETURN', 1],
    ]
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def make_object_site(selname='LT', count=2, cache=True):
    """
    A function of count positionals returning OBJECT_COMPARE's value
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr += [
        ['OBJECT_COMPARE', cmpcount(Compare[selname], count)],
        ['RETURN', 1],
    ]
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


class TestCheckCompareElector(CevalTest):

    def test_site_requests_one_cache_slot(self):
        _, code = make_check_site(cache=True)
        self.assertEqual(code.cache_slots, 1)
        _, code = make_check_site(cache=False)
        self.assertEqual(code.cache_slots, 0)


    async def test_every_selector_at_an_elected_site(self):
        for selname, pyop in SELECTORS:
            site, _ = make_check_site(selname)
            for a, b in PAIRS:
                self.assertEqual(
                    await site(a, b), int(pyop(a, b)), (selname, a, b))


    async def test_int_site_repeats(self):
        site, _ = make_check_site('LT')
        for i in range(30):
            self.assertEqual(await site(i, 15), int(i < 15))


    async def test_ungated_site_matches(self):
        site, _ = make_check_site('LT', cache=False)
        for i in range(30):
            self.assertEqual(await site(i, 15), int(i < 15))
            self.assertEqual(await site(float(i), 15.0), int(i < 15))


    async def test_polymorphic_site_serves_every_type(self):
        site, _ = make_check_site('LT')
        for i in range(30):
            self.assertEqual(await site(i, 15), int(i < 15))
            self.assertEqual(await site(float(i), 15.5), int(i < 15.5))
            self.assertEqual(await site('a', 'b'), 1)


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site, _ = make_check_site('EQ')
        for _ in range(10):
            self.assertEqual(await site(1, 1), 1)
            self.assertEqual(await site(1.5, 2.0), 0)
        for i in range(600):
            self.assertEqual(await site(i, 7), int(i == 7))
        self.assertEqual(await site(1.5, 1.5), 1)


    async def test_bool_operand_at_an_elected_site(self):
        site, _ = make_check_site('EQ')
        self.assertEqual(await site(1, 1), 1)
        self.assertEqual(await site(True, 1), 1)
        self.assertEqual(await site(False, 1), 0)


    async def test_identity_selector_stays_generic(self):
        site, _ = make_check_site('IS')
        obj = object()
        for _ in range(10):
            self.assertEqual(await site(obj, obj), 1)
            self.assertEqual(await site(1, 1.0), 0)


    async def test_chain_of_three_stays_generic(self):
        site, _ = make_check_site('LT', count=3)
        for _ in range(10):
            self.assertEqual(await site(1, 2, 3), 1)
            self.assertEqual(await site(1, 3, 2), 0)


    async def test_unorderable_at_an_elected_site_raises(self):
        site, _ = make_check_site('LT')
        self.assertEqual(await site(1, 2), 1)
        with self.assertRaises(TypeError):
            await site(1, 'x')
        self.assertEqual(await site(1, 2), 1)


class TestObjectCompareElector(CevalTest):

    def test_site_requests_one_cache_slot(self):
        _, code = make_object_site(cache=True)
        self.assertEqual(code.cache_slots, 1)
        _, code = make_object_site(cache=False)
        self.assertEqual(code.cache_slots, 0)


    async def test_every_selector_at_an_elected_site(self):
        for selname, pyop in SELECTORS:
            site, _ = make_object_site(selname)
            for a, b in PAIRS:
                result = await site(a, b)
                self.assertIs(result, pyop(a, b), (selname, a, b))


    async def test_polymorphic_site_serves_every_type(self):
        site, _ = make_object_site('LT')
        for i in range(30):
            self.assertIs(await site(i, 15), i < 15)
            self.assertIs(await site(float(i), 15.5), i < 15.5)
            self.assertIs(await site('a', 'b'), True)


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site, _ = make_object_site('NE')
        for _ in range(10):
            self.assertIs(await site(1, 2), True)
            self.assertIs(await site(1.5, 1.5), False)
        for i in range(600):
            self.assertIs(await site(i, 7), i != 7)
        self.assertIs(await site(1.5, 2.5), True)


    async def test_chain_of_three_stays_generic(self):
        site, _ = make_object_site('LT', count=3)
        for _ in range(10):
            self.assertIs(await site(1, 2, 3), True)
            self.assertIs(await site(1, 3, 2), False)


    async def test_membership_selector_stays_generic(self):
        site, _ = make_object_site('IN')
        for _ in range(10):
            self.assertIs(await site(1, [1, 2]), True)
            self.assertIs(await site(3, [1, 2]), False)


# The end.
