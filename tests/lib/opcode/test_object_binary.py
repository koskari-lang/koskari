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
Tests for OBJECT_ADD opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestObjectBinaryOpcodes(CevalTest):
    """
    Tests for OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY,
    OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_BIT_AND, OBJECT_BIT_OR,
    OBJECT_BIT_XOR, OBJECT_MATRIX_MULTIPLY, OBJECT_POWER, OBJECT_MODULO,
    OBJECT_DIVMOD, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT
    """

    async def test_object_add(self):
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_subtract(self):
        code, ns = build_expr([
            ["CONST", 50],
            ["CONST", 8],
            ["OBJECT_SUBTRACT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_multiply(self):
        code, ns = build_expr([
            ["CONST", 6],
            ["CONST", 7],
            ["OBJECT_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_floor_divide(self):
        code, ns = build_expr([
            ["CONST", 85],
            ["CONST", 2],
            ["OBJECT_FLOOR_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_true_divide(self):
        code, ns = build_expr([
            ["CONST", 84],
            ["CONST", 2],
            ["OBJECT_TRUE_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42.0)


    async def test_object_bit_and(self):
        code, ns = build_expr([
            ["CONST", 0xFF],
            ["CONST", 0x0F],
            ["OBJECT_BIT_AND", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 15)


    async def test_object_bit_or(self):
        code, ns = build_expr([
            ["CONST", 32],
            ["CONST", 10],
            ["OBJECT_BIT_OR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_bit_xor(self):
        code, ns = build_expr([
            ["CONST", 63],
            ["CONST", 21],
            ["OBJECT_BIT_XOR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_matrix_multiply(self):
        class MatMulResult(object):
            def __matmul__(self, other):
                return 42
        code, ns = build_expr([
            ["CONST", MatMulResult()],
            ["CONST", None],
            ["OBJECT_MATRIX_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_object_power(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 5],
            ["OBJECT_POWER", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_object_modulo(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_MODULO", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 2)


    async def test_object_divmod(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_DIVMOD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), (3, 2))


    async def test_object_bit_left(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 5],
            ["OBJECT_BIT_LEFT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_object_bit_right(self):
        code, ns = build_expr([
            ["CONST", 64],
            ["CONST", 1],
            ["OBJECT_BIT_RIGHT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_object_add_three_values(self):
        """
        OBJECT_ADD 3 reduces 1+2+3 => 6
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["OBJECT_ADD", 3],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 6)


    async def test_object_add_type_error(self):
        """
        OBJECT_ADD with incompatible types raises TypeError
        """

        code, ns = build_expr([
            ["CONST", "a"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


# The end.
