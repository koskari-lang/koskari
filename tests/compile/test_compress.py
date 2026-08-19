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
Unit tests for ByteCodeProcessor.compress peephole optimizations

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import ByteCodeProcessor, CHECK_SQUASHES
from koskari.lib import OpCode, Operand


pytestmark = pytest.mark.phase_compile


def compress_ops(instr):
    proc = ByteCodeProcessor(instr)
    proc.compress()
    return proc.instructions


class TestCompress(unittest.TestCase):

    def test_pop_elides_const(self):
        out = compress_ops([
            (OpCode.CONST, 1),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 1)])

    def test_pop_elides_dup(self):
        out = compress_ops([
            (OpCode.DUP, 0),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 1)])

    def test_object_not_folds_const_truthy(self):
        out = compress_ops([
            (OpCode.CONST, True),
            (OpCode.OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.CONST, False),
                               (OpCode.RETURN, 1)])

    def test_object_not_folds_const_falsy(self):
        out = compress_ops([
            (OpCode.CONST, 0),
            (OpCode.OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_jump_true_after_const_true_becomes_jump(self):
        out = compress_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.JUMP, "L1")])

    def test_jump_true_after_const_false_is_removed(self):
        out = compress_ops([
            (OpCode.CONST, False),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertNotIn((OpCode.JUMP, "L1"), out)

    def test_jump_false_after_const_false_becomes_jump(self):
        out = compress_ops([
            (OpCode.CONST, False),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.JUMP, "L1")])

    def test_jump_false_after_const_true_is_removed(self):
        out = compress_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertNotIn((OpCode.JUMP_FALSE, "L1"), out)
        self.assertNotIn((OpCode.JUMP, "L1"), out)

    def test_check_squash_object_not_to_jump_true(self):
        out = compress_ops([
            (OpCode.OBJECT_NOT, 0),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, 123),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, 456),
        ])
        self.assertEqual(out[:1], [(OpCode.JUMP_TRUE, "L1")])

    def test_check_squash_exception_match_to_check_exception(self):
        """
        EXCEPTION_MATCH then JUMP_FALSE squashes to CHECK_EXCEPTION
        """
        out = compress_ops([
            (OpCode.EXCEPTION_MATCH, 0),
            (OpCode.JUMP_FALSE, "handler"),
            (OpCode.CONST, "matched"),
            (OpCode.LABEL, "handler"),
            (OpCode.CONST, "no_match"),
        ])
        self.assertEqual(out[:1], [(OpCode.CHECK_EXCEPTION, "handler")])

    def test_check_squash_object_ops(self):
        for prior, squashed in CHECK_SQUASHES.items():
            if prior == OpCode.OBJECT_NOT:
                continue

            out = compress_ops([
                (prior, 0),
                (OpCode.JUMP_FALSE, "L1"),
                (OpCode.CONST, "ok"),
                (OpCode.LABEL, "L1"),
                (OpCode.CONST, "bad"),
            ])
            self.assertEqual(out[:1], [(squashed, "L1")],
                             msg="expected %s -> %s" % (prior, squashed))

    def test_jump_to_immediate_label_is_removed(self):
        out = compress_ops([
            (OpCode.JUMP, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, 1),
        ])
        self.assertNotIn((OpCode.JUMP, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_with_const_is_removed(self):
        out = compress_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "one"),
        ])
        self.assertNotIn((OpCode.CONST, True), out)
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_becomes_pop(self):
        out = compress_ops([
            (OpCode.VAR, "x"),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, 1),
        ])
        self.assertIn((OpCode.POP, 1), out)
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_const_none_before_return_is_removed(self):
        out = compress_ops([
            (OpCode.CONST, None),
            (OpCode.RETURN, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 0)])

    def test_missing_return_appends_return_one(self):
        out = compress_ops([
            (OpCode.CONST, 123),
        ])
        self.assertEqual(out[-1], (OpCode.RETURN, 1))

    def test_trailing_const_none_becomes_return_zero(self):
        out = compress_ops([
            (OpCode.CONST, None),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 0)])

    def test_consecutive_cdr_merges_to_sum(self):
        out = compress_ops([
            (OpCode.CONST, 0),
            (OpCode.CDR, 1),
            (OpCode.CDR, 1),
            (OpCode.CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.CONST, 0),
             (OpCode.CDR, 3),
             (OpCode.RETURN, 1)])

    def test_consecutive_cdr_merges_mixed_counts(self):
        out = compress_ops([
            (OpCode.CONST, 0),
            (OpCode.CDR, 2),
            (OpCode.CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.CONST, 0),
             (OpCode.CDR, 3),
             (OpCode.RETURN, 1)])

    def test_consecutive_car_merges_to_sum(self):
        out = compress_ops([
            (OpCode.CONST, 0),
            (OpCode.CAR, 1),
            (OpCode.CAR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.CONST, 0),
             (OpCode.CAR, 2),
             (OpCode.RETURN, 1)])

    def test_car_then_cdr_not_merged(self):
        out = compress_ops([
            (OpCode.CONST, 0),
            (OpCode.CAR, 1),
            (OpCode.CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.CONST, 0),
             (OpCode.CAR, 1),
             (OpCode.CDR, 1),
             (OpCode.RETURN, 1)])

    def test_var_store_const_squash(self):
        out = compress_ops([
            (OpCode.CONST, 42),
            (OpCode.VAR_STORE, "x"),
        ])
        self.assertEqual(
            out,
            [(OpCode.VAR_STORE_CONST, ("x", 42, None, None)),
             (OpCode.RETURN, 1)])

    def test_var_store_not_squashed_without_const(self):
        out = compress_ops([
            (OpCode.VAR, "y"),
            (OpCode.VAR_STORE, "x"),
        ])
        self.assertIn((OpCode.VAR_STORE, "x"), out)
        self.assertNotIn(OpCode.VAR_STORE_CONST, [op for op, _ in out])

    def test_var_store_not_squashed_when_prior_not_const(self):
        out = compress_ops([
            (OpCode.CONST, 1),
            (OpCode.VAR, "y"),
            (OpCode.VAR_STORE, "x"),
        ])
        self.assertIn((OpCode.CONST, 1), out)
        self.assertIn((OpCode.VAR, "y"), out)
        self.assertIn((OpCode.VAR_STORE, "x"), out)
        self.assertNotIn(OpCode.VAR_STORE_CONST, [op for op, _ in out])


class TestInplaceConstSquash(unittest.TestCase):
    """
    CONST rhs on a single-operand VAR_INPLACE fuses to the
    stack-free VAR_INPLACE_CONST form
    """

    def test_const_rhs_fuses(self):
        out = compress_ops([
            (OpCode.CONST, 1),
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 1), "i", 0, 0)),
        ])
        self.assertIn(
            (OpCode.VAR_INPLACE_CONST,
             (int(Operand.ADD), "i", 1, None)),
            out)
        self.assertNotIn(
            OpCode.VAR_INPLACE, [op for op, _ in out])

    def test_computed_rhs_stays_generic(self):
        out = compress_ops([
            (OpCode.VAR, "y"),
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 1), "i", 0, 0)),
        ])
        self.assertIn(
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 1), "i", 0, 0)),
            out)

    def test_multi_operand_stays_generic(self):
        instr = [
            (OpCode.VAR, "y"),
            (OpCode.CONST, 1),
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 2), "i", 0, 0)),
        ]
        out = compress_ops(instr)
        self.assertIn(
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 2), "i", 0, 0)),
            out)
        self.assertNotIn(
            OpCode.VAR_INPLACE_CONST, [op for op, _ in out])


# The end.
