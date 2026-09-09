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
Tests for the fused format opcodes VAR_FORMAT, VAR_FORMAT_CONST,
FAST_FORMAT, and FAST_FORMAT_CONST.

The no-rules forms take the name constant or slot index directly; the
_CONST forms carry an EXTRA schema (VAR, CONST, NOARG, NOARG) or
(FASTVAR, CONST, NOARG, NOARG) pairing the object with a constant rules
string.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class BadFormat(object):
    def __format__(self, rules):
        raise ValueError("no format for you")


class TestVarFormatOpcodes(CevalTest):
    """
    Tests for VAR_FORMAT and VAR_FORMAT_CONST
    """

    async def test_var_format(self):
        """
        VAR_FORMAT loads the name and pushes format(obj)
        """
        code, ns = build_expr([
            ["VAR_FORMAT", "x"],
            ["RETURN", 1],
        ], values={"x": 99})
        self.assertEqual(await make_callable(code, ns)(), "99")


    async def test_var_format_unbound_raises(self):
        """
        VAR_FORMAT on an unbound name raises NameError with nothing
        formatted
        """
        code, ns = build_expr([
            ["VAR_FORMAT", "x"],
            ["RETURN", 1],
        ], local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_var_format_raising_dunder(self):
        """
        VAR_FORMAT propagates an exception from __format__
        """
        code, ns = build_expr([
            ["VAR_FORMAT", "x"],
            ["RETURN", 1],
        ], values={"x": BadFormat()})
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_var_format_const(self):
        """
        VAR_FORMAT_CONST applies the constant rules string
        """
        code, ns = build_expr([
            ["VAR_FORMAT_CONST", ("x", "04d", None, None)],
            ["RETURN", 1],
        ], values={"x": 7})
        self.assertEqual(await make_callable(code, ns)(), "0007")


    async def test_var_format_const_float_rules(self):
        code, ns = build_expr([
            ["VAR_FORMAT_CONST", ("x", ".2f", None, None)],
            ["RETURN", 1],
        ], values={"x": 2.5})
        self.assertEqual(await make_callable(code, ns)(), "2.50")


    async def test_var_format_const_unbound_raises(self):
        code, ns = build_expr([
            ["VAR_FORMAT_CONST", ("x", "04d", None, None)],
            ["RETURN", 1],
        ], local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


class TestFastFormatOpcodes(CevalTest):
    """
    Tests for FAST_FORMAT and FAST_FORMAT_CONST
    """

    async def test_fast_format(self):
        """
        FAST_FORMAT loads the slot and pushes format(obj)
        """
        code, ns = build_expr([
            ["FAST_FORMAT", 0],
            ["RETURN", 1],
        ], values={"x": 99})
        self.assertEqual(await make_callable(code, ns)(), "99")


    async def test_fast_format_empty_slot_raises(self):
        """
        FAST_FORMAT on an empty slot raises NameError with nothing
        formatted
        """
        code, ns = build_expr([
            ["FAST_FORMAT", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_fast_format_raising_dunder(self):
        """
        FAST_FORMAT propagates an exception from __format__
        """
        code, ns = build_expr([
            ["FAST_FORMAT", 0],
            ["RETURN", 1],
        ], values={"x": BadFormat()})
        with self.assertRaises(ValueError):
            await make_callable(code, ns)()


    async def test_fast_format_const(self):
        """
        FAST_FORMAT_CONST applies the constant rules string
        """
        code, ns = build_expr([
            ["FAST_FORMAT_CONST", (0, "04d", None, None)],
            ["RETURN", 1],
        ], values={"x": 7})
        self.assertEqual(await make_callable(code, ns)(), "0007")


    async def test_fast_format_const_float_rules(self):
        code, ns = build_expr([
            ["FAST_FORMAT_CONST", (0, ".2f", None, None)],
            ["RETURN", 1],
        ], values={"x": 2.5})
        self.assertEqual(await make_callable(code, ns)(), "2.50")


# The end.
