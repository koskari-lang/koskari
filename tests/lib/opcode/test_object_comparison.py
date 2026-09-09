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
Tests for the OBJECT_COMPARE opcode across its Compare selectors.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from koskari.lib import Compare
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


def _cmp(sel, count):
    """
    Pack a CMPCOUNT arg from a Compare selector name and count
    """

    return (int(Compare[sel]) << 8) | count


class TestObjectComparisonOpcodes(CevalTest):
    """
    Tests for OBJECT_COMPARE under every Compare selector
    """

    async def test_object_eq(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", 42],
            ["OBJECT_COMPARE", _cmp("EQ", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_ne(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_COMPARE", _cmp("NE", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_lt(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_COMPARE", _cmp("LT", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_le(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_COMPARE", _cmp("LE", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_gt(self):
        code, ns = build_expr([
            ["CONST", 3],
            ["CONST", 2],
            ["OBJECT_COMPARE", _cmp("GT", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_ge(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_COMPARE", _cmp("GE", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_comparisons_chain_true(self):
        cases = (
            ("EQ", [7, 7, 7]),
            ("NE", [1, 2, 3]),
            ("LT", [1, 2, 3]),
            ("LE", [1, 2, 2]),
            ("GT", [3, 2, 1]),
            ("GE", [3, 2, 2]),
        )

        for opcode, values in cases:
            with self.subTest(opcode=opcode):
                expr = [["CONST", value] for value in values]
                expr.append(["OBJECT_COMPARE", _cmp(opcode, 3)])
                expr.append(["RETURN", 1])
                code, ns = build_expr(expr)
                self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_comparisons_chain_false(self):
        cases = (
            ("EQ", [7, 7, 8]),
            ("NE", [1, 2, 2]),
            ("LT", [1, 3, 2]),
            ("LE", [1, 2, 1]),
            ("GT", [3, 1, 2]),
            ("GE", [3, 2, 3]),
        )

        for opcode, values in cases:
            with self.subTest(opcode=opcode):
                expr = [["CONST", value] for value in values]
                expr.append(["OBJECT_COMPARE", _cmp(opcode, 3)])
                expr.append(["RETURN", 1])
                code, ns = build_expr(expr)
                self.assertEqual(await make_callable(code, ns)(), False)


    async def test_object_comparisons_reject_count_zero(self):
        opcodes = ("EQ", "NE", "LT", "LE", "GT", "GE")

        for opcode in opcodes:
            with self.subTest(opcode=opcode):
                code, ns = build_expr([
                    ["OBJECT_COMPARE", _cmp(opcode, 0)],
                    ["RETURN", 1],
                ])
                with self.assertRaises(TypeError):
                    await make_callable(code, ns)()


    async def test_object_comparisons_reject_count_one(self):
        opcodes = ("EQ", "NE", "LT", "LE", "GT", "GE")

        for opcode in opcodes:
            with self.subTest(opcode=opcode):
                code, ns = build_expr([
                    ["CONST", 1],
                    ["OBJECT_COMPARE", _cmp(opcode, 1)],
                    ["RETURN", 1],
                ])
                with self.assertRaises(TypeError):
                    await make_callable(code, ns)()


    async def test_object_in(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", [1, 2, 3]],
            ["OBJECT_COMPARE", _cmp("IN", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_not_in(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", [1, 2, 3]],
            ["OBJECT_COMPARE", _cmp("NOT_IN", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_is(self):
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["OBJECT_COMPARE", _cmp("IS", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_is_not(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["OBJECT_COMPARE", _cmp("IS_NOT", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_is_instance(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["OBJECT_COMPARE", _cmp("IS_INSTANCE", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


    async def test_object_is_subclass(self):
        code, ns = build_expr([
            ["CONST", bool],
            ["CONST", int],
            ["OBJECT_COMPARE", _cmp("IS_SUBCLASS", 2)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), True)


# The end.
