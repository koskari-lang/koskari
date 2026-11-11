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
Unit tests for the CONST2 pair packing in ByteCodeProcessor.pack_runs
(the trailing pack phase)

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import Compare, OpCode

from . import make_fragment


pytestmark = pytest.mark.phase_compile


C = OpCode.CONST


def combine_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.pack_runs()
    return proc.instructions


def process_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    return proc.process()


class TestCombineConst(unittest.TestCase):

    def test_pair_combines(self):
        out = combine_ops([(C, 1), (C, 2)])
        self.assertEqual(out, [(OpCode.CONST2, (1, 2, None, None))])

    def test_three_run_makes_a_triple(self):
        out = combine_ops([(C, 1), (C, 2), (C, 3)])
        self.assertEqual(out, [
            (OpCode.CONST3, (1, 2, 3, None)),
        ])

    def test_five_run_leaves_trailing_const(self):
        out = combine_ops([(C, 1), (C, 2), (C, 3), (C, 4), (C, 5)])
        self.assertEqual(out, [
            (OpCode.CONST4, (1, 2, 3, 4)),
            (C, 5),
        ])

    def test_four_run_makes_a_quad(self):
        out = combine_ops([(C, 1), (C, 2), (C, 3), (C, 4)])
        self.assertEqual(out, [
            (OpCode.CONST4, (1, 2, 3, 4)),
        ])

    def test_run_breaks_at_other_op(self):
        instr = [(C, 1), (OpCode.VAR, "x"), (C, 2)]
        out = combine_ops(instr)
        self.assertEqual(out, instr)

    def test_position_inside_run_is_absorbed(self):
        out = combine_ops([
            (C, 1),
            (OpCode.POSITION, (1, 2)),
            (C, 2),
        ])
        self.assertEqual(out, [
            (OpCode.CONST2, (1, 2, None, None)),
            (OpCode.POSITION, (1, 2)),
        ])

    def test_position_before_run_is_kept(self):
        out = combine_ops([
            (OpCode.POSITION, (1, 2)),
            (C, 1),
            (C, 2),
        ])
        self.assertEqual(out, [
            (OpCode.POSITION, (1, 2)),
            (OpCode.CONST2, (1, 2, None, None)),
        ])

    def test_single_const_untouched(self):
        instr = [(C, 1), (OpCode.RETURN, 1)]
        out = combine_ops(instr)
        self.assertEqual(out, instr)

    def test_process_packs_after_all_passes(self):
        out = process_ops([
            (C, 1),
            (C, 2),
            (OpCode.RETURN, 0),
        ])
        self.assertEqual(out, [
            (OpCode.CONST2, (1, 2, None, None)),
            (OpCode.RETURN, 0),
        ])

    def test_process_packs_fold_products(self):
        # the compare fold's CONST bool ends up adjacent to another CONST
        # and the final pack claims the pair
        out = process_ops([
            (C, "tag"),
            (C, 4),
            (C, 8),
            (OpCode.OBJECT_COMPARE, (Compare.EQ, 2)),
            (OpCode.RETURN, 0),
        ])
        self.assertEqual(out, [
            (OpCode.CONST2, ("tag", False, None, None)),
            (OpCode.RETURN, 0),
        ])


# The end.
