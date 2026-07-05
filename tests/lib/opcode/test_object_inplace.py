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
Tests for the stack-based OBJECT_INPLACE_* opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Fable 5 via Claude Code
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestObjectInplaceOpcodes(CevalTest):
    """
    Tests for OBJECT_INPLACE_ADD, OBJECT_INPLACE_SUBTRACT,
    OBJECT_INPLACE_MULTIPLY, OBJECT_INPLACE_POWER,
    OBJECT_INPLACE_FLOOR_DIVIDE, OBJECT_INPLACE_TRUE_DIVIDE,
    OBJECT_INPLACE_MODULO, OBJECT_INPLACE_MATRIX_MULTIPLY,
    OBJECT_INPLACE_BIT_LEFT, OBJECT_INPLACE_BIT_RIGHT,
    OBJECT_INPLACE_BIT_AND, OBJECT_INPLACE_BIT_OR, OBJECT_INPLACE_BIT_XOR
    """

    async def test_inplace_add(self):
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_INPLACE_ADD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_subtract(self):
        code, ns = build_expr([
            ["CONST", 50],
            ["CONST", 8],
            ["OBJECT_INPLACE_SUBTRACT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_multiply(self):
        code, ns = build_expr([
            ["CONST", 6],
            ["CONST", 7],
            ["OBJECT_INPLACE_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_power(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 5],
            ["OBJECT_INPLACE_POWER", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_inplace_floor_divide(self):
        code, ns = build_expr([
            ["CONST", 85],
            ["CONST", 2],
            ["OBJECT_INPLACE_FLOOR_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_true_divide(self):
        code, ns = build_expr([
            ["CONST", 84],
            ["CONST", 2],
            ["OBJECT_INPLACE_TRUE_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42.0)


    async def test_inplace_modulo(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_INPLACE_MODULO", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 2)


    async def test_inplace_matrix_multiply(self):
        class IMatMul(object):
            def __imatmul__(self, other):
                return 42
        code, ns = build_expr([
            ["CONST", IMatMul()],
            ["CONST", None],
            ["OBJECT_INPLACE_MATRIX_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_bit_left(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 5],
            ["OBJECT_INPLACE_BIT_LEFT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_inplace_bit_right(self):
        code, ns = build_expr([
            ["CONST", 64],
            ["CONST", 1],
            ["OBJECT_INPLACE_BIT_RIGHT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 32)


    async def test_inplace_bit_and(self):
        code, ns = build_expr([
            ["CONST", 0xFF],
            ["CONST", 0x0F],
            ["OBJECT_INPLACE_BIT_AND", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 15)


    async def test_inplace_bit_or(self):
        code, ns = build_expr([
            ["CONST", 32],
            ["CONST", 10],
            ["OBJECT_INPLACE_BIT_OR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_bit_xor(self):
        code, ns = build_expr([
            ["CONST", 63],
            ["CONST", 21],
            ["OBJECT_INPLACE_BIT_XOR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_add_mutates_list(self):
        """
        OBJECT_INPLACE_ADD extends a list in place and pushes the same
        list object, where OBJECT_ADD would build a copy
        """

        lst = [1, 2]
        code, ns = build_expr([
            ["CONST", lst],
            ["CONST", [3]],
            ["OBJECT_INPLACE_ADD", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, lst)
        self.assertEqual(lst, [1, 2, 3])


    async def test_inplace_multiply_mutates_list(self):
        lst = [1, 2]
        code, ns = build_expr([
            ["CONST", lst],
            ["CONST", 2],
            ["OBJECT_INPLACE_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, lst)
        self.assertEqual(lst, [1, 2, 1, 2])


    async def test_inplace_bit_or_mutates_set(self):
        s = {1, 2}
        code, ns = build_expr([
            ["CONST", s],
            ["CONST", {3}],
            ["OBJECT_INPLACE_BIT_OR", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, s)
        self.assertEqual(s, {1, 2, 3})


    async def test_inplace_subtract_mutates_set(self):
        s = {1, 2, 3}
        code, ns = build_expr([
            ["CONST", s],
            ["CONST", {2}],
            ["OBJECT_INPLACE_SUBTRACT", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, s)
        self.assertEqual(s, {1, 3})


    async def test_inplace_add_calls_iadd_dunder(self):
        class IAdd(object):
            def __init__(self):
                self.calls = []

            def __iadd__(self, other):
                self.calls.append(other)
                return self

        obj = IAdd()
        code, ns = build_expr([
            ["CONST", obj],
            ["CONST", 7],
            ["OBJECT_INPLACE_ADD", 2],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, obj)
        self.assertEqual(obj.calls, [7])


    async def test_inplace_add_three_values(self):
        """
        OBJECT_INPLACE_ADD 3 reduces left-to-right: 10 += 30 += 2 => 42
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 30],
            ["CONST", 2],
            ["OBJECT_INPLACE_ADD", 3],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_inplace_add_three_values_mutates_list(self):
        """
        the reduce applies each rhs to the same accumulator, so a list
        lhs is extended once per operand
        """

        lst = [1]
        code, ns = build_expr([
            ["CONST", lst],
            ["CONST", [2]],
            ["CONST", [3]],
            ["OBJECT_INPLACE_ADD", 3],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIs(result, lst)
        self.assertEqual(lst, [1, 2, 3])


    async def test_inplace_power_not_reducing(self):
        """
        OBJECT_INPLACE_POWER is strictly binary; arg 3 raises TypeError
        """

        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 3],
            ["CONST", 2],
            ["OBJECT_INPLACE_POWER", 3],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


    async def test_inplace_add_requires_two_values(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["OBJECT_INPLACE_ADD", 1],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


    async def test_inplace_add_type_error(self):
        code, ns = build_expr([
            ["CONST", "a"],
            ["CONST", 1],
            ["OBJECT_INPLACE_ADD", 2],
            ["RETURN", 1],
        ])
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()


# The end.
