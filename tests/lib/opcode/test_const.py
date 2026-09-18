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
Tests for CONST, ITEM_CONST opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestConstOpcodes(CevalTest):
    """
    Tests for CONST, ITEM_CONST, ITEM_CONST_CLEAR, ITEM_CONST_REF, ITEM_CONST_STORE
    """

    async def test_const_return_single_value(self):
        """
        CONST pushes value; RETURN 1 returns it
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_const_item_getitem(self):
        """
        ITEM_CONST pushes TOS[key] with key from constant pool
        """

        code, ns = build_expr([
            ["CONST", {"a": 10, "b": 20}],
            ["ITEM_CONST", "b"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 20)


    async def test_const_item_store_setitem(self):
        """
        ITEM_CONST_STORE does TOS1[key] = TOS; key from constant pool
        """

        code, ns = build_expr([
            ["CONST", {}],
            ["DUP", 1],
            ["CONST", "x"],
            ["ITEM_CONST_STORE", "key"],
            ["ITEM_CONST", "key"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, "x")


    async def test_const_item_clear_delitem(self):
        """
        ITEM_CONST_CLEAR deletes TOS[key]
        """

        d = {"z": 1}
        code, ns = build_expr([
            ["CONST", d],
            ["ITEM_CONST_CLEAR", "z"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertNotIn("z", d)


    async def test_const_item_ref_pushes_item_ref(self):
        """
        ITEM_CONST_REF pushes ItemRef for TOS[key]
        """

        from koskari.lib import ItemRef
        d = {"k": "v"}
        code, ns = build_expr([
            ["CONST", d],
            ["ITEM_CONST_REF", "k"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, ItemRef)
        self.assertEqual(result.deref(), "v")


class TestConst2Opcode(CevalTest):
    """
    Tests for the CONST2 bulk constant load
    """

    async def test_const2_pushes_second_on_top(self):
        """
        CONST2 pushes both constants in row order; TOS is the second
        """

        code, ns = build_expr([
            ["CONST2", (10, 20, None, None)],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 20)


    async def test_const2_pushes_first_beneath(self):
        """
        Popping TOS after CONST2 leaves the first constant
        """

        code, ns = build_expr([
            ["CONST2", (10, 20, None, None)],
            ["POP", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


    async def test_const2_same_value_both_slots(self):
        """
        Both slots may intern to the same pool constant
        """

        code, ns = build_expr([
            ["CONST2", (7, 7, None, None)],
            ["POP", 1],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 7)


# The end.
