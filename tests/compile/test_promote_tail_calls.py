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
Unit tests for ByteCodeProcessor.promote_tail_calls: the extent-stack
legality scan and the clear-route walk to RETURN 1

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import OpCode


pytestmark = pytest.mark.phase_compile


C = OpCode.CONST
V = OpCode.VAR
CALL = OpCode.CALL
TAIL = OpCode.TAIL_CALL
RET = OpCode.RETURN
P = OpCode.POSITION
L = OpCode.LABEL
J = OpCode.JUMP


def promote(instr):
    proc = ByteCodeProcessor(instr)
    proc.promote_tail_calls()
    return proc.instructions


class TestClearRoute(unittest.TestCase):
    """
    Route validity: only a clear path to RETURN 1 promotes
    """

    def test_adjacent_return_promotes(self):
        out = promote([(CALL, 1), (RET, 1)])
        self.assertEqual(out, [(TAIL, 1), (RET, 1)])

    def test_return_zero_stays(self):
        out = promote([(CALL, 1), (RET, 0)])
        self.assertEqual(out, [(CALL, 1), (RET, 0)])

    def test_return_two_stays(self):
        out = promote([(CALL, 1), (RET, 2)])
        self.assertEqual(out, [(CALL, 1), (RET, 2)])

    def test_position_transparent(self):
        out = promote([(CALL, 1), (P, 7), (RET, 1)])
        self.assertEqual(out, [(TAIL, 1), (P, 7), (RET, 1)])

    def test_consumer_row_stays(self):
        out = promote([(CALL, 1), (OpCode.POP, 1), (RET, 1)])
        self.assertEqual(out, [(CALL, 1), (OpCode.POP, 1), (RET, 1)])

    def test_jump_to_return_promotes(self):
        out = promote([
            (CALL, 1), (J, "end"),
            (C, 5), (L, "end"), (RET, 1),
        ])
        self.assertEqual(out, [
            (TAIL, 1), (J, "end"),
            (C, 5), (L, "end"), (RET, 1),
        ])

    def test_jump_chain_promotes(self):
        out = promote([
            (CALL, 1), (J, "a"),
            (L, "a"), (J, "b"),
            (L, "b"), (RET, 1),
        ])
        self.assertEqual(out[0], (TAIL, 1))

    def test_jump_cycle_terminates_unpromoted(self):
        out = promote([
            (CALL, 1), (J, "a"),
            (L, "a"), (J, "a"),
        ])
        self.assertEqual(out[0], (CALL, 1))

    def test_conditional_jump_stays(self):
        out = promote([
            (CALL, 1), (OpCode.JUMP_FALSE, "end"),
            (L, "end"), (RET, 1),
        ])
        self.assertEqual(out[0], (CALL, 1))

    def test_route_over_discardable_pop_promotes(self):
        out = promote([
            (OpCode.PUSH_NS, 0),
            (CALL, 1), (OpCode.POP_NS, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (TAIL, 1))


class TestExtentLegality(unittest.TestCase):
    """
    Any open non-discardable extent denies promotion; discardable
    extents allow it
    """

    def test_inside_try_denied(self):
        out = promote([
            (OpCode.PUSH_TRY, "h"),
            (CALL, 1), (OpCode.POP_TRY, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (CALL, 1))

    def test_inside_with_denied(self):
        out = promote([
            (OpCode.PUSH_WITH, 0),
            (CALL, 1), (OpCode.POP_WITH, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (CALL, 1))

    def test_inside_param_denied(self):
        out = promote([
            (OpCode.PUSH_PARAM, 0),
            (CALL, 1), (OpCode.POP_PARAM, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (CALL, 1))

    def test_inside_ns_allowed(self):
        out = promote([
            (OpCode.PUSH_NS, 0),
            (CALL, 1), (OpCode.POP_NS, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (TAIL, 1))

    def test_inside_loop_allowed(self):
        out = promote([
            (OpCode.PUSH_LOOP, 0),
            (CALL, 1), (OpCode.POP_LOOP, 0), (RET, 1),
        ])
        self.assertEqual(out[1], (TAIL, 1))

    def test_ns_inside_with_denied(self):
        # whole-stack denial: an outer non-discardable extent wins
        out = promote([
            (OpCode.PUSH_WITH, 0), (OpCode.PUSH_NS, 0),
            (CALL, 1), (OpCode.POP_NS, 0),
            (OpCode.POP_WITH, 0), (RET, 1),
        ])
        self.assertEqual(out[2], (CALL, 1))

    def test_after_popped_try_allowed(self):
        out = promote([
            (OpCode.PUSH_TRY, "h"), (OpCode.POP_TRY, 0),
            (CALL, 1), (RET, 1),
        ])
        self.assertEqual(out[2], (TAIL, 1))


class TestPromotionForms(unittest.TestCase):
    """
    The promotable call family and its rewrites
    """

    def test_call_names_promotes(self):
        out = promote([(OpCode.CALL_NAMES, (("k",), 1)), (RET, 1)])
        self.assertEqual(out[0], (OpCode.TAIL_CALL_NAMES, (("k",), 1)))

    def test_call_star_promotes(self):
        out = promote([(OpCode.CALL_STAR, 1), (RET, 1)])
        self.assertEqual(out[0], (OpCode.TAIL_CALL_STAR, 1))

    def test_call_var_stays(self):
        # CALL_VAR carries its callee in the row; TAIL_CALL needs the
        # callable below the arguments, so no local rewrite promotes it
        out = promote([(OpCode.CALL_VAR, ("f", 2, 0, 0)), (RET, 1)])
        self.assertEqual(out, [(OpCode.CALL_VAR, ("f", 2, 0, 0)), (RET, 1)])

    def test_attr_call_stays(self):
        out = promote([
            (OpCode.CALL_FAST_ATTR, (0, "m", 1, 0)), (RET, 1)])
        self.assertEqual(out[0], (OpCode.CALL_FAST_ATTR, (0, "m", 1, 0)))

    def test_yielding_stream_untouched(self):
        instr = [(CALL, 1), (RET, 1), (C, 1), (OpCode.YIELD, 1)]
        self.assertEqual(promote(instr), instr)


# The end.
