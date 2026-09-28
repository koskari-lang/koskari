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
Tests for the join-constant elision pass of the bytecode processor: a
constant every path pushes into a label whose one consumer discards it
is removed from every path, and only then

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import OpCode

from . import make_fragment


pytestmark = pytest.mark.phase_compile


C = OpCode.CONST
CALL = OpCode.CALL
CONT = OpCode.CONTINUE
J = OpCode.JUMP
JF = OpCode.JUMP_FALSE
L = OpCode.LABEL
P = OpCode.POSITION
POP = OpCode.POP
RET = OpCode.RETURN
V = OpCode.VAR


def elide(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.elide_join_constants()
    return proc.instructions


class TestPopConsumer(unittest.TestCase):
    """
    A POP 1 after a label every edge reaches with a CONST goes with the
    constants
    """

    def test_two_jump_edges_and_fallthrough(self):
        out = elide([
            (V, "x"), (JF, "b"),
            (C, 1), (J, "done"),
            (L, "b"), (V, "y"), (JF, "c"),
            (C, 2), (J, "done"),
            (L, "c"), (C, 3),
            (L, "done"), (POP, 1),
            (C, 7), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "x"), (JF, "b"),
            (J, "done"),
            (L, "b"), (V, "y"), (JF, "c"),
            (J, "done"),
            (L, "c"),
            (L, "done"),
            (C, 7), (RET, 1),
        ])

    def test_positions_are_transparent(self):
        out = elide([
            (C, 1), (P, 3), (J, "done"),
            (L, "z"), (C, 2), (P, 4),
            (L, "done"), (P, 5), (POP, 1),
            (RET, 0),
        ])
        self.assertEqual(out, [
            (P, 3), (J, "done"),
            (L, "z"), (P, 4),
            (L, "done"), (P, 5),
            (RET, 0),
        ])

    def test_call_edge_keeps_everything(self):
        # (begin (if x (x) 0) 7): the call edge is not a constant, so the
        # POP stays and so does the CONST 0 on the other edge
        instr = [
            (V, "x"), (JF, "else"),
            (V, "x"), (CALL, 0), (J, "done"),
            (L, "else"), (C, 0),
            (L, "done"), (POP, 1),
            (C, 7), (RET, 1),
        ]
        self.assertEqual(elide(instr), instr)

    def test_conditional_jump_reference_keeps_everything(self):
        instr = [
            (V, "x"), (JF, "done"),
            (C, 1),
            (L, "done"), (POP, 1),
            (RET, 0),
        ]
        self.assertEqual(elide(instr), instr)

    def test_extra_row_label_reference_keeps_everything(self):
        instr = [
            (V, "x"), (V, "y"),
            (OpCode.CHECK_COMPARE, (OpCode.CHECK_COMPARE, 2, "done", 0)),
            (C, 1),
            (L, "done"), (POP, 1),
            (RET, 0),
        ]
        self.assertEqual(elide(instr), instr)

    def test_handler_label_reference_keeps_everything(self):
        instr = [
            (OpCode.PUSH_TRY, "done"),
            (C, 1),
            (L, "done"), (POP, 1),
            (RET, 0),
        ]
        self.assertEqual(elide(instr), instr)

    def test_pop_two_keeps_everything(self):
        instr = [
            (C, 1), (J, "done"),
            (L, "z"), (C, 2),
            (L, "done"), (POP, 2),
            (RET, 0),
        ]
        self.assertEqual(elide(instr), instr)

    def test_unreachable_fallthrough_is_not_an_edge(self):
        out = elide([
            (C, 1), (J, "done"),
            (L, "z"), (V, "q"), (RET, 1),
            (L, "done"), (POP, 1),
            (RET, 0),
        ])
        self.assertEqual(out, [
            (J, "done"),
            (L, "z"), (V, "q"), (RET, 1),
            (L, "done"),
            (RET, 0),
        ])

    def test_non_constant_fallthrough_keeps_everything(self):
        instr = [
            (C, 1), (J, "done"),
            (L, "z"), (V, "q"),
            (L, "done"), (POP, 1),
            (RET, 0),
        ]
        self.assertEqual(elide(instr), instr)

    def test_label_at_entry_keeps_everything(self):
        instr = [
            (L, "top"), (POP, 1),
            (C, 1), (J, "top"),
        ]
        self.assertEqual(elide(instr), instr)


class TestControlConsumer(unittest.TestCase):
    """
    A one-value control op after a label every edge reaches with None
    takes its zero-arg form
    """

    def test_continue_over_none_edges(self):
        out = elide([
            (V, "x"), (JF, "b"),
            (C, None), (J, "done"),
            (L, "b"), (C, None),
            (L, "done"), (CONT, 1),
        ])
        self.assertEqual(out, [
            (V, "x"), (JF, "b"),
            (J, "done"),
            (L, "b"),
            (L, "done"), (CONT, 0),
        ])

    def test_return_over_none_edges(self):
        out = elide([
            (C, None), (J, "done"),
            (L, "b"), (C, None),
            (L, "done"), (RET, 1),
        ])
        self.assertEqual(out, [
            (J, "done"),
            (L, "b"),
            (L, "done"), (RET, 0),
        ])

    def test_mixed_constants_keep_everything(self):
        instr = [
            (C, None), (J, "done"),
            (L, "b"), (C, 0),
            (L, "done"), (CONT, 1),
        ]
        self.assertEqual(elide(instr), instr)

    def test_control_of_two_keeps_everything(self):
        instr = [
            (C, None), (J, "done"),
            (L, "b"), (C, None),
            (L, "done"), (RET, 2),
        ]
        self.assertEqual(elide(instr), instr)


# The end.
