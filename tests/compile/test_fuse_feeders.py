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

from koskari.compile.processor import ByteCodeProcessor, make_cmpsrc
from koskari.lib import Compare, NamespaceSpec, OpCode

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
        instr = [(CCV, (src, "a", "b", "fail"))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.CHECK_COMPARE_FAST, (src, 0, 1, "fail")),
        ])

    async def test_const_slot_passes_through(self):
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, True)
        instr = [(CCV, (src, "a", 100, "fail"))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.CHECK_COMPARE_FAST, (src, 0, 100, "fail")),
        ])

    async def test_ineligible_name_stays_var(self):
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, False)
        instr = [(CCV, (src, "a", "elsewhere", "fail"))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (CCV, (src, "a", "elsewhere", "fail")),
        ])

    async def test_const_string_never_mistaken_for_name(self):
        # a constant that happens to spell a local name must stay a
        # constant: the source bit, not the value, decides
        nspec = NamespaceSpec(None, ("a",), ())
        src = make_cmpsrc(LT, False, True)
        instr = [(CCV, (src, "a", "a", "fail"))]
        result = convert_fast(nspec, instr)
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
CI = OpCode.CONST_ITEM
VIT = OpCode.VAR_ITEM
VCI = OpCode.VAR_CONST_ITEM


class TestFuseItemFeeders(unittest.TestCase):
    """
    VAR obj, VAR key, ITEM fuse into VAR_ITEM; VAR obj, CONST_ITEM key
    into VAR_CONST_ITEM. A constant object, a computed object or key,
    and the store rows stay
    """

    def test_var_var_item_fuses(self):
        out = fuse_ops([(V, "a"), (V, "i"), (IT, 0)])
        self.assertEqual(out, [(VIT, ("a", "i", 0, 0))])

    def test_var_const_item_fuses(self):
        out = fuse_ops([(V, "a"), (CI, "k")])
        self.assertEqual(out, [(VCI, ("a", "k", 0, 0))])

    def test_const_object_stays(self):
        out = fuse_ops([(C, (1, 2)), (V, "i"), (IT, 0)])
        self.assertEqual(out, [(C, (1, 2)), (V, "i"), (IT, 0)])
        out = fuse_ops([(C, "abc"), (CI, 1)])
        self.assertEqual(out, [(C, "abc"), (CI, 1)])

    def test_computed_key_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "a"), (V, "f"), call, (IT, 0)])
        self.assertEqual(out, [(V, "a"), (V, "f"), call, (IT, 0)])

    def test_computed_object_stays(self):
        call = (OpCode.CALL, 0)
        out = fuse_ops([(V, "f"), call, (V, "i"), (IT, 0)])
        self.assertEqual(out, [(V, "f"), call, (V, "i"), (IT, 0)])
        out = fuse_ops([(V, "f"), call, (CI, "k")])
        self.assertEqual(out, [(V, "f"), call, (CI, "k")])

    def test_var_var_item_position_transparent(self):
        out = fuse_ops([(V, "a"), (P, 3), (V, "i"), (P, 7), (IT, 0)])
        self.assertEqual(out, [(VIT, ("a", "i", 0, 0)), (P, 7)])

    def test_store_rows_stay(self):
        out = fuse_ops([(V, "a"), (V, "i"), (V, "v"), (OpCode.ITEM_STORE, 0)])
        self.assertEqual(
            out, [(V, "a"), (V, "i"), (V, "v"), (OpCode.ITEM_STORE, 0)])
        out = fuse_ops([(V, "a"), (V, "v"), (OpCode.CONST_ITEM_STORE, "k")])
        self.assertEqual(
            out, [(V, "a"), (V, "v"), (OpCode.CONST_ITEM_STORE, "k")])

    def test_fused_rows_pack_nothing(self):
        # the feeders the rule consumed never reach the pack phase
        nspec = NamespaceSpec(None, ("a", "i"), ())
        proc = ByteCodeProcessor(make_fragment([
            (V, "a"), (V, "i"), (IT, 0), (OpCode.RETURN, 1)]))
        proc.fuse_feeders()
        proc.convert_fast(nspec)
        proc.pack_runs()
        self.assertEqual(proc.instructions, [
            (OpCode.FAST_ITEM, (0, 1, 0, 0)), (OpCode.RETURN, 1)])


class TestItemLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers the VAR item rows to their FAST twins by the
    name-derived map and the schema: every VAR field must have a slot
    """

    async def test_var_item_lowers_when_both_local(self):
        nspec = NamespaceSpec(None, ("a", "i"), ())
        result = convert_fast(nspec, [(VIT, ("a", "i", 0, 0))])
        self.assertEqual(result, [(OpCode.FAST_ITEM, (0, 1, 0, 0))])

    async def test_var_item_stays_when_key_not_local(self):
        nspec = NamespaceSpec(None, ("a",), ())
        result = convert_fast(nspec, [(VIT, ("a", "i", 0, 0))])
        self.assertEqual(result, [(VIT, ("a", "i", 0, 0))])

    async def test_var_const_item_lowers_when_local(self):
        nspec = NamespaceSpec(None, ("a",), ())
        result = convert_fast(nspec, [(VCI, ("a", "k", 0, 0))])
        self.assertEqual(result, [(OpCode.FAST_CONST_ITEM, (0, "k", 0, 0))])

    async def test_store_rows_lower(self):
        nspec = NamespaceSpec(None, ("a", "i"), ())
        result = convert_fast(nspec, [
            (OpCode.VAR_ITEM_STORE, ("a", "i", 0, 0)),
            (OpCode.VAR_CONST_ITEM_STORE, ("a", "k", 0, 0)),
        ])
        self.assertEqual(result, [
            (OpCode.FAST_ITEM_STORE, (0, 1, 0, 0)),
            (OpCode.FAST_CONST_ITEM_STORE, (0, "k", 0, 0)),
        ])

    async def test_store_rows_stay_when_not_local(self):
        nspec = NamespaceSpec(None, ("i",), ())
        result = convert_fast(nspec, [
            (OpCode.VAR_ITEM_STORE, ("a", "i", 0, 0)),
            (OpCode.VAR_CONST_ITEM_STORE, ("a", "k", 0, 0)),
        ])
        self.assertEqual(result, [
            (OpCode.VAR_ITEM_STORE, ("a", "i", 0, 0)),
            (OpCode.VAR_CONST_ITEM_STORE, ("a", "k", 0, 0)),
        ])


# The end.
