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
Unit tests for ByteCodeProcessor.fuse_feeders (CHECK_COMPARE feeder
absorption into CHECK_COMPARE_VAR) and its convert_fast lowering to
CHECK_COMPARE_FAST

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import (
    ITEM_VAR_TO_FAST, VAR_TO_FAST, ByteCodeProcessor, make_cmpsrc)
from koskari.lib import Compare, NamespaceSpec, OpCode, Operand

from tests.lib.opcode import opcount

from . import make_fragment


pytestmark = pytest.mark.phase_compile


LT = Compare.LT
IS = Compare.IS

V = OpCode.VAR
C = OpCode.CONST
CC = OpCode.CHECK_COMPARE
CCV = OpCode.CHECK_COMPARE_VAR
OC = OpCode.OBJECT_COMPARE
OCV = OpCode.OBJECT_COMPARE_VAR


def fuse_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.fuse_feeders()
    return proc.instructions


def process_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    return proc.process()


def convert_fast(base_nspec, instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.convert_fast(base_nspec)
    return proc.instructions


def fuse_and_lower(base_nspec, instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.fuse_feeders()
    proc.convert_fast(base_nspec)
    return proc.instructions


class TestFuseConstOperand(unittest.TestCase):
    """
    A binary OBJECT_OPERAND whose right operand is a constant takes the
    row carrying it, naming the left operand too when it is a variable
    """

    def test_var_const_operand_fuses(self):
        out = fuse_ops([(V, "a"), (C, 3), (OpCode.OBJECT_OPERAND, opcount(Operand.ADD, 2))])
        self.assertEqual(out, [
            (OpCode.VAR_OPERAND_CONST, (int(Operand.ADD), "a", 3, None))])

    def test_stack_const_operand_fuses(self):
        out = fuse_ops([(OpCode.CALL, 0), (C, 3),
                        (OpCode.OBJECT_OPERAND, opcount(Operand.MULTIPLY, 2))])
        self.assertEqual(out, [
            (OpCode.CALL, 0),
            (OpCode.OBJECT_OPERAND_CONST, (int(Operand.MULTIPLY), 3, None, None))])

    def test_three_operand_site_stays(self):
        instr = [(V, "a"), (C, 3), (OpCode.OBJECT_OPERAND, opcount(Operand.ADD, 3))]
        self.assertEqual(fuse_ops(instr), instr)

    def test_two_var_operand_fuses(self):
        out = fuse_ops([(V, "a"), (V, "b"), (OpCode.OBJECT_OPERAND, opcount(Operand.ADD, 2))])
        self.assertEqual(out, [
            (OpCode.VAR_OPERAND_VAR, (int(Operand.ADD), "a", "b", None))])

    def test_stack_var_operand_fuses(self):
        out = fuse_ops([(OpCode.CALL, 0), (V, "b"),
                        (OpCode.OBJECT_OPERAND, opcount(Operand.MULTIPLY, 2))])
        self.assertEqual(out, [
            (OpCode.CALL, 0),
            (OpCode.OBJECT_OPERAND_VAR, (int(Operand.MULTIPLY), "b", None, None))])

    def test_const_var_operand_keeps_the_constant_load(self):
        # a constant left operand has no row of its own; the constant
        # is loaded and the right name rides the row
        out = fuse_ops([(C, 3), (V, "a"), (OpCode.OBJECT_OPERAND, opcount(Operand.SUBTRACT, 2))])
        self.assertEqual(out, [
            (C, 3),
            (OpCode.OBJECT_OPERAND_VAR, (int(Operand.SUBTRACT), "a", None, None))])

    def test_three_var_operand_site_stays(self):
        instr = [(V, "a"), (V, "b"), (V, "c"),
                 (OpCode.OBJECT_OPERAND, opcount(Operand.ADD, 3))]
        self.assertEqual(fuse_ops(instr), instr)

    def test_var_operand_const_lowers_to_fast(self):
        nspec = NamespaceSpec("fn", ("a",), ())
        out = convert_fast(nspec, [
            (OpCode.VAR_OPERAND_CONST, (int(Operand.ADD), "a", 3, None))])
        self.assertEqual(out, [
            (OpCode.FAST_OPERAND_CONST, (int(Operand.ADD), 0, 3, None))])
        out = convert_fast(nspec, [
            (OpCode.VAR_OPERAND_CONST, (int(Operand.ADD), "b", 3, None))])
        self.assertEqual(out, [
            (OpCode.VAR_OPERAND_CONST, (int(Operand.ADD), "b", 3, None))])

    def test_var_operand_var_lowers_to_fast(self):
        nspec = NamespaceSpec("fn", ("a", "b"), ())
        out = convert_fast(nspec, [
            (OpCode.VAR_OPERAND_VAR, (int(Operand.ADD), "a", "b", None)),
            (OpCode.OBJECT_OPERAND_VAR, (int(Operand.ADD), "b", None, None))])
        self.assertEqual(out, [
            (OpCode.FAST_OPERAND_FAST, (int(Operand.ADD), 0, 1, None)),
            (OpCode.OBJECT_OPERAND_FAST, (int(Operand.ADD), 1, None, None))])
        out = convert_fast(nspec, [
            (OpCode.VAR_OPERAND_VAR, (int(Operand.ADD), "a", "c", None))])
        self.assertEqual(out, [
            (OpCode.VAR_OPERAND_VAR, (int(Operand.ADD), "a", "c", None))])


class TestFuseControlFeeders(unittest.TestCase):

    def test_const_return_fuses(self):
        out = fuse_ops([(C, 42), (OpCode.RETURN, 1)])
        self.assertEqual(out, [(OpCode.RETURN_CONST, 42)])

    def test_var_return_fuses(self):
        out = fuse_ops([(V, "a"), (OpCode.RETURN, 1)])
        self.assertEqual(out, [(OpCode.RETURN_VAR, "a")])

    def test_const_yield_fuses(self):
        out = fuse_ops([(C, 42), (OpCode.YIELD, 1), (OpCode.RETURN, 0)])
        self.assertEqual(
            out, [(OpCode.YIELD_CONST, 42), (OpCode.RETURN, 0)])

    def test_var_yield_fuses(self):
        out = fuse_ops([(V, "a"), (OpCode.YIELD, 1), (OpCode.RETURN, 0)])
        self.assertEqual(
            out, [(OpCode.YIELD_VAR, "a"), (OpCode.RETURN, 0)])

    def test_tuple_return_stays(self):
        instr = [(V, "a"), (V, "b"), (OpCode.RETURN, 2)]
        self.assertEqual(fuse_ops(instr), instr)

    def test_feeder_across_label_stays(self):
        instr = [(C, 42), (OpCode.LABEL, "join"), (OpCode.RETURN, 1)]
        self.assertEqual(fuse_ops(instr), instr)
        instr = [(V, "a"), (OpCode.LABEL, "join"), (OpCode.YIELD, 1),
                 (OpCode.RETURN, 0)]
        self.assertEqual(fuse_ops(instr), instr)

    def test_feeder_position_transparent(self):
        pos = (OpCode.POSITION, (1, 0, None))
        out = fuse_ops([(C, 42), pos, (OpCode.RETURN, 1)])
        self.assertEqual(out, [(OpCode.RETURN_CONST, 42), pos])
        out = fuse_ops([(V, "a"), pos, (OpCode.YIELD, 1), (OpCode.RETURN, 0)])
        self.assertEqual(
            out, [(OpCode.YIELD_VAR, "a"), pos, (OpCode.RETURN, 0)])

    def test_return_var_lowers_to_fast(self):
        nspec = NamespaceSpec("fn", ("a",), ())
        out = convert_fast(nspec, [(OpCode.RETURN_VAR, "a")])
        self.assertEqual(out, [(OpCode.RETURN_FAST, 0)])
        out = convert_fast(nspec, [(OpCode.YIELD_VAR, "a")])
        self.assertEqual(out, [(OpCode.YIELD_FAST, 0)])

    def test_return_var_stays_without_slot(self):
        nspec = NamespaceSpec("fn", ("a",), ())
        out = convert_fast(nspec, [(OpCode.RETURN_VAR, "b")])
        self.assertEqual(out, [(OpCode.RETURN_VAR, "b")])


class TestFuseCheckFeeders(unittest.TestCase):

    def test_var_var_fuses(self):
        out = fuse_ops([
            (V, "a"), (V, "b"),
            (CC, (int(LT), 2, "fail", None)),
        ])
        self.assertEqual(out, [
            (CCV, (make_cmpsrc(LT, False, False), "a", "b", "fail")),
        ])

    def test_var_const_fuses(self):
        out = fuse_ops([
            (V, "a"), (C, 100),
            (CC, (int(LT), 2, "fail", None)),
        ])
        self.assertEqual(out, [
            (CCV, (make_cmpsrc(LT, False, True), "a", 100, "fail")),
        ])

    def test_const_var_fuses(self):
        out = fuse_ops([
            (C, 0), (V, "a"),
            (CC, (int(LT), 2, "fail", None)),
        ])
        self.assertEqual(out, [
            (CCV, (make_cmpsrc(LT, True, False), 0, "a", "fail")),
        ])

    def test_const_const_stays_generic(self):
        instr = [
            (C, 1), (C, 2),
            (CC, (int(LT), 2, "fail", None)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_chain_count_stays_generic(self):
        instr = [
            (V, "a"), (V, "b"), (V, "c"),
            (CC, (int(LT), 3, "fail", None)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_extended_selector_fuses(self):
        out = fuse_ops([
            (V, "a"), (C, None),
            (CC, (int(IS), 2, "fail", None)),
        ])
        self.assertEqual(out, [
            (CCV, (make_cmpsrc(IS, False, True), "a", None, "fail")),
        ])

    def test_non_feeder_prior_stays_generic(self):
        instr = [
            (OpCode.FAST_ATTR, ("a", "x", None, None)), (V, "b"),
            (CC, (int(LT), 2, "fail", None)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_position_absorbed_and_trailing_reemitted(self):
        pos1 = (OpCode.POSITION, (1, 0, None))
        pos2 = (OpCode.POSITION, (1, 5, None))
        out = fuse_ops([
            pos1, (V, "a"), pos2, (V, "b"),
            (CC, (int(LT), 2, "fail", None)),
        ])
        self.assertEqual(out, [
            pos1,
            (CCV, (make_cmpsrc(LT, False, False), "a", "b", "fail")),
            pos2,
        ])

    def test_process_runs_the_fusion_before_packing(self):
        # a loop-condition shape: the feeders fuse into the check
        # instead of packing into VAR2
        out = process_ops([
            (V, "a"), (V, "b"),
            (OpCode.OBJECT_COMPARE, (int(LT), 2)),
            (OpCode.JUMP_FALSE, "fail"),
            (C, 1),
            (OpCode.LABEL, "fail"),
        ])
        self.assertEqual(out[0], (
            CCV, (make_cmpsrc(LT, False, False), "a", "b", "fail")))


class TestFuseTernaryBounds(unittest.TestCase):
    """
    The CONST / VAR / CONST bounds shape collapses into the
    bool-producing OBJECT_COMPARE_VAR; in branch position the check
    splits back into the bool row plus JUMP_FALSE
    """

    def test_value_position_fuses(self):
        out = fuse_ops([
            (C, 0), (V, "x"), (C, 100),
            (OC, (int(LT), 3)),
        ])
        self.assertEqual(out, [
            (OCV, (int(LT), 0, "x", 100)),
        ])

    def test_branch_position_splits(self):
        out = fuse_ops([
            (C, 0), (V, "x"), (C, 100),
            (CC, (int(LT), 3, "fail", None)),
        ])
        self.assertEqual(out, [
            (OCV, (int(LT), 0, "x", 100)),
            (OpCode.JUMP_FALSE, "fail"),
        ])

    def test_wrong_shape_stays_generic(self):
        instr = [
            (V, "x"), (C, 0), (C, 100),
            (OC, (int(LT), 3)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_all_var_stays_generic(self):
        instr = [
            (V, "a"), (V, "b"), (V, "c"),
            (OC, (int(LT), 3)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_binary_object_compare_untouched(self):
        instr = [
            (V, "a"), (C, 1),
            (OC, (int(LT), 2)),
        ]
        self.assertEqual(fuse_ops(instr), instr)

    def test_position_tolerant(self):
        pos = (OpCode.POSITION, (2, 4, None))
        out = fuse_ops([
            (C, 0), pos, (V, "x"), (C, 100),
            (OC, (int(LT), 3)),
        ])
        self.assertEqual(out, [
            (OCV, (int(LT), 0, "x", 100)),
            pos,
        ])


class TestTernaryLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers OBJECT_COMPARE_VAR to OBJECT_COMPARE_FAST
    through the generic schema-driven path
    """

    async def test_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("x",), ())
        instr = [(OCV, (int(LT), 0, "x", 100))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.OBJECT_COMPARE_FAST, (int(LT), 0, 0, 100)),
        ])

    async def test_stays_var_when_not_local(self):
        nspec = NamespaceSpec(None, ("y",), ())
        instr = [(OCV, (int(LT), 0, "x", 100))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OCV, (int(LT), 0, "x", 100)),
        ])


class TestFusedCheckLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers CHECK_COMPARE_VAR to CHECK_COMPARE_FAST via the
    CMPSRC-aware path, with the all-or-nothing slot rule
    """

    async def test_var_var_lowers(self):
        nspec = NamespaceSpec(None, ("a", "b"), ())
        src = make_cmpsrc(LT, False, False)
        instr = [
            (V, "a"),
            (V, "b"),
            (CC, (int(LT), 2, "fail", None)),
        ]
        result = fuse_and_lower(nspec, instr)
        self.assertEqual(result, [
            (OpCode.CHECK_COMPARE_FAST, (src, 0, 1, "fail")),
        ])

    async def test_const_slot_passes_through(self):
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, True)
        instr = [
            (V, "a"),
            (C, 100),
            (CC, (int(LT), 2, "fail", None)),
        ]
        result = fuse_and_lower(nspec, instr)
        self.assertEqual(result, [
            (OpCode.CHECK_COMPARE_FAST, (src, 0, 100, "fail")),
        ])

    async def test_ineligible_name_stays_var(self):
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, False)
        instr = [
            (V, "a"),
            (V, "elsewhere"),
            (CC, (int(LT), 2, "fail", None)),
        ]
        result = fuse_and_lower(nspec, instr)
        self.assertEqual(result, [
            (CCV, (src, "a", "elsewhere", "fail")),
        ])

    async def test_const_string_never_mistaken_for_name(self):
        # a constant equal to a local name must stay a constant: the
        # source bit, not the value, decides
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, True)
        instr = [
            (V, "a"),
            (C, "a"),
            (CC, (int(LT), 2, "fail", None)),
        ]
        result = fuse_and_lower(nspec, instr)
        self.assertEqual(result, [
            (OpCode.CHECK_COMPARE_FAST, (src, 0, "a", "fail")),
        ])


OI = OpCode.OBJECT_ITER
ON = OpCode.OBJECT_NEXT
VI = OpCode.VAR_ITER
VN = OpCode.VAR_NEXT
P = OpCode.POSITION


class TestFuseIterNext(unittest.TestCase):
    """
    fuse_feeders absorbs bare VAR feeders into VAR_ITER and
    VAR_NEXT
    """

    def test_var_iter_fuses(self):
        out = fuse_ops([(V, "a"), (OI, 0)])
        self.assertEqual(out, [(VI, "a")])

    def test_const_iter_stays(self):
        out = fuse_ops([(C, (1, 2)), (OI, 0)])
        self.assertEqual(out, [(C, (1, 2)), (OI, 0)])

    def test_computed_iter_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "f"), call, (OI, 0)])
        self.assertEqual(out, [(V, "f"), call, (OI, 0)])

    def test_var_iter_position_transparent(self):
        out = fuse_ops([(V, "a"), (P, 7), (OI, 0)])
        self.assertEqual(out, [(VI, "a"), (P, 7)])

    def test_var_next_1_fuses(self):
        out = fuse_ops([(V, "it"), (ON, 1)])
        self.assertEqual(out, [(VN, ("it", 1, None, None))])

    def test_const_next_1_stays(self):
        out = fuse_ops([(C, "x"), (ON, 1)])
        self.assertEqual(out, [(C, "x"), (ON, 1)])

    def test_var_next_2_const_default_fuses(self):
        out = fuse_ops([(V, "it"), (C, 99), (ON, 2)])
        self.assertEqual(out, [(C, 99), (VN, ("it", 2, None, None))])

    def test_var_next_2_var_default_fuses(self):
        out = fuse_ops([(V, "it"), (V, "d"), (ON, 2)])
        self.assertEqual(out, [(V, "d"), (VN, ("it", 2, None, None))])

    def test_var_next_2_computed_default_stays(self):
        # the iterator load cannot move past a non-bare default row
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "it"), (V, "f"), call, (ON, 2)])
        self.assertEqual(out, [(V, "it"), (V, "f"), call, (ON, 2)])

    def test_var_next_2_const_iterator_stays(self):
        # only a VAR iterator feeder fuses
        out = fuse_ops([(C, "x"), (C, 99), (ON, 2)])
        self.assertEqual(out, [(C, "x"), (C, 99), (ON, 2)])


class TestIterNextLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers VAR_ITER / VAR_NEXT to their FAST twins: the
    simple path for VAR_ITER, the generic schema-driven path for the
    EXTRA-styled VAR_NEXT
    """

    async def test_var_iter_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("x",), ())
        result = convert_fast(
            nspec, [(VI, "x")])
        self.assertEqual(result, [(OpCode.FAST_ITER, 0)])

    async def test_var_iter_stays_when_not_local(self):
        nspec = NamespaceSpec(None, ("y",), ())
        result = convert_fast(
            nspec, [(VI, "x")])
        self.assertEqual(result, [(VI, "x")])

    async def test_var_next_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("it",), ())
        result = convert_fast(
            nspec, [(VN, ("it", 2, None, None))])
        self.assertEqual(result, [
            (OpCode.FAST_NEXT, (0, 2, None, None)),
        ])

    async def test_var_next_stays_when_not_local(self):
        nspec = NamespaceSpec(None, ("y",), ())
        result = convert_fast(
            nspec, [(VN, ("it", 1, None, None))])
        self.assertEqual(result, [(VN, ("it", 1, None, None))])


OF = OpCode.OBJECT_FORMAT
OFC = OpCode.OBJECT_FORMAT_CONST
VF = OpCode.VAR_FORMAT
VFC = OpCode.VAR_FORMAT_CONST


class TestFuseFormat(unittest.TestCase):
    """
    fuse_feeders absorbs bare VAR feeders into VAR_FORMAT and
    VAR_FORMAT_CONST
    """

    def test_var_format_fuses(self):
        out = fuse_ops([(V, "a"), (OF, 1)])
        self.assertEqual(out, [(VF, "a")])

    def test_const_format_stays(self):
        out = fuse_ops([(C, 99), (OF, 1)])
        self.assertEqual(out, [(C, 99), (OF, 1)])

    def test_computed_format_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "f"), call, (OF, 1)])
        self.assertEqual(out, [(V, "f"), call, (OF, 1)])

    def test_dynamic_rules_stays(self):
        # 2-arg OBJECT_FORMAT with dynamic rules never fuses: there is
        # no fused form carrying a stacked rules operand
        out = fuse_ops([(V, "a"), (V, "r"), (OF, 2)])
        self.assertEqual(out, [(V, "a"), (V, "r"), (OF, 2)])

    def test_const_rules_demotes_and_fuses(self):
        # the full lowering ladder: the constant rules feeder demotes
        # OBJECT_FORMAT to OBJECT_FORMAT_CONST, whose retry fuses the
        # VAR object feeder
        out = fuse_ops([(V, "a"), (C, "04d"), (OF, 2)])
        self.assertEqual(out, [(VFC, ("a", "04d", None, None))])

    def test_const_rules_computed_object_demotes_only(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "f"), call, (C, "04d"), (OF, 2)])
        self.assertEqual(out, [(V, "f"), call, (OFC, "04d")])

    def test_const_rules_position_transparent_demotion(self):
        pos = (P, 9)
        out = fuse_ops([(V, "a"), (C, "04d"), pos, (OF, 2)])
        self.assertEqual(out, [(VFC, ("a", "04d", None, None)), pos])

    def test_var_format_position_transparent(self):
        out = fuse_ops([(V, "a"), (P, 7), (OF, 1)])
        self.assertEqual(out, [(VF, "a"), (P, 7)])

    def test_var_format_const_fuses(self):
        out = fuse_ops([(V, "a"), (OFC, "04d")])
        self.assertEqual(out, [(VFC, ("a", "04d", None, None))])

    def test_const_format_const_stays(self):
        out = fuse_ops([(C, 7), (OFC, "04d")])
        self.assertEqual(out, [(C, 7), (OFC, "04d")])

    def test_process_runs_the_fusion_before_packing(self):
        # adjacent interpolations: each VAR fuses into its format row
        # instead of packing into VAR2
        out = process_ops([
            (V, "a"), (OF, 1),
            (V, "b"), (OFC, ".2f"),
        ])
        self.assertEqual(out[:2], [
            (VF, "a"),
            (VFC, ("b", ".2f", None, None)),
        ])


class TestFormatLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers VAR_FORMAT / VAR_FORMAT_CONST to their FAST
    twins: the simple path for VAR_FORMAT, the generic schema-driven
    path for the EXTRA-styled VAR_FORMAT_CONST
    """

    async def test_var_format_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("x",), ())
        result = convert_fast(
            nspec, [(VF, "x")])
        self.assertEqual(result, [(OpCode.FAST_FORMAT, 0)])

    async def test_var_format_stays_when_not_local(self):
        nspec = NamespaceSpec(None, ("y",), ())
        result = convert_fast(
            nspec, [(VF, "x")])
        self.assertEqual(result, [(VF, "x")])

    async def test_var_format_const_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("x",), ())
        result = convert_fast(
            nspec, [(VFC, ("x", "04d", None, None))])
        self.assertEqual(result, [
            (OpCode.FAST_FORMAT_CONST, (0, "04d", None, None)),
        ])

    async def test_var_format_const_stays_when_not_local(self):
        nspec = NamespaceSpec(None, ("y",), ())
        result = convert_fast(
            nspec, [(VFC, ("x", "04d", None, None))])
        self.assertEqual(result, [(VFC, ("x", "04d", None, None))])


IT = OpCode.ITEM
ADD1 = (Operand.ADD, 1)
ADD2 = (Operand.ADD, 2)
IADD = int(Operand.ADD)


class TestFuseItemAccess(unittest.TestCase):
    """
    A load, CLEAR, TAKE, or REF row folds the feeders directly ahead
    of it: a VAR object and a feeder key name both, a feeder key alone
    names the key, a CONST object never folds, and a computed operand
    between a feeder and the row leaves the feeder alone
    """

    VERBS = ("ITEM", "ITEM_CLEAR", "ITEM_TAKE", "ITEM_REF")

    def row(self, verb, obj, key):
        parts = ([obj] if obj else []) + ["ITEM"] + ([key] if key else [])
        if verb != "ITEM":
            parts.append(verb[len("ITEM_"):])
        return OpCode["_".join(parts)]

    def test_var_obj_var_key(self):
        for verb in self.VERBS:
            out = fuse_ops([(V, "a"), (V, "i"), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(self.row(verb, "VAR", "VAR"), ("a", "i", 0, 0))])

    def test_var_obj_const_key(self):
        for verb in self.VERBS:
            out = fuse_ops([(V, "a"), (C, "k"), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(self.row(verb, "VAR", "CONST"), ("a", "k", 0, 0))])

    def test_const_obj_folds_key_only(self):
        for verb in self.VERBS:
            out = fuse_ops([(C, (1, 2)), (V, "i"), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(C, (1, 2)), (self.row(verb, None, "VAR"), "i")])
            out = fuse_ops([(C, "abc"), (C, 1), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(C, "abc"), (self.row(verb, None, "CONST"), 1)])

    def test_computed_obj_folds_key_only(self):
        call = (OpCode.CALL, 0)
        for verb in self.VERBS:
            out = fuse_ops([(V, "f"), call, (V, "i"), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(V, "f"), call, (self.row(verb, None, "VAR"), "i")])
            out = fuse_ops([(V, "f"), call, (C, "k"), (OpCode[verb], 0)])
            self.assertEqual(
                out, [(V, "f"), call, (self.row(verb, None, "CONST"), "k")])

    def test_computed_key_stays(self):
        call = (OpCode.CALL, 0)
        for verb in self.VERBS:
            out = fuse_ops([(V, "a"), (V, "f"), call, (OpCode[verb], 0)])
            self.assertEqual(out, [(V, "a"), (V, "f"), call, (OpCode[verb], 0)])

    def test_position_transparent(self):
        out = fuse_ops([(V, "a"), (P, 3), (V, "i"), (P, 7), (IT, 0)])
        self.assertEqual(out, [(OpCode.VAR_ITEM_VAR, ("a", "i", 0, 0)), (P, 7)])

    def test_fused_rows_pack_nothing(self):
        # the feeders the rule consumed never reach the pack phase
        nspec = NamespaceSpec(None, ("a", "i"), ())
        proc = ByteCodeProcessor(make_fragment([
            (V, "a"), (V, "i"), (IT, 0), (OpCode.RETURN, 1)]))
        proc.fuse_feeders()
        proc.convert_fast(nspec)
        proc.pack_runs()
        self.assertEqual(proc.instructions, [
            (OpCode.FAST_ITEM_FAST, (0, 1, 0, 0)), (OpCode.RETURN, 1)])


class TestFuseItemStore(unittest.TestCase):
    """
    ITEM_STORE folds its trailing feeders: value, then key, then a
    VAR object; the rows the special shapes with the value on the
    stack fold a value feeder alone
    """

    ST = OpCode.ITEM_STORE

    def test_three_feeders(self):
        cases = {
            ((V, "a"), (V, "i"), (V, "v")): (
                OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0)),
            ((V, "a"), (V, "i"), (C, 1)): (
                OpCode.VAR_ITEM_VAR_STORE_CONST, ("a", "i", 1, 0)),
            ((V, "a"), (C, "k"), (V, "v")): (
                OpCode.VAR_ITEM_CONST_STORE_VAR, ("a", "k", "v", 0)),
            ((V, "a"), (C, "k"), (C, 1)): (
                OpCode.VAR_ITEM_CONST_STORE_CONST, ("a", "k", 1, 0)),
        }
        for feeders, fused in cases.items():
            out = fuse_ops(list(feeders) + [(self.ST, 0)])
            self.assertEqual(out, [fused])

    def test_two_feeders_behind_a_computed_object(self):
        call = (OpCode.CALL, 0)
        cases = {
            ((V, "i"), (V, "v")): (OpCode.ITEM_VAR_STORE_VAR, ("i", "v", 0, 0)),
            ((V, "i"), (C, 1)): (OpCode.ITEM_VAR_STORE_CONST, ("i", 1, 0, 0)),
            ((C, "k"), (V, "v")): (OpCode.ITEM_CONST_STORE_VAR, ("k", "v", 0, 0)),
            ((C, "k"), (C, 1)): (OpCode.ITEM_CONST_STORE_CONST, ("k", 1, 0, 0)),
        }
        for feeders, fused in cases.items():
            out = fuse_ops([(V, "f"), call] + list(feeders) + [(self.ST, 0)])
            self.assertEqual(out, [(V, "f"), call, fused])

    def test_const_object_folds_key_and_value(self):
        out = fuse_ops([(C, (1,)), (V, "i"), (V, "v"), (self.ST, 0)])
        self.assertEqual(
            out, [(C, (1,)), (OpCode.ITEM_VAR_STORE_VAR, ("i", "v", 0, 0))])

    def test_one_feeder_behind_a_computed_key(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "a"), (V, "f"), call, (V, "v"), (self.ST, 0)])
        self.assertEqual(
            out, [(V, "a"), (V, "f"), call, (OpCode.ITEM_STORE_VAR, "v")])
        out = fuse_ops([(V, "a"), (V, "f"), call, (C, 1), (self.ST, 0)])
        self.assertEqual(
            out, [(V, "a"), (V, "f"), call, (OpCode.ITEM_STORE_CONST, 1)])

    def test_computed_value_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "a"), (V, "i"), (V, "f"), call, (self.ST, 0)])
        self.assertEqual(out, [(V, "a"), (V, "i"), (V, "f"), call, (self.ST, 0)])

    def test_shaped_rows_fold_a_value_feeder(self):
        cases = {
            (OpCode.VAR_ITEM_STORE, "a"): (
                OpCode.VAR_ITEM_STORE_VAR, ("a", "v", 0, 0),
                OpCode.VAR_ITEM_STORE_CONST, ("a", 1, 0, 0)),
            (OpCode.VAR_ITEM_VAR_STORE, ("a", "i", 0, 0)): (
                OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0),
                OpCode.VAR_ITEM_VAR_STORE_CONST, ("a", "i", 1, 0)),
            (OpCode.VAR_ITEM_CONST_STORE, ("a", "k", 0, 0)): (
                OpCode.VAR_ITEM_CONST_STORE_VAR, ("a", "k", "v", 0),
                OpCode.VAR_ITEM_CONST_STORE_CONST, ("a", "k", 1, 0)),
            (OpCode.ITEM_VAR_STORE, "i"): (
                OpCode.ITEM_VAR_STORE_VAR, ("i", "v", 0, 0),
                OpCode.ITEM_VAR_STORE_CONST, ("i", 1, 0, 0)),
            (OpCode.ITEM_CONST_STORE, "k"): (
                OpCode.ITEM_CONST_STORE_VAR, ("k", "v", 0, 0),
                OpCode.ITEM_CONST_STORE_CONST, ("k", 1, 0, 0)),
        }
        for row, (vop, varg, cop, carg) in cases.items():
            self.assertEqual(fuse_ops([(V, "v"), row]), [(vop, varg)])
            self.assertEqual(fuse_ops([(C, 1), row]), [(cop, carg)])

    def test_shaped_row_keeps_a_computed_value(self):
        call = (OpCode.CALL, 0)
        row = (OpCode.VAR_ITEM_VAR_STORE, ("a", "i", 0, 0))
        out = fuse_ops([(V, "f"), call, row])
        self.assertEqual(out, [(V, "f"), call, row])

    def test_position_transparent(self):
        out = fuse_ops([
            (V, "a"), (P, 1), (V, "i"), (P, 2), (V, "v"), (P, 3), (self.ST, 0)])
        self.assertEqual(
            out, [(OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0)), (P, 3)])

    def test_compound_object_ending_in_a_load_folds_the_load(self):
        # the object's last instruction is the bare load the rule sees
        # as the object feeder; the call ahead of it is left in place
        call = (OpCode.CALL, 0)
        out = fuse_ops([
            (V, "f"), call, (OpCode.POP, 1), (V, "a"), (V, "i"), (V, "v"),
            (self.ST, 0)])
        self.assertEqual(out, [
            (V, "f"), call, (OpCode.POP, 1),
            (OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0))])


class TestFuseItemInplace(unittest.TestCase):
    """
    ITEM_INPLACE folds a VAR object and a feeder key when every value
    is a feeder, re-emitting the values ahead of the row; a single
    CONST value folds into the _CONST twin; the shaped rows fold a
    CONST operand at count one
    """

    IP = OpCode.ITEM_INPLACE

    def test_var_obj_var_key_var_value(self):
        out = fuse_ops([(V, "a"), (V, "i"), (V, "v"), (self.IP, ADD1)])
        self.assertEqual(out, [
            (V, "v"), (OpCode.VAR_ITEM_VAR_INPLACE, (ADD1, "a", "i", 0))])

    def test_var_obj_const_key_var_value(self):
        out = fuse_ops([(V, "a"), (C, "k"), (V, "v"), (self.IP, ADD1)])
        self.assertEqual(out, [
            (V, "v"), (OpCode.VAR_ITEM_CONST_INPLACE, (ADD1, "a", "k", 0))])

    def test_const_value_folds_into_the_const_twin(self):
        out = fuse_ops([(V, "a"), (V, "i"), (C, 1), (self.IP, ADD1)])
        self.assertEqual(
            out, [(OpCode.VAR_ITEM_VAR_INPLACE_CONST, (IADD, "a", "i", 1))])
        out = fuse_ops([(V, "a"), (C, "k"), (C, 1), (self.IP, ADD1)])
        self.assertEqual(
            out, [(OpCode.VAR_ITEM_CONST_INPLACE_CONST, (IADD, "a", "k", 1))])

    def test_computed_object_folds_key_only(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "f"), call, (V, "i"), (C, 1), (self.IP, ADD1)])
        self.assertEqual(out, [
            (V, "f"), call, (OpCode.ITEM_VAR_INPLACE_CONST, (IADD, "i", 1, 0))])
        out = fuse_ops([(V, "f"), call, (C, "k"), (V, "v"), (self.IP, ADD1)])
        self.assertEqual(out, [
            (V, "f"), call, (V, "v"),
            (OpCode.ITEM_CONST_INPLACE, (ADD1, "k", 0, 0))])

    def test_const_object_folds_key_only(self):
        out = fuse_ops([(C, [0]), (C, 0), (C, 1), (self.IP, ADD1)])
        self.assertEqual(
            out, [(C, [0]), (OpCode.ITEM_CONST_INPLACE_CONST, (IADD, 0, 1, 0))])

    def test_two_values_re_emit_behind_the_row(self):
        out = fuse_ops([(V, "a"), (V, "i"), (V, "x"), (C, 2), (self.IP, ADD2)])
        self.assertEqual(out, [
            (V, "x"), (C, 2), (OpCode.VAR_ITEM_VAR_INPLACE, (ADD2, "a", "i", 0))])

    def test_re_emitted_values_pack(self):
        nspec = NamespaceSpec(None, ("a", "i", "x", "y"), ())
        proc = ByteCodeProcessor(make_fragment([
            (V, "a"), (V, "i"), (V, "x"), (V, "y"), (self.IP, ADD2),
            (OpCode.RETURN, 0)]))
        proc.fuse_feeders()
        proc.normalize_opcount()
        proc.convert_fast(nspec)
        proc.pack_runs()
        self.assertEqual(proc.instructions, [
            (OpCode.FAST2, (2, 3, None, None)),
            (OpCode.FAST_ITEM_FAST_INPLACE, (opcount(Operand.ADD, 2), 0, 1, 0)),
            (OpCode.RETURN, 0)])

    def test_position_transparent(self):
        out = fuse_ops([
            (V, "a"), (P, 1), (V, "i"), (P, 2), (V, "v"), (P, 3), (self.IP, ADD1)])
        self.assertEqual(out, [
            (V, "v"), (OpCode.VAR_ITEM_VAR_INPLACE, (ADD1, "a", "i", 0)), (P, 3)])
        out = fuse_ops([
            (V, "a"), (V, "i"), (P, 4), (C, 1), (self.IP, ADD1)])
        self.assertEqual(out, [
            (OpCode.VAR_ITEM_VAR_INPLACE_CONST, (IADD, "a", "i", 1)), (P, 4)])

    def test_computed_value_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "a"), (V, "i"), (V, "f"), call, (self.IP, ADD1)])
        self.assertEqual(
            out, [(V, "a"), (V, "i"), (V, "f"), call, (self.IP, ADD1)])

    def test_computed_key_folds_a_const_operand_only(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "a"), (V, "f"), call, (C, 1), (self.IP, ADD1)])
        self.assertEqual(
            out, [(V, "a"), (V, "f"), call,
                  (OpCode.ITEM_INPLACE_CONST, (IADD, 1, 0, 0))])
        out = fuse_ops([(V, "a"), (V, "f"), call, (V, "v"), (self.IP, ADD1)])
        self.assertEqual(
            out, [(V, "a"), (V, "f"), call, (V, "v"), (self.IP, ADD1)])
        out = fuse_ops([(V, "a"), (V, "f"), call, (C, 1), (C, 2), (self.IP, ADD2)])
        self.assertEqual(
            out, [(V, "a"), (V, "f"), call, (C, 1), (C, 2), (self.IP, ADD2)])

    def test_shaped_rows_fold_a_const_operand_at_count_one(self):
        cases = {
            (OpCode.VAR_ITEM_INPLACE, (ADD1, "a", 0, 0)): (
                OpCode.VAR_ITEM_INPLACE_CONST, (IADD, "a", 1, 0)),
            (OpCode.VAR_ITEM_VAR_INPLACE, (ADD1, "a", "i", 0)): (
                OpCode.VAR_ITEM_VAR_INPLACE_CONST, (IADD, "a", "i", 1)),
            (OpCode.VAR_ITEM_CONST_INPLACE, (ADD1, "a", "k", 0)): (
                OpCode.VAR_ITEM_CONST_INPLACE_CONST, (IADD, "a", "k", 1)),
            (OpCode.ITEM_VAR_INPLACE, (ADD1, "i", 0, 0)): (
                OpCode.ITEM_VAR_INPLACE_CONST, (IADD, "i", 1, 0)),
            (OpCode.ITEM_CONST_INPLACE, (ADD1, "k", 0, 0)): (
                OpCode.ITEM_CONST_INPLACE_CONST, (IADD, "k", 1, 0)),
        }
        for row, fused in cases.items():
            self.assertEqual(fuse_ops([(C, 1), row]), [fused])

    def test_shaped_rows_keep_a_var_operand_and_a_count_above_one(self):
        row = (OpCode.VAR_ITEM_VAR_INPLACE, (ADD1, "a", "i", 0))
        self.assertEqual(fuse_ops([(V, "v"), row]), [(V, "v"), row])
        row = (OpCode.VAR_ITEM_VAR_INPLACE, (ADD2, "a", "i", 0))
        self.assertEqual(
            fuse_ops([(V, "v"), (C, 1), row]), [(V, "v"), (C, 1), row])


class TestItemLowering(unittest.TestCase):
    """
    convert_fast lowers every VAR item row to its FAST twin through
    the written-out map and the schema: every VAR field must have a
    slot, a plain-style row lowers its one field
    """

    def test_map_covers_every_var_row(self):
        var_rows = {
            op for op in OpCode
            if "ITEM" in op.name and "VAR" in op.name.split("_")}
        self.assertEqual(set(ITEM_VAR_TO_FAST), var_rows)
        for var, fast in ITEM_VAR_TO_FAST.items():
            self.assertEqual(fast.name, var.name.replace("VAR", "FAST"))
            self.assertIn(var, VAR_TO_FAST)
            self.assertIs(VAR_TO_FAST[var], fast)

    def test_three_field_row_lowers(self):
        nspec = NamespaceSpec(None, ("a", "i", "v"), ())
        result = convert_fast(
            nspec, [(OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0))])
        self.assertEqual(
            result, [(OpCode.FAST_ITEM_FAST_STORE_FAST, (0, 1, 2, 0))])

    def test_all_or_nothing(self):
        nspec = NamespaceSpec(None, ("a", "i"), ())
        row = (OpCode.VAR_ITEM_VAR_STORE_VAR, ("a", "i", "v", 0))
        self.assertEqual(convert_fast(nspec, [row]), [row])
        row = (OpCode.VAR_ITEM_VAR, ("a", "i", 0, 0))
        self.assertEqual(
            convert_fast(nspec, [row]), [(OpCode.FAST_ITEM_FAST, (0, 1, 0, 0))])
        self.assertEqual(
            convert_fast(NamespaceSpec(None, ("a",), ()), [row]), [row])

    def test_plain_rows_lower(self):
        nspec = NamespaceSpec(None, ("a", "v"), ())
        result = convert_fast(nspec, [
            (OpCode.VAR_ITEM, "a"),
            (OpCode.ITEM_VAR, "v"),
            (OpCode.ITEM_STORE_VAR, "v"),
            (OpCode.VAR_ITEM_STORE, "a"),
            (OpCode.ITEM_VAR_TAKE, "a"),
        ])
        self.assertEqual(result, [
            (OpCode.FAST_ITEM, 0),
            (OpCode.ITEM_FAST, 1),
            (OpCode.ITEM_STORE_FAST, 1),
            (OpCode.FAST_ITEM_STORE, 0),
            (OpCode.ITEM_FAST_TAKE, 0),
        ])

    def test_mixed_rows_lower(self):
        nspec = NamespaceSpec(None, ("a", "v"), ())
        result = convert_fast(nspec, [
            (OpCode.VAR_ITEM_CONST_STORE_VAR, ("a", "k", "v", 0)),
            (OpCode.ITEM_CONST_STORE_VAR, ("k", "v", 0, 0)),
            (OpCode.VAR_ITEM_VAR_INPLACE_CONST, (IADD, "a", "v", 1)),
        ])
        self.assertEqual(result, [
            (OpCode.FAST_ITEM_CONST_STORE_FAST, (0, "k", 1, 0)),
            (OpCode.ITEM_CONST_STORE_FAST, ("k", 1, 0, 0)),
            (OpCode.FAST_ITEM_FAST_INPLACE_CONST, (IADD, 0, 1, 1)),
        ])


# The end.
