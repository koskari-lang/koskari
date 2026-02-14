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

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestObjectBinaryOpcodes(CevalTest):
    """
    Tests for OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY,
    OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_BIT_AND, OBJECT_BIT_OR,
    OBJECT_BIT_XOR, OBJECT_MATRIX_MULTIPLY, OBJECT_POWER, OBJECT_MODULO,
    OBJECT_DIVMOD, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT
    """

    def test_object_add(self):
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_subtract(self):
        code, ns = build_expr([
            ["CONST", 50],
            ["CONST", 8],
            ["OBJECT_SUBTRACT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_multiply(self):
        code, ns = build_expr([
            ["CONST", 6],
            ["CONST", 7],
            ["OBJECT_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_floor_divide(self):
        code, ns = build_expr([
            ["CONST", 85],
            ["CONST", 2],
            ["OBJECT_FLOOR_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_true_divide(self):
        code, ns = build_expr([
            ["CONST", 84],
            ["CONST", 2],
            ["OBJECT_TRUE_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42.0)


    def test_object_bit_and(self):
        code, ns = build_expr([
            ["CONST", 0xFF],
            ["CONST", 0x0F],
            ["OBJECT_BIT_AND", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 15)


    def test_object_bit_or(self):
        code, ns = build_expr([
            ["CONST", 32],
            ["CONST", 10],
            ["OBJECT_BIT_OR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_bit_xor(self):
        code, ns = build_expr([
            ["CONST", 63],
            ["CONST", 21],
            ["OBJECT_BIT_XOR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_matrix_multiply(self):
        class MatMulResult(object):
            def __matmul__(self, other):
                return 42
        code, ns = build_expr([
            ["CONST", MatMulResult()],
            ["CONST", None],
            ["OBJECT_MATRIX_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_power(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 5],
            ["OBJECT_POWER", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)


    def test_object_modulo(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_MODULO", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 2)


    def test_object_divmod(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_DIVMOD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), (3, 2))


    def test_object_bit_left(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 5],
            ["OBJECT_BIT_LEFT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)


    def test_object_bit_right(self):
        code, ns = build_expr([
            ["CONST", 64],
            ["CONST", 1],
            ["OBJECT_BIT_RIGHT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)

# The end.
