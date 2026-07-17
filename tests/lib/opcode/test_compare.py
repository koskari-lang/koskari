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


# The end.
