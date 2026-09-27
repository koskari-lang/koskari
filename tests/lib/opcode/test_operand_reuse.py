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
Tests for result reuse into a dying right operand: a pair body whose
left operand is shared and whose right operand is a temporary answers
as Python does, leaves the shared operand untouched, keeps reference
counts balanced, and a FAST_INPLACE site with a temporary right operand
leaves the slot and the stack correct.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import operator
import sys

import pytest

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount


pytestmark = pytest.mark.phase_lib


INT_OPERATORS = [
    ('OBJECT_ADD', operator.add),
    ('OBJECT_SUBTRACT', operator.sub),
    ('OBJECT_MULTIPLY', operator.mul),
    ('OBJECT_BIT_AND', operator.and_),
    ('OBJECT_BIT_OR', operator.or_),
    ('OBJECT_BIT_XOR', operator.xor),
]

FLOAT_OPERATORS = [
    ('OBJECT_ADD', operator.add),
    ('OBJECT_SUBTRACT', operator.sub),
    ('OBJECT_MULTIPLY', operator.mul),
    ('OBJECT_TRUE_DIVIDE', operator.truediv),
]


def make_shape(opname, scale):
    """
    A function of (a, b) computing a OP (b * scale): the left operand is
    the caller's shared a, the right operand a temporary the multiply
    made
    """

    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['CONST', scale],
        ['OBJECT_MULTIPLY', 2],
        [opname, 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='shape')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


def make_inplace_shape(scale):
    """
    A function of (a, b): a += (b * scale), returning (a, "mark") so an
    unbalanced stack shows in the tuple
    """

    code = build_code([
        ['FAST', 1],
        ['CONST', scale],
        ['OBJECT_MULTIPLY', 2],
        ['FAST_INPLACE', (opcount(Operand.ADD, 1), 0, 0, 0)],
        ['FAST', 0],
        ['CONST', "mark"],
        ['BUILD_ARGS', 2],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='inplace')
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))


class TestRightOperandReuse(CevalTest):

    async def test_int_shapes_agree_with_python(self):
        lefts = [1000, -1000, 2 ** 40, 2 ** 62, 2 ** 100, 300]
        rights = [500, -7, 2 ** 35, 2 ** 61, 3]
        for opname, pyop in INT_OPERATORS:
            shape = make_shape(opname, 3)
            for a in lefts:
                for b in rights:
                    expected = pyop(a, b * 3)
                    for _ in range(3):
                        result = await shape(a, b)
                        self.assertEqual(result, expected, (opname, a, b))
                        self.assertIs(type(result), int)

    async def test_float_shapes_agree_with_python(self):
        lefts = [1.5, -2.25, 1e300, 0.0]
        rights = [2.0, -0.5, 1e-300]
        for opname, pyop in FLOAT_OPERATORS:
            shape = make_shape(opname, 2.5)
            for a in lefts:
                for b in rights:
                    expected = pyop(a, b * 2.5)
                    for _ in range(3):
                        result = await shape(a, b)
                        self.assertEqual(result, expected, (opname, a, b))
                        self.assertIs(type(result), float)

    async def test_shared_left_operand_is_never_rewritten(self):
        shape = make_shape('OBJECT_ADD', 3)
        a = 1000 + 24
        b = 500 + 1
        before_a, before_b = sys.getrefcount(a), sys.getrefcount(b)
        for _ in range(50):
            self.assertEqual(await shape(a, b), 1024 + 1503)
        self.assertEqual(a, 1024)
        self.assertEqual(b, 501)
        self.assertEqual(sys.getrefcount(a), before_a)
        self.assertEqual(sys.getrefcount(b), before_b)
        fa, fb = 1.5, 2.0
        shape = make_shape('OBJECT_MULTIPLY', 2.0)
        for _ in range(50):
            self.assertEqual(await shape(fa, fb), 6.0)
        self.assertEqual((fa, fb), (1.5, 2.0))

    async def test_narrow_right_operand_takes_a_fresh_result(self):
        # the temporary has one digit; the result needs three
        shape = make_shape('OBJECT_ADD', 3)
        for _ in range(3):
            self.assertEqual(await shape(2 ** 62, 5), 2 ** 62 + 15)
            self.assertEqual(await shape(-(2 ** 62), 5), -(2 ** 62) + 15)

    async def test_inplace_with_a_temporary_right_operand(self):
        shape = make_inplace_shape(3)
        for a, b in ((1000, 500), (2 ** 62, 5), (-1000, 2 ** 40)):
            for _ in range(3):
                self.assertEqual(await shape(a, b), (a + b * 3, "mark"))
        shape = make_inplace_shape(2.5)
        for a, b in ((1.5, 2.0), (-1e300, 4.0)):
            for _ in range(3):
                self.assertEqual(await shape(a, b), (a + b * 2.5, "mark"))

    async def test_inplace_keeps_reference_counts(self):
        shape = make_inplace_shape(2.5)
        a, b = 1.5, 2.0
        before_a, before_b = sys.getrefcount(a), sys.getrefcount(b)
        for _ in range(50):
            self.assertEqual(await shape(a, b), (6.5, "mark"))
        self.assertEqual(sys.getrefcount(a), before_a)
        self.assertEqual(sys.getrefcount(b), before_b)


# The end.
