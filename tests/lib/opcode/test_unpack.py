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
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
Tests for UNPACK, UNPACK_STAR, UNPACK_VAR, UNPACK_FAST opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestUnpackOpcodes(CevalTest):
    """
    Tests for sequence unpack opcodes
    """

    async def test_unpack_zero(self):
        """
        UNPACK 0 accepts empty sequence and pushes nothing
        """

        code, ns = build_expr([
            ["CONST", ()],
            ["UNPACK", 0],
            ["CONST", None],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)


    async def test_unpack_three_stack_order(self):
        """
        UNPACK 3 pushes so RETURN 3 yields (seq[2], seq[1], seq[0]) bottom-up
        """

        code, ns = build_expr([
            ["CONST", (1, 2, 3)],
            ["UNPACK", 3],
            ["RETURN", 3],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (3, 2, 1))


    async def test_unpack_length_mismatch_raises(self):
        """
        UNPACK n raises ValueError when sequence length differs from n
        """

        code, ns = build_expr([
            ["CONST", (1, 2)],
            ["UNPACK", 3],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_unpack_star_2_1(self):
        """
        UNPACK_STAR (2,1) on four elements: suffix, middle tuple, prefix order
        """

        code, ns = build_expr([
            ["CONST", (1, 2, 3, 4)],
            ["UNPACK_STAR", (2, 1, 0, 0)],
            ["RETURN", 4],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (4, (3,), 2, 1))


    async def test_unpack_star_left_only(self):
        """
        UNPACK_STAR (3, 0): prefix three and middle only (no suffix)
        """

        code, ns = build_expr([
            ["CONST", (1, 2, 3, 4)],
            ["UNPACK_STAR", (3, 0, 0, 0)],
            ["RETURN", 4],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, ((4,), 3, 2, 1))


    async def test_unpack_star_right_only(self):
        """
        UNPACK_STAR (0, 2): suffix two and middle is head
        """

        code, ns = build_expr([
            ["CONST", (1, 2, 3, 4)],
            ["UNPACK_STAR", (0, 2, 0, 0)],
            ["RETURN", 3],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, (4, 3, (1, 2)))


    async def test_unpack_star_both_zero_raises(self):
        """
        UNPACK_STAR (0, 0) raises ValueError
        """

        code, ns = build_expr([
            ["CONST", (1, 2)],
            ["UNPACK_STAR", (0, 0, 0, 0)],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_unpack_star_too_short_raises(self):
        """
        UNPACK_STAR raises when len(seq) < left + right
        """

        code, ns = build_expr([
            ["CONST", (1, 2)],
            ["UNPACK_STAR", (2, 1, 0, 0)],
            ["RETURN", 1],
        ])
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_unpack_var_stores_two_names(self):
        """
        UNPACK_VAR binds seq[0] and seq[1] to two VAR names
        """

        code, ns = build_expr([
            ["CONST", (10, 20)],
            ["UNPACK_VAR", ("x", "y", 0, 0)],
            ["RETURN", 0],
        ], local_syms=("x", "y"))
        await make_callable(code, ns)()
        self.assertEqual(ns.deref("x"), 10)
        self.assertEqual(ns.deref("y"), 20)


    async def test_unpack_var_length_mismatch_raises(self):
        """
        UNPACK_VAR requires exactly length 2
        """

        code, ns = build_expr([
            ["CONST", (1,)],
            ["UNPACK_VAR", ("x", "y", 0, 0)],
            ["RETURN", 0],
        ], local_syms=("x", "y"))
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_unpack_fast_stores_two_slots(self):
        """
        UNPACK_FAST writes seq[0] and seq[1] into fast slots 0 and 1
        """

        code, ns = build_expr([
            ["CONST", (30, 40)],
            ["UNPACK_FAST", (0, 1, 0, 0)],
            ["FAST", 0],
            ["FAST", 1],
            ["RETURN", 2],
        ], local_syms=("a", "b"))
        result = await make_callable(code, ns)()
        self.assertEqual(result, (30, 40))


    async def test_unpack_fast_out_of_bounds_raises(self):
        """
        UNPACK_FAST raises NameError when a fast index is out of bounds
        """

        code, ns = build_expr([
            ["CONST", (1, 2)],
            ["UNPACK_FAST", (0, 1, 0, 0)],
            ["RETURN", 0],
        ], local_syms=("a",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.
