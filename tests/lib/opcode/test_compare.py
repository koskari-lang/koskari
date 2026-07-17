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
Tests for the selector-keyed comparison opcodes CHECK_COMPARE and
OBJECT_COMPARE (KosCompare dispatch)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.lib import Compare

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


LT = int(Compare.KOS_COMPARE_LT)
EQ = int(Compare.KOS_COMPARE_EQ)
IS = int(Compare.KOS_COMPARE_IS)
IN = int(Compare.KOS_COMPARE_IN)
NOT_IN = int(Compare.KOS_COMPARE_NOT_IN)
IS_INSTANCE = int(Compare.KOS_COMPARE_IS_INSTANCE)


def cmpcount(sel, count):
    return (sel << 8) | count


def cmpsrc(sel, x_const, y_const):
    return (sel & 0x0f) | (0x10 if x_const else 0) | (0x20 if y_const else 0)


class TestCheckCompare(CevalTest):
    """
    CHECK_COMPARE chain-compares under a KosCompare selector and jumps
    on chain failure
    """

    async def test_lt_continues_when_chain_holds(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["CHECK_COMPARE", (LT, 3, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_lt_jumps_when_chain_fails(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 3],
            ["CONST", 2],
            ["CHECK_COMPARE", (LT, 3, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_eq_binary(self):
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 10],
            ["CHECK_COMPARE", (EQ, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_is_jumps_on_distinct_objects(self):
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", 1],
            ["CHECK_COMPARE", (IS, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_in_continues_when_contained(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", (1, 2, 3)],
            ["CHECK_COMPARE", (IN, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_not_in_jumps_when_contained(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", (1, 2, 3)],
            ["CHECK_COMPARE", (NOT_IN, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_is_instance(self):
        code, ns = build_expr([
            ["CONST", 5],
            ["CONST", int],
            ["CHECK_COMPARE", (IS_INSTANCE, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_lt_unorderable_raises(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", "x"],
            ["CHECK_COMPARE", (LT, 2, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


class TestObjectCompare(CevalTest):
    """
    OBJECT_COMPARE chain-compares under a CMPCOUNT selector and pushes
    the result
    """

    async def test_lt_chain_true(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["OBJECT_COMPARE", cmpcount(LT, 3)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, True)


    async def test_lt_chain_false(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 3],
            ["CONST", 2],
            ["OBJECT_COMPARE", cmpcount(LT, 3)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, False)


    async def test_is_binary(self):
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["OBJECT_COMPARE", cmpcount(IS, 2)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, True)


    async def test_not_in(self):
        code, ns = build_expr([
            ["CONST", 9],
            ["CONST", (1, 2, 3)],
            ["OBJECT_COMPARE", cmpcount(NOT_IN, 2)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, True)


    async def test_is_instance_multi_class(self):
        code, ns = build_expr([
            ["CONST", 5],
            ["CONST", str],
            ["CONST", int],
            ["OBJECT_COMPARE", cmpcount(IS_INSTANCE, 3)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, True)


class TestCheckCompareFast(CevalTest):
    """
    CHECK_COMPARE_FAST loads operands per the CMPSRC source bits and
    branches on comparison failure
    """

    async def test_fast_fast(self):
        # slots stored explicitly: build_expr's values= path rebuilds
        # local_syms from a set, so slot order would be hash-dependent
        code, ns = build_expr([
            ["CONST", 1],
            ["FAST_STORE", 0],
            ["CONST", 2],
            ["FAST_STORE", 1],
            ["CHECK_COMPARE_FAST", (cmpsrc(LT, False, False), 0, 1, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("a", "b"))
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_fast_const_branches(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_FAST", (cmpsrc(LT, False, True), 0, 5, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("a",), values={"a": 9})
        self.assertEqual(await make_callable(code, ns)(), 0)


    async def test_const_fast(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_FAST", (cmpsrc(LT, True, False), 0, 0, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("a",), values={"a": 9})
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_unbound_slot_raises(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_FAST", (cmpsrc(LT, False, True), 0, 5, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("a",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestCheckCompareVar(CevalTest):
    """
    CHECK_COMPARE_VAR resolves names per the CMPSRC source bits and
    branches on comparison failure
    """

    async def test_var_var(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_VAR", (cmpsrc(EQ, False, False), "a", "b", "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"a": 7, "b": 7})
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_var_const_branches(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_VAR", (cmpsrc(EQ, False, True), "a", 8, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ], values={"a": 7})
        self.assertEqual(await make_callable(code, ns)(), 0)


    async def test_missing_name_raises(self):
        code, ns = build_expr([
            ["CHECK_COMPARE_VAR", (cmpsrc(EQ, False, True), "nope", 8, "fail")],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestObjectCompareFast(CevalTest):
    """
    OBJECT_COMPARE_FAST evaluates the ternary bounds row and pushes the
    chain result
    """

    async def test_in_bounds(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_FAST", (LT, 0, 0, 100)],
            ["RETURN", 1],
        ], local_syms=("x",), values={"x": 50})
        self.assertIs(await make_callable(code, ns)(), True)


    async def test_above_bounds(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_FAST", (LT, 0, 0, 100)],
            ["RETURN", 1],
        ], local_syms=("x",), values={"x": 200})
        self.assertIs(await make_callable(code, ns)(), False)


    async def test_below_bounds(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_FAST", (LT, 0, 0, 100)],
            ["RETURN", 1],
        ], local_syms=("x",), values={"x": -3})
        self.assertIs(await make_callable(code, ns)(), False)


    async def test_unbound_slot_raises(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_FAST", (LT, 0, 0, 100)],
            ["RETURN", 1],
        ], local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestObjectCompareVar(CevalTest):
    """
    OBJECT_COMPARE_VAR resolves the middle operand by name and pushes
    the chain result
    """

    async def test_in_bounds(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_VAR", (LT, 0, "x", 100)],
            ["RETURN", 1],
        ], values={"x": 50})
        self.assertIs(await make_callable(code, ns)(), True)


    async def test_missing_name_raises(self):
        code, ns = build_expr([
            ["OBJECT_COMPARE_VAR", (LT, 0, "nope", 100)],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.
