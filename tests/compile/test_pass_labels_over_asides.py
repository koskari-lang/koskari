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
tests.compile.test_pass_labels_over_asides

Tests for the bytecode processor pass that moves a LABEL leading to a
PUSH_ASIDE to after the aside's POP_ASIDE. The frame an aside is
written in only passes over the aside's instructions, so a jump to the
label goes where the PUSH_ASIDE would have taken it

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
JF = OpCode.JUMP_FALSE
L = OpCode.LABEL
NOOP = OpCode.NOOP
P = OpCode.POSITION
POPA = OpCode.POP_ASIDE
PUSHA = OpCode.PUSH_ASIDE
RESET = OpCode.RESET
RET = OpCode.RETURN
V = OpCode.VAR


def aside(name, *rows):
    return [(PUSHA, (None, name, 0, 0)), *rows, (RET, 1), (POPA, 0)]


def passed(instr, holds_aside=True):
    frag = make_fragment(instr)
    frag.holds_aside = holds_aside
    proc = ByteCodeProcessor(frag)
    proc.pass_labels_over_asides()
    return proc.instructions


class TestPassLabelsOverAsides(unittest.TestCase):

    def test_label_at_an_aside_moves_after_it(self):
        out = passed([
            (V, "a"), (JF, "else"), (C, 1), (RET, 1),
            (L, "else"),
            *aside("body", (C, 2)),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ])
        self.assertEqual([
            (V, "a"), (JF, "else"), (C, 1), (RET, 1),
            *aside("body", (C, 2)),
            (L, "else"),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ], out)


    def test_label_moves_past_position_and_noop_rows(self):
        out = passed([
            (L, "a"), (P, (1, 2)), (L, "b"), (NOOP, 0),
            *aside("body", (C, 2)),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ])
        self.assertEqual([
            (P, (1, 2)), (NOOP, 0),
            *aside("body", (C, 2)),
            (L, "a"), (L, "b"),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ], out)


    def test_label_at_another_instruction_stays(self):
        instr = [
            (L, "top"), (C, 1),
            *aside("body", (C, 2)),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ]
        self.assertEqual(instr, passed(instr))


    def test_label_moves_past_asides_that_follow_one_another(self):
        out = passed([
            (L, "top"),
            *aside("before", (C, 1)),
            *aside("after", (C, 2)),
            (RET, 0),
        ])
        self.assertEqual([
            *aside("before", (C, 1)),
            *aside("after", (C, 2)),
            (L, "top"),
            (RET, 0),
        ], out)


    def test_label_inside_an_aside_stays_inside_it(self):
        out = passed([
            (PUSHA, (None, "body", 0, 0)),
            (L, "inner"),
            *aside("handler", (C, 1)),
            (C, 2),
            (RET, 1),
            (POPA, 0),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ])
        self.assertEqual([
            (PUSHA, (None, "body", 0, 0)),
            *aside("handler", (C, 1)),
            (L, "inner"),
            (C, 2),
            (RET, 1),
            (POPA, 0),
            (RESET, (0, "body", 0, 0)), (RET, 1),
        ], out)


    def test_stream_without_an_aside_is_untouched(self):
        instr = [(L, "top"), (C, 1), (RET, 1)]
        self.assertEqual(instr, passed(instr, holds_aside=False))


    def test_label_a_threaded_jump_inserts_at_an_aside_moves(self):
        # the JUMP_OR threads to the fall-through of the JUMP_FALSE it
        # leads to, where no label is, so one is inserted there, at
        # the aside
        frag = make_fragment([
            (V, "a"), (OpCode.JUMP_OR, "test"), (V, "b"),
            (L, "test"), (JF, "end"),
            *aside("body", (C, 2)),
            (RESET, (0, "body", 0, 0)), (RET, 1),
            (L, "end"), (C, 3), (RET, 1),
        ])
        frag.holds_aside = True
        out = ByteCodeProcessor(frag).process()

        ops = [op for op, _ in out]
        self.assertNotIn(OpCode.JUMP_OR, ops)
        self.assertIn(PUSHA, ops)
        for i, op in enumerate(ops):
            if op != L:
                continue
            after = next(o for o in ops[i:] if o not in (L, P, NOOP))
            self.assertNotEqual(PUSHA, after)


# The end.
