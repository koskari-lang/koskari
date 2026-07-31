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
Tests for the stack-free augmented-assignment opcodes
VAR_INPLACE_CONST and FAST_INPLACE_CONST

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.lib import Operand

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


ADD = int(Operand.ADD)
SUBTRACT = int(Operand.SUBTRACT)


class TestVarObjectInplaceConst(CevalTest):
    """
    VAR_INPLACE_CONST applies obj OP= const through the frame
    namespace with no stack traffic
    """

    async def test_add(self):
        code, ns = build_expr([
            ["VAR_INPLACE_CONST", (ADD, "i", 5, 0)],
            ["VAR", "i"],
            ["RETURN", 1],
        ], values={"i": 10})
        self.assertEqual(await make_callable(code, ns)(), 15)


    async def test_subtract(self):
        code, ns = build_expr([
            ["VAR_INPLACE_CONST", (SUBTRACT, "i", 3, 0)],
            ["VAR", "i"],
            ["RETURN", 1],
        ], values={"i": 10})
        self.assertEqual(await make_callable(code, ns)(), 7)


    async def test_missing_name_raises(self):
        code, ns = build_expr([
            ["VAR_INPLACE_CONST", (ADD, "nope", 1, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestFastObjectInplaceConst(CevalTest):
    """
    FAST_INPLACE_CONST applies slot OP= const with no stack
    traffic
    """

    async def test_add(self):
        # slot stored explicitly for a deterministic index
        code, ns = build_expr([
            ["CONST", 10],
            ["FAST_STORE", 0],
            ["FAST_INPLACE_CONST", (ADD, 0, 5, 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], local_syms=("i",))
        self.assertEqual(await make_callable(code, ns)(), 15)


    async def test_string_concat(self):
        code, ns = build_expr([
            ["CONST", "ab"],
            ["FAST_STORE", 0],
            ["FAST_INPLACE_CONST", (ADD, 0, "c", 0)],
            ["FAST", 0],
            ["RETURN", 1],
        ], local_syms=("s",))
        self.assertEqual(await make_callable(code, ns)(), "abc")


    async def test_unbound_slot_raises(self):
        code, ns = build_expr([
            ["FAST_INPLACE_CONST", (ADD, 0, 1, 0)],
            ["CONST", None],
            ["RETURN", 1],
        ], local_syms=("i",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.
