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
Tests for the FAST_ATTR_INPLACE elector: a site whose field cache has
resolved a direct location elects an int or float body on its second
execution, guards through the field slot on every execution, writes
the result back through the same location, and falls to the generic
path for another type, a rebound receiver, an unset attribute, an
operand with its own in-place protocol, and a count above one.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount

import pytest


pytestmark = pytest.mark.phase_lib


class SlotBox:
    __slots__ = ('a',)

    def __init__(self, a):
        self.a = a


class DictBox:
    def __init__(self, a):
        self.a = a


BOXES = (SlotBox, DictBox)

INT_OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('MODULO', operator.mod),
    ('FLOOR_DIVIDE', operator.floordiv), ('BIT_AND', operator.and_),
    ('BIT_LEFT', operator.lshift), ('POWER', operator.pow),
]

INT_PAIRS = [
    (7, 3), (-7, 3), (300, 5), (2 ** 40, 3), (2 ** 62, 2), (2 ** 100, 3),
]

FLOAT_OPERATORS = [
    ('ADD', operator.add), ('MULTIPLY', operator.mul),
    ('TRUE_DIVIDE', operator.truediv), ('MODULO', operator.mod),
]

FLOAT_PAIRS = [
    (1.5, 2.0), (-2.25, 0.5), (1e300, 3.0), (0.0, -1.0), (7.5, -0.0),
]


def make_site(opname='ADD', count=1, cache=True):
    """
    A function of (box, v...) applying box.a OP= v through one
    FAST_ATTR_INPLACE site and returning the box
    """

    syms = ('box',) + tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i + 1] for i in range(count)]
    instr += [
        ['FAST_ATTR_INPLACE', (opcount(Operand[opname], count), 0, 'a', 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ]
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def same(result, expected):
    return repr(result) == repr(expected) and type(result) is type(expected)


class TestFastAttrInplaceElector(CevalTest):

    def test_site_requests_two_cache_slots(self):
        _, code = make_site(cache=True)
        self.assertEqual(code.cache_slots, 2)
        _, code = make_site(cache=False)
        self.assertEqual(code.cache_slots, 0)


    async def test_every_int_operator_through_an_elected_site(self):
        for cls in BOXES:
            for opname, pyop in INT_OPERATORS:
                site, _ = make_site(opname)
                for a, b in INT_PAIRS * 2:
                    box = cls(a)
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(box, b)
                        continue
                    await site(box, b)
                    self.assertTrue(same(box.a, expected), (cls, opname, a, b, box.a))


    async def test_every_float_operator_through_an_elected_site(self):
        for cls in BOXES:
            for opname, pyop in FLOAT_OPERATORS:
                site, _ = make_site(opname)
                for a, b in FLOAT_PAIRS * 2:
                    box = cls(a)
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(box, b)
                        continue
                    await site(box, b)
                    self.assertTrue(same(box.a, expected), (cls, opname, a, b))


    async def test_repeated_updates_on_one_receiver(self):
        for cls in BOXES:
            site, _ = make_site('ADD')
            box = cls(1000)
            for i in range(300):
                await site(box, i)
            self.assertEqual(box.a, 1000 + sum(range(300)))


    async def test_shared_value_is_untouched(self):
        for cls in BOXES:
            site, _ = make_site('ADD')
            value = 2 ** 40
            box = cls(value)
            await site(box, 1)
            await site(box, 1)
            self.assertEqual(value, 2 ** 40)
            self.assertEqual(box.a, 2 ** 40 + 2)


    async def test_receiver_class_changes_under_the_site(self):
        site, _ = make_site('ADD')
        boxes = [SlotBox(10), DictBox(20)]
        for i in range(40):
            box = boxes[i % 2]
            await site(box, 1)
        self.assertEqual(boxes[0].a, 30)
        self.assertEqual(boxes[1].a, 40)


    async def test_unset_attribute_raises_after_the_election(self):
        site, _ = make_site('ADD')
        box = SlotBox(1)
        await site(box, 1)
        await site(box, 1)
        empty = SlotBox.__new__(SlotBox)
        with self.assertRaises(AttributeError):
            await site(empty, 1)
        await site(box, 1)
        self.assertEqual(box.a, 4)


    async def test_inplace_protocol_operand_at_an_elected_site(self):
        for cls in BOXES:
            site, _ = make_site('ADD')
            box = cls(1)
            await site(box, 1)
            await site(box, 1)
            box.a = [1]
            await site(box, [2])
            self.assertEqual(box.a, [1, 2])
            box.a = 5
            await site(box, 1)
            self.assertEqual(box.a, 6)


    async def test_mixed_types_at_an_elected_site(self):
        for cls in BOXES:
            site, _ = make_site('ADD')
            box = cls(1)
            await site(box, 1)
            await site(box, 0.5)
            self.assertTrue(same(box.a, 2.5))
            await site(box, 1)
            self.assertTrue(same(box.a, 3.5))


    async def test_demoted_site_re_elects_and_serves_every_type(self):
        site, _ = make_site('ADD')
        box = SlotBox(0)
        for _ in range(10):
            box.a = 1
            await site(box, 2)
            self.assertEqual(box.a, 3)
            box.a = 1.5
            await site(box, 2.0)
            self.assertEqual(box.a, 3.5)
        box.a = 0
        for _ in range(600):
            await site(box, 1)
        self.assertEqual(box.a, 600)
        box.a = 'x'
        await site(box, 'y')
        self.assertEqual(box.a, 'xy')


    async def test_count_of_two_stays_generic(self):
        for cls in BOXES:
            site, _ = make_site('ADD', count=2)
            box = cls(1)
            for _ in range(5):
                await site(box, 2, 3)
            self.assertEqual(box.a, 26)


    async def test_ungated_site_matches(self):
        for cls in BOXES:
            site, _ = make_site('MULTIPLY', cache=False)
            box = cls(3)
            for _ in range(10):
                await site(box, 2)
            self.assertEqual(box.a, 3 * 2 ** 10)


# The end.
