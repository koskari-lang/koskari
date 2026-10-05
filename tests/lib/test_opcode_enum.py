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

        self.assertIs(OpStyle.COUNT, OpCode.OBJECT_STR.style)


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
            if op.style == OpStyle.EXTRA:
                self.assertTrue(op.extra, op.name)
            else:
                self.assertEqual((), op.extra, op.name)


    async def test_extra_schema_fields_are_opstyle_members(self):
        """an EXTRA opcode's schema is four OpStyle field kinds"""

        for op in OpCode:
            if op.style != OpStyle.EXTRA:
                continue
            self.assertEqual(4, len(op.extra), op.name)
            for field in op.extra:
                self.assertIsInstance(field, OpStyle, op.name)


    async def test_extra_schema_spot_check(self):
        """CALL_ATTR indexes a row of (attr name, argument count)"""

        self.assertEqual(
            (OpStyle.CONST, OpStyle.COUNT,
             OpStyle.NOARG, OpStyle.NOARG),
            OpCode.CALL_ATTR.extra)


class TestOpCodeExtentFacts(unittest.IsolatedAsyncioTestCase):
    """
    Every member carries the extent facts the C side holds: the paired
    pop of a push, the paired push of a pop, and whether a tail call
    may pass through the extent a push opens
    """


    async def test_every_member_carries_the_attributes(self):
        for op in OpCode:
            self.assertIn(type(op.pop), (type(None), OpCode), op.name)
            self.assertIn(type(op.push), (type(None), OpCode), op.name)
            self.assertIsInstance(op.discardable, bool, op.name)


    async def test_push_and_pop_pair_each_other(self):
        pushes = [op for op in OpCode if op.pop is not None]
        pops = [op for op in OpCode if op.push is not None]
        self.assertEqual(13, len(pushes))
        self.assertEqual(13, len(pops))
        for push in pushes:
            self.assertIs(push, push.pop.push, push.name)
            self.assertIsNone(push.push, push.name)
        for pop in pops:
            self.assertIs(pop, pop.push.pop, pop.name)
            self.assertIsNone(pop.pop, pop.name)


    async def test_pairing_spot_check(self):
        self.assertIs(OpCode.POP_TRY, OpCode.PUSH_TRY.pop)
        self.assertIs(OpCode.PUSH_TRY, OpCode.POP_TRY.push)
        self.assertIsNone(OpCode.CONST.pop)
        self.assertIsNone(OpCode.CONST.push)


    async def test_discardable_is_the_tail_call_set(self):
        """
        a tail call passes through NS, LOOP and FOREACH, whose uninstall
        is a pure release, and through no other extent
        """

        discardable = frozenset(op for op in OpCode if op.discardable)
        self.assertEqual(
            frozenset((OpCode.PUSH_NS, OpCode.PUSH_LOOP,
                       OpCode.PUSH_FOREACH)),
            discardable)
        self.assertFalse(OpCode.PUSH_TRY.discardable)
        self.assertFalse(OpCode.POP_NS.discardable)


class TestOpCodeYields(unittest.IsolatedAsyncioTestCase):
    """
    Every member says whether it makes a code object a generator
    """


    async def test_yield_rows(self):
        yields = frozenset(op for op in OpCode if op.yields)
        self.assertEqual(
            frozenset((OpCode.YIELD, OpCode.YIELD_FROM, OpCode.YIELD_CONST,
                       OpCode.YIELD_VAR, OpCode.YIELD_FAST)),
            yields)
        for op in OpCode:
            self.assertIsInstance(op.yields, bool, op.name)


# The end.
