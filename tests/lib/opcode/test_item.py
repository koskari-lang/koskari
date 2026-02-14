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
Tests for ITEM, ITEM_STORE opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestItemOpcodes(CevalTest):
    """
    Tests for ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE
    """

    def test_item_getitem(self):
        """
        ITEM pushes TOS1[TOS]
        """
        code, ns = build_expr([
            ["CONST", {"a": 1, "b": 2}],
            ["CONST", "b"],
            ["ITEM", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 2)


    def test_item_store_setitem(self):
        """
        ITEM_STORE does TOS2[TOS1] = TOS (val, key, obj popped)
        """
        code, ns = build_expr([
            ["CONST", {}],
            ["DUP", 1],
            ["CONST", "k"],
            ["CONST", "v"],
            ["ITEM_STORE", 0],
            ["CONST", "k"],
            ["ITEM", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, "v")


    def test_item_clear_delitem(self):
        """
        ITEM_CLEAR deletes TOS1[TOS]
        """
        d = {"x": 1}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST", "x"],
            ["ITEM_CLEAR", 0],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)
        self.assertNotIn("x", d)


    def test_item_ref_pushes_item_ref(self):
        """
        ITEM_REF pushes ItemRef for TOS1[TOS]
        """
        from koskari.lib import ItemRef
        d = {"k": "v"}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST", "k"],
            ["ITEM_REF", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, ItemRef)
        self.assertEqual(result.deref(), "v")

# The end.
