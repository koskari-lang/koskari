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
Boundary tests for the two-operand PyLong fast path in the arithmetic
and bitwise opcodes (kos_long_binop_fast).

Every case asserts against the equivalent Python expression so both the
fast path and its fallback edges must agree with PyNumber semantics:
in-range ints take the C long long route, while overflow, negative
shifts, huge operands, non-int operands, and non-binary operand counts
must all fall back to the generic reduce/binary path.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


INT64_MAX = 2 ** 63 - 1
INT64_MIN = -(2 ** 63)


class LongFastPathTest(CevalTest):

    async def run_binop(self, opname, left, right):
        code, ns = build_expr([
            ["CONST", left],
            ["CONST", right],
            [opname, 2],
            ["RETURN", 1],
        ])
        return await make_callable(code, ns)()


class TestLongFastPathHits(LongFastPathTest):
    """
    Operand pairs that qualify for the C long long fast path
    """

    async def test_small_ints(self):
        cases = [
            ("OBJECT_ADD", 2, 3, 5),
            ("OBJECT_SUBTRACT", 2, 5, -3),
            ("OBJECT_MULTIPLY", 7, 6, 42),
            ("OBJECT_BIT_AND", 0xFF, 0x0F, 0x0F),
            ("OBJECT_BIT_OR", 5, 2, 7),
            ("OBJECT_BIT_XOR", 5, 3, 6),
            ("OBJECT_BIT_LEFT", 1, 10, 1 << 10),
            ("OBJECT_BIT_RIGHT", 1024, 3, 128),
        ]
        for opname, left, right, expected in cases:
            with self.subTest(opname=opname):
                result = await self.run_binop(opname, left, right)
                self.assertEqual(result, expected)
                self.assertIsInstance(result, int)

    async def test_two_digit_values(self):
        # 32-bit values occupy two CPython digits; the fast path must
        # still take them (this is the churn workload shape)
        x = 0xDEADBEEF
        mask = 0xFFFFFFFF
        cases = [
            ("OBJECT_BIT_AND", x, mask, x & mask),
            ("OBJECT_BIT_XOR", x, x >> 13, x ^ (x >> 13)),
            ("OBJECT_MULTIPLY", x, 1664525, x * 1664525),
            ("OBJECT_ADD", x, 1013904223, x + 1013904223),
            ("OBJECT_BIT_LEFT", x, 13, x << 13),
            ("OBJECT_BIT_RIGHT", x, 17, x >> 17),
        ]
        for opname, left, right, expected in cases:
            with self.subTest(opname=opname):
                self.assertEqual(
                    await self.run_binop(opname, left, right), expected)

    async def test_negative_bitwise(self):
        # bitwise ops on negatives are exact in two's complement
        cases = [
            ("OBJECT_BIT_AND", -1, 255, -1 & 255),
            ("OBJECT_BIT_AND", -7, -13, -7 & -13),
            ("OBJECT_BIT_OR", -8, 3, -8 | 3),
            ("OBJECT_BIT_XOR", -7, 13, -7 ^ 13),
        ]
        for opname, left, right, expected in cases:
            with self.subTest(opname=opname, left=left, right=right):
                self.assertEqual(
                    await self.run_binop(opname, left, right), expected)

    async def test_negative_arithmetic(self):
        cases = [
            ("OBJECT_ADD", -5, 3, -2),
            ("OBJECT_SUBTRACT", -5, -3, -2),
            ("OBJECT_MULTIPLY", -7, 6, -42),
        ]
        for opname, left, right, expected in cases:
            with self.subTest(opname=opname):
                self.assertEqual(
                    await self.run_binop(opname, left, right), expected)

    async def test_rshift_by_large_count(self):
        # b >= 63 short-circuits to 0 for non-negative left operands
        self.assertEqual(await self.run_binop("OBJECT_BIT_RIGHT", 5, 63), 0)
        self.assertEqual(await self.run_binop("OBJECT_BIT_RIGHT", 5, 100), 0)
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_RIGHT", INT64_MAX, 200), 0)


class TestLongFastPathFallbacks(LongFastPathTest):
    """
    Operand pairs that must fall back to the generic path and still
    produce exact Python int semantics
    """

    async def test_add_overflow(self):
        self.assertEqual(
            await self.run_binop("OBJECT_ADD", INT64_MAX, 1),
            INT64_MAX + 1)
        self.assertEqual(
            await self.run_binop("OBJECT_SUBTRACT", INT64_MIN, 1),
            INT64_MIN - 1)

    async def test_multiply_overflow(self):
        big = 2 ** 62
        self.assertEqual(
            await self.run_binop("OBJECT_MULTIPLY", big, 4), big * 4)

    async def test_huge_operands(self):
        huge = 2 ** 65 - 1
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_AND", huge, 0xFFFFFFFF),
            huge & 0xFFFFFFFF)
        self.assertEqual(
            await self.run_binop("OBJECT_ADD", huge, huge), huge + huge)

    async def test_lshift_overflow(self):
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_LEFT", 1, 100), 1 << 100)
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_LEFT", INT64_MAX, 1),
            INT64_MAX << 1)

    async def test_negative_shifts_defer(self):
        # negative left operand: C >> is implementation-defined, so the
        # generic path must produce Python's floor semantics
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_RIGHT", -8, 1), -8 >> 1)
        self.assertEqual(
            await self.run_binop("OBJECT_BIT_LEFT", -2, 3), -2 << 3)

    async def test_negative_shift_count_raises(self):
        with self.assertRaises(ValueError):
            await self.run_binop("OBJECT_BIT_RIGHT", 1, -1)
        with self.assertRaises(ValueError):
            await self.run_binop("OBJECT_BIT_LEFT", 1, -1)

    async def test_bool_operands(self):
        # bool is a PyLong subtype; CheckExact must exclude it and the
        # generic path still yields ints
        result = await self.run_binop("OBJECT_ADD", True, True)
        self.assertEqual(result, 2)
        self.assertIsInstance(result, int)

    async def test_non_int_operands(self):
        self.assertEqual(
            await self.run_binop("OBJECT_ADD", 1.5, 2), 3.5)
        self.assertEqual(
            await self.run_binop("OBJECT_MULTIPLY", "ab", 3), "ababab")

    async def test_reducing_three_operands(self):
        # opa != 2 skips the fast path entirely
        code, ns = build_expr([
            ["CONST", 0xFF],
            ["CONST", 0x3F],
            ["CONST", 0x0F],
            ["OBJECT_BIT_AND", 3],
            ["RETURN", 1],
        ])
        self.assertEqual(
            await make_callable(code, ns)(), 0xFF & 0x3F & 0x0F)

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["OBJECT_ADD", 3],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 6)


# The end.
