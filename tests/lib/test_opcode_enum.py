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
Unit tests for the OpCode enum decorations: the style and extra
attributes carried by every opcode member

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import OpCode, OpStyle


pytestmark = pytest.mark.phase_lib


class TestOpCodeStyle(unittest.IsolatedAsyncioTestCase):
    """Tests for OpCode.style"""


    async def test_style_is_an_opstyle_member(self):
        """every opcode carries its style as an OpStyle member"""

        for op in OpCode:
            self.assertIsInstance(op.style, OpStyle, op.name)


    async def test_style_spot_check(self):
        """OBJECT_STR is a COUNT-style opcode"""

        self.assertIs(OpStyle.KOSTYLE_COUNT, OpCode.KOS_OP_OBJECT_STR.style)


class TestOpCodeExtra(unittest.IsolatedAsyncioTestCase):
    """Tests for OpCode.extra"""


    async def test_extra_is_always_a_tuple(self):
        """every opcode carries an extra schema, EXTRA-style or not"""

        for op in OpCode:
            self.assertIsInstance(op.extra, tuple, op.name)


    async def test_extra_empty_unless_extra_style(self):
        """
        non-EXTRA opcodes have an empty schema, so consumers can test it
        for truth rather than checking the style first
        """

        for op in OpCode:
            if op.style == OpStyle.KOSTYLE_EXTRA:
                self.assertTrue(op.extra, op.name)
            else:
                self.assertEqual((), op.extra, op.name)


    async def test_extra_schema_fields_are_opstyle_members(self):
        """an EXTRA opcode's schema is four OpStyle field kinds"""

        for op in OpCode:
            if op.style != OpStyle.KOSTYLE_EXTRA:
                continue
            self.assertEqual(4, len(op.extra), op.name)
            for field in op.extra:
                self.assertIsInstance(field, OpStyle, op.name)


    async def test_extra_schema_spot_check(self):
        """CALL_ATTR indexes a row of (attr name, argument count)"""

        self.assertEqual(
            (OpStyle.KOSTYLE_CONST, OpStyle.KOSTYLE_COUNT,
             OpStyle.KOSTYLE_NOARG, OpStyle.KOSTYLE_NOARG),
            OpCode.KOS_OP_CALL_ATTR.extra)


# The end.
