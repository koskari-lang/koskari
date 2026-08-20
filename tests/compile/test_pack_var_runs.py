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
Unit tests for the VAR and FAST run packing in ByteCodeProcessor.pack_runs
(the trailing pack phase) and the convert_fast lowering of VAR2/VAR4 to
FAST2/FAST4

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import NamespaceSpec, OpCode

from . import make_fragment


pytestmark = pytest.mark.phase_compile


def pack_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.pack_runs()
    return proc.instructions


def process_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    return proc.process()


def convert_fast(base_nspec, instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.convert_fast(base_nspec)
    return proc.instructions


V = OpCode.VAR


class TestPackVarRuns(unittest.TestCase):

    def test_two_run_packs_var2(self):
        out = pack_ops([(V, "a"), (V, "b")])
        self.assertEqual(out, [
            (OpCode.VAR2, ("a", "b", None, None)),
        ])

    def test_three_run_packs_var2_var(self):
        out = pack_ops([(V, "a"), (V, "b"), (V, "c")])
        self.assertEqual(out, [
            (OpCode.VAR2, ("a", "b", None, None)),
            (V, "c"),
        ])

    def test_four_run_packs_var4(self):
        out = pack_ops([(V, "a"), (V, "b"), (V, "c"), (V, "d")])
        self.assertEqual(out, [
            (OpCode.VAR4, ("a", "b", "c", "d")),
        ])

    def test_five_run_packs_var4_var(self):
        out = pack_ops([(V, n) for n in "abcde"])
        self.assertEqual(out, [
            (OpCode.VAR4, ("a", "b", "c", "d")),
            (V, "e"),
        ])

    def test_eight_run_packs_var4_var4(self):
        out = pack_ops([(V, n) for n in "abcdefgh"])
        self.assertEqual(out, [
            (OpCode.VAR4, ("a", "b", "c", "d")),
            (OpCode.VAR4, ("e", "f", "g", "h")),
        ])

    def test_single_var_unchanged(self):
        out = pack_ops([(V, "a")])
        self.assertEqual(out, [(V, "a")])

    def test_label_breaks_run(self):
        out = pack_ops([
            (V, "a"), (V, "b"),
            (OpCode.LABEL, "L1"),
            (V, "c"), (V, "d"),
        ])
        self.assertEqual(out, [
            (OpCode.VAR2, ("a", "b", None, None)),
            (OpCode.LABEL, "L1"),
            (OpCode.VAR2, ("c", "d", None, None)),
        ])

    def test_other_ops_break_run(self):
        out = pack_ops([
            (V, "a"),
            (OpCode.VAR_ATTR, ("a", "x", None, None)),
            (V, "b"),
        ])
        self.assertEqual(out, [
            (V, "a"),
            (OpCode.VAR_ATTR, ("a", "x", None, None)),
            (V, "b"),
        ])

    def test_position_absorbed_and_trailing_reemitted(self):
        pos1 = (OpCode.POSITION, (1, 0, None))
        pos2 = (OpCode.POSITION, (1, 5, None))
        call = (OpCode.CALL, 2)
        out = pack_ops([pos1, (V, "a"), pos2, (V, "b"), call])
        self.assertEqual(out, [
            pos1,
            (OpCode.VAR2, ("a", "b", None, None)),
            pos2,
            call,
        ])

    def test_position_outside_run_passes_through(self):
        pos = (OpCode.POSITION, (3, 0, None))
        out = pack_ops([pos, (OpCode.CONST, 1)])
        self.assertEqual(out, [pos, (OpCode.CONST, 1)])

    def test_process_runs_the_packer(self):
        out = process_ops([(V, "a"), (V, "b"), (V, "c"), (V, "d")])
        self.assertEqual(out, [
            (OpCode.VAR4, ("a", "b", "c", "d")),
            (OpCode.RETURN, 1),
        ])

    def test_fast_run_packs_fast2_fast4(self):
        out = pack_ops([(OpCode.FAST, i) for i in range(6)])
        self.assertEqual(out, [
            (OpCode.FAST4, (0, 1, 2, 3)),
            (OpCode.FAST2, (4, 5, None, None)),
        ])

    def test_mixed_kinds_break_runs(self):
        instr = [
            (V, "a"),
            (OpCode.CONST, 1),
            (OpCode.FAST, 0),
        ]
        out = pack_ops(instr)
        self.assertEqual(out, instr)

    def test_adjacent_homogeneous_runs_pack_separately(self):
        out = pack_ops([
            (V, "a"), (V, "b"),
            (OpCode.FAST, 0), (OpCode.FAST, 1),
        ])
        self.assertEqual(out, [
            (OpCode.VAR2, ("a", "b", None, None)),
            (OpCode.FAST2, (0, 1, None, None)),
        ])


class TestBulkLoadLowering(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers VAR2/VAR4 to FAST2/FAST4 through the ordinary
    VAR->FAST path, with the all-or-nothing slot rule
    """

    async def test_var2_lowers_to_fast2(self):
        nspec = NamespaceSpec(None, ("a", "b"), ())
        instr = [(OpCode.VAR2, ("a", "b", None, None))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.FAST2, (0, 1, None, None)),
        ])

    async def test_var4_lowers_to_fast4(self):
        nspec = NamespaceSpec(None, ("a", "b"), ("c", "d"))
        instr = [(OpCode.VAR4, ("a", "b", "c", "d"))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.FAST4, (0, 1, 2, 3)),
        ])

    async def test_mixed_eligibility_stays_var(self):
        nspec = NamespaceSpec(None, ("a",), ())
        instr = [(OpCode.VAR2, ("a", "elsewhere", None, None))]
        result = convert_fast(nspec, instr)
        self.assertEqual(result, [
            (OpCode.VAR2, ("a", "elsewhere", None, None)),
        ])


# The end.
