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
Tests for CONST, CONST_ITEM opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


class TestConstOpcodes(CevalTest):
    """
    Tests for CONST, CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE
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
        CONST_ITEM pushes TOS[key] with key from constant pool
        """

        code, ns = build_expr([
            ["CONST", {"a": 10, "b": 20}],
            ["CONST_ITEM", "b"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 20)


    async def test_const_item_store_setitem(self):
        """
        CONST_ITEM_STORE does TOS1[key] = TOS; key from constant pool
        """

        code, ns = build_expr([
            ["CONST", {}],
            ["DUP", 1],
            ["CONST", "x"],
            ["CONST_ITEM_STORE", "key"],
            ["CONST_ITEM", "key"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, "x")


    async def test_const_item_clear_delitem(self):
        """
        CONST_ITEM_CLEAR deletes TOS[key]
        """

        d = {"z": 1}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST_ITEM_CLEAR", "z"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertNotIn("z", d)


    async def test_const_item_ref_pushes_item_ref(self):
        """
        CONST_ITEM_REF pushes ItemRef for TOS[key]
        """

        from koskari.lib import ItemRef
        d = {"k": "v"}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST_ITEM_REF", "k"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsInstance(result, ItemRef)
        self.assertEqual(result.deref(), "v")


# The end.
