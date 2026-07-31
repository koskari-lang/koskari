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
Tests for the CHECK_COMPARE opcode across its Compare selectors.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.lib import Compare

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


EQ = int(Compare.EQ)
NE = int(Compare.NE)
IN = int(Compare.IN)
NOT_IN = int(Compare.NOT_IN)
IS = int(Compare.IS)
IS_NOT = int(Compare.IS_NOT)
IS_INSTANCE = int(Compare.IS_INSTANCE)
IS_SUBCLASS = int(Compare.IS_SUBCLASS)


class TestCheckOpcodes(CevalTest):
    """
    Tests for CHECK_COMPARE under the EQ, NE, IN, NOT_IN, IS, IS_NOT,
    IS_INSTANCE, and IS_SUBCLASS selectors
    """

    async def test_check_eq_jumps_on_mismatch(self):
        """
        CHECK_COMPARE EQ jumps when left != right
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CHECK_COMPARE", (EQ, 2, "fail", 0)],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_eq_continues_on_match(self):
        """
        CHECK_COMPARE EQ does not jump when left == right
        """

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


    async def test_check_eq_variadic_continues_when_all_equal(self):
        """
        CHECK_COMPARE EQ chains over COUNT operands: no jump when every arm is equal
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["CONST", 7],
            ["CONST", 7],
            ["CHECK_COMPARE", (EQ, 3, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_eq_variadic_jumps_when_one_differs(self):
        """
        CHECK_COMPARE EQ chains over COUNT operands: jump when any arm differs
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["CONST", 7],
            ["CONST", 8],
            ["CHECK_COMPARE", (EQ, 3, "fail", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_in_jumps_when_not_contained(self):
        """
        CHECK_COMPARE IN jumps when value not in container
        """

        code, ns = build_expr([
            ["CONST", 99],
            ["CONST", [1, 2, 3]],
            ["CHECK_COMPARE", (IN, 2, "miss", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_in_continues_when_contained(self):
        """
        CHECK_COMPARE IN does not jump when value in container
        """

        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", [1, 2, 3]],
            ["CHECK_COMPARE", (IN, 2, "miss", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_check_is_jumps_when_different_identity(self):
        """
        CHECK_COMPARE IS jumps when left is not right
        """

        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["CHECK_COMPARE", (IS, 2, "diff", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_is_continues_when_same_identity(self):
        """
        CHECK_COMPARE IS does not jump when left is right
        """

        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["CHECK_COMPARE", (IS, 2, "diff", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_is_instance_jumps_when_false(self):
        """
        CHECK_COMPARE IS_INSTANCE jumps when isinstance(value, cls) is False
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", str],
            ["CHECK_COMPARE", (IS_INSTANCE, 2, "no", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_is_instance_continues_when_true(self):
        """
        CHECK_COMPARE IS_INSTANCE does not jump when isinstance(value, cls) is True
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["CHECK_COMPARE", (IS_INSTANCE, 2, "no", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_check_is_same_identity_no_jump(self):
        """
        CHECK_COMPARE IS does not jump when left and right are same object
        """

        same = []
        code, ns = build_expr([
            ["CONST", same],
            ["CONST", same],
            ["CHECK_COMPARE", (IS, 2, "diff", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_is_not_same_identity_jumps(self):
        """
        CHECK_COMPARE IS_NOT jumps when left and right are same object
        """

        same = []
        code, ns = build_expr([
            ["CONST", same],
            ["CONST", same],
            ["CHECK_COMPARE", (IS_NOT, 2, "same_obj", 0)],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "same_obj"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_not_in_jumps_when_contained(self):
        """
        CHECK_COMPARE NOT_IN jumps when value in container
        """
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", [1, 2, 3]],
            ["CHECK_COMPARE", (NOT_IN, 2, "in_it", 0)],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "in_it"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_not_in_continues_when_not_contained(self):
        """
        CHECK_COMPARE NOT_IN does not jump when value not in container
        """

        code, ns = build_expr([
            ["CONST", 99],
            ["CONST", [1, 2, 3]],
            ["CHECK_COMPARE", (NOT_IN, 2, "in_it", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "in_it"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_is_subclass_jumps_when_false(self):
        """
        CHECK_COMPARE IS_SUBCLASS jumps when issubclass(x, cls) is False
        """

        code, ns = build_expr([
            ["CONST", int],
            ["CONST", str],
            ["CHECK_COMPARE", (IS_SUBCLASS, 2, "no", 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_check_is_subclass_continues_when_true(self):
        """
        CHECK_COMPARE IS_SUBCLASS does not jump when issubclass(x, cls) is True
        """

        code, ns = build_expr([
            ["CONST", bool],
            ["CONST", int],
            ["CHECK_COMPARE", (IS_SUBCLASS, 2, "no", 0)],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_check_exception_jumps_when_no_exception_set(self):
        """
        CHECK_EXCEPTION jumps to handler when no exception is set
        """

        code, ns = build_expr([
            ["CONST", ValueError],
            ["CHECK_EXCEPTION", "handler"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "handler"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


# The end.
