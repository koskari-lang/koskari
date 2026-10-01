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
Unit tests for the ByteCodeProcessor.squish peephole pass

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.processor import CHECK_SQUASHES, ByteCodeProcessor
from koskari.lib import Compare, OpCode, Operand

from . import make_fragment


pytestmark = pytest.mark.phase_compile


def squish_ops(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.squish()
    return proc.instructions


class TestCompress(unittest.TestCase):

    def test_pop_elides_const(self):
        out = squish_ops([
            (OpCode.CONST, 1),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 1)])

    def test_pop_elides_dup(self):
        out = squish_ops([
            (OpCode.DUP, 1),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 1)])

    def test_dup_zero_is_dropped(self):
        out = squish_ops([
            (OpCode.DUP, 0),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.POP, 1), (OpCode.RETURN, 1)])

    def test_pop_zero_is_dropped(self):
        out = squish_ops([
            (OpCode.DUP, 2),
            (OpCode.POP, 0),
        ])
        self.assertEqual(out, [(OpCode.DUP, 2), (OpCode.RETURN, 1)])

    def test_pop_of_every_copy_elides_dup(self):
        out = squish_ops([
            (OpCode.DUP, 2),
            (OpCode.POP, 2),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 1)])

    def test_pop_of_fewer_copies_keeps_dup(self):
        """
        DUP 2 copies the top two in order, so one POP leaves a copy of
        the slot below the top, which no smaller DUP gives
        """

        out = squish_ops([
            (OpCode.DUP, 2),
            (OpCode.POP, 1),
        ])
        self.assertEqual(out, [(OpCode.DUP, 2), (OpCode.POP, 1),
                               (OpCode.RETURN, 1)])

    def test_pop_past_the_copies_keeps_the_rest_of_the_drop(self):
        out = squish_ops([
            (OpCode.DUP, 1),
            (OpCode.POP, 3),
        ])
        self.assertEqual(out, [(OpCode.POP, 2), (OpCode.RETURN, 1)])

    def test_object_not_folds_const_truthy(self):
        out = squish_ops([
            (OpCode.CONST, True),
            (OpCode.OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.CONST, False),
                               (OpCode.RETURN, 1)])

    def test_object_not_folds_const_falsy(self):
        out = squish_ops([
            (OpCode.CONST, 0),
            (OpCode.OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_jump_true_after_const_true_becomes_jump(self):
        out = squish_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.JUMP, "L1")])

    def test_jump_true_after_const_false_is_removed(self):
        out = squish_ops([
            (OpCode.CONST, False),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertNotIn((OpCode.JUMP, "L1"), out)

    def test_jump_false_after_const_false_becomes_jump(self):
        out = squish_ops([
            (OpCode.CONST, False),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.JUMP, "L1")])

    def test_jump_false_after_const_true_is_removed(self):
        out = squish_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "yep"),
        ])
        self.assertNotIn((OpCode.JUMP_FALSE, "L1"), out)
        self.assertNotIn((OpCode.JUMP, "L1"), out)

    def test_check_squash_object_not_to_jump_true(self):
        out = squish_ops([
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
        out = squish_ops([
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

            out = squish_ops([
                (prior, 0),
                (OpCode.JUMP_FALSE, "L1"),
                (OpCode.CONST, "ok"),
                (OpCode.LABEL, "L1"),
                (OpCode.CONST, "bad"),
            ])
            self.assertEqual(out[:1], [(squashed, "L1")],
                             msg="expected %s -> %s" % (prior, squashed))

    def test_jump_to_immediate_label_is_removed(self):
        out = squish_ops([
            (OpCode.JUMP, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, 1),
        ])
        self.assertNotIn((OpCode.JUMP, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_with_const_is_removed(self):
        out = squish_ops([
            (OpCode.CONST, True),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, "one"),
        ])
        self.assertNotIn((OpCode.CONST, True), out)
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_becomes_pop(self):
        out = squish_ops([
            (OpCode.VAR, "x"),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.LABEL, "L1"),
            (OpCode.CONST, 1),
        ])
        self.assertIn((OpCode.POP, 1), out)
        self.assertNotIn((OpCode.JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.LABEL, "L1"), out)

    def test_const_none_before_return_is_removed(self):
        out = squish_ops([
            (OpCode.CONST, None),
            (OpCode.RETURN, 1),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 0)])

    def test_build_args_before_control_takes_the_count(self):
        for op in (OpCode.RETURN, OpCode.BREAK, OpCode.CONTINUE,
                   OpCode.YIELD):
            out = squish_ops([
                (OpCode.CONST, 1),
                (OpCode.CONST, 2),
                (OpCode.BUILD_ARGS, 2),
                (op, 1),
            ])
            # squish appends RETURN 1 when the stream does not end in one
            self.assertEqual(
                out[:3], [(OpCode.CONST, 1), (OpCode.CONST, 2), (op, 2)])

    def test_build_args_below_two_stays(self):
        for count in (0, 1):
            rows = [(OpCode.CONST, 1)] * count
            rows += [(OpCode.BUILD_ARGS, count), (OpCode.RETURN, 1)]
            self.assertEqual(squish_ops(rows), rows)

    def test_build_args_before_control_zero_stays(self):
        rows = [
            (OpCode.CONST, 1),
            (OpCode.CONST, 2),
            (OpCode.BUILD_ARGS, 2),
            (OpCode.POP, 1),
            (OpCode.RETURN, 0),
        ]
        out = squish_ops(rows)
        self.assertIn((OpCode.BUILD_ARGS, 2), out)
        self.assertIn((OpCode.RETURN, 0), out)

    def test_build_args_across_label_stays(self):
        rows = [
            (OpCode.CONST, 1),
            (OpCode.CONST, 2),
            (OpCode.BUILD_ARGS, 2),
            (OpCode.LABEL, "join"),
            (OpCode.RETURN, 1),
        ]
        self.assertEqual(squish_ops(rows), rows)

    def test_trailing_build_args_folds_into_the_appended_return(self):
        out = squish_ops([
            (OpCode.CONST, 1),
            (OpCode.CONST, 2),
            (OpCode.BUILD_ARGS, 2),
        ])
        self.assertEqual(
            out, [(OpCode.CONST, 1), (OpCode.CONST, 2), (OpCode.RETURN, 2)])

    def test_trailing_build_args_below_two_keeps_return_one(self):
        for count in (0, 1):
            rows = [(OpCode.CONST, 1)] * count + [(OpCode.BUILD_ARGS, count)]
            self.assertEqual(squish_ops(rows), rows + [(OpCode.RETURN, 1)])

    def test_missing_return_appends_return_one(self):
        out = squish_ops([
            (OpCode.CONST, 123),
        ])
        self.assertEqual(out[-1], (OpCode.RETURN, 1))

    def test_trailing_const_none_becomes_return_zero(self):
        out = squish_ops([
            (OpCode.CONST, None),
        ])
        self.assertEqual(out, [(OpCode.RETURN, 0)])

    def test_consecutive_cdr_merges_to_sum(self):
        out = squish_ops([
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
        out = squish_ops([
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
        out = squish_ops([
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
        out = squish_ops([
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
        out = squish_ops([
            (OpCode.CONST, 42),
            (OpCode.VAR_STORE, "x"),
        ])
        self.assertEqual(
            out,
            [(OpCode.VAR_STORE_CONST, ("x", 42, None, None)),
             (OpCode.RETURN, 1)])

    def test_var_store_not_squashed_without_const(self):
        out = squish_ops([
            (OpCode.VAR, "y"),
            (OpCode.VAR_STORE, "x"),
        ])
        self.assertIn((OpCode.VAR_STORE, "x"), out)
        self.assertNotIn(OpCode.VAR_STORE_CONST, [op for op, _ in out])

    def test_var_store_not_squashed_when_prior_not_const(self):
        out = squish_ops([
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
        out = squish_ops([
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
        out = squish_ops([
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
        out = squish_ops(instr)
        self.assertIn(
            (OpCode.VAR_INPLACE,
             ((Operand.ADD, 2), "i", 0, 0)),
            out)
        self.assertNotIn(
            OpCode.VAR_INPLACE_CONST, [op for op, _ in out])


class TestCompareConstFold(unittest.TestCase):
    """
    An all-constant OBJECT_COMPARE chain folds to a single CONST bool
    at compile time; non-foldable selectors, non-constant feeders, and
    comparison errors leave the op in place
    """

    def test_lt_folds_true(self):
        out = squish_ops([
            (OpCode.CONST, 4),
            (OpCode.CONST, 8),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_gt_folds_false(self):
        out = squish_ops([
            (OpCode.CONST, 4),
            (OpCode.CONST, 8),
            (OpCode.OBJECT_COMPARE, (Compare.GT, 2)),
        ])
        self.assertEqual(out, [(OpCode.CONST, False),
                               (OpCode.RETURN, 1)])

    def test_merged_int_operand_folds(self):
        out = squish_ops([
            (OpCode.CONST, 5),
            (OpCode.CONST, 5),
            (OpCode.OBJECT_COMPARE, (int(Compare.EQ) << 8) | 2),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_chain_folds_true(self):
        out = squish_ops([
            (OpCode.CONST, 1),
            (OpCode.CONST, 2),
            (OpCode.CONST, 3),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 3)),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_chain_folds_false(self):
        out = squish_ops([
            (OpCode.CONST, 1),
            (OpCode.CONST, 3),
            (OpCode.CONST, 2),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 3)),
        ])
        self.assertEqual(out, [(OpCode.CONST, False),
                               (OpCode.RETURN, 1)])

    def test_membership_folds(self):
        out = squish_ops([
            (OpCode.CONST, 3),
            (OpCode.CONST, (1, 2, 3)),
            (OpCode.OBJECT_COMPARE, (Compare.IN, 2)),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_not_in_folds(self):
        out = squish_ops([
            (OpCode.CONST, 9),
            (OpCode.CONST, (1, 2, 3)),
            (OpCode.OBJECT_COMPARE, (Compare.NOT_IN, 2)),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.RETURN, 1)])

    def test_position_between_feeders_is_kept(self):
        out = squish_ops([
            (OpCode.CONST, 4),
            (OpCode.POSITION, (1, 2)),
            (OpCode.CONST, 8),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
        ])
        self.assertEqual(out, [(OpCode.CONST, True),
                               (OpCode.POSITION, (1, 2)),
                               (OpCode.RETURN, 1)])

    def test_identity_selector_declines(self):
        instr = [
            (OpCode.CONST, None),
            (OpCode.CONST, None),
            (OpCode.OBJECT_COMPARE, (Compare.IS, 2)),
        ]
        out = squish_ops(instr)
        self.assertEqual(out, instr + [(OpCode.RETURN, 1)])

    def test_non_const_feeder_declines(self):
        instr = [
            (OpCode.VAR, "x"),
            (OpCode.CONST, 1),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
        ]
        out = squish_ops(instr)
        self.assertEqual(out, instr + [(OpCode.RETURN, 1)])

    def test_uncomparable_consts_decline(self):
        instr = [
            (OpCode.CONST, 1),
            (OpCode.CONST, "a"),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
        ]
        out = squish_ops(instr)
        self.assertEqual(out, instr + [(OpCode.RETURN, 1)])

    def test_membership_bad_arity_declines(self):
        instr = [
            (OpCode.CONST, 1),
            (OpCode.CONST, 2),
            (OpCode.CONST, 3),
            (OpCode.OBJECT_COMPARE, (Compare.IN, 3)),
        ]
        out = squish_ops(instr)
        self.assertEqual(out, instr + [(OpCode.RETURN, 1)])

    def test_fold_feeds_jump_false_elision(self):
        out = squish_ops([
            (OpCode.CONST, 4),
            (OpCode.CONST, 8),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
            (OpCode.JUMP_FALSE, "L1"),
            (OpCode.CONST, "yep"),
            (OpCode.LABEL, "L1"),
        ])
        ops = [op for op, _ in out]
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.CHECK_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)
        self.assertNotIn(OpCode.JUMP, ops)
        self.assertIn((OpCode.CONST, "yep"), out)

    def test_fold_feeds_jump_true_conversion(self):
        out = squish_ops([
            (OpCode.CONST, 4),
            (OpCode.CONST, 8),
            (OpCode.OBJECT_COMPARE, (Compare.LT, 2)),
            (OpCode.JUMP_TRUE, "L1"),
            (OpCode.CONST, "nope"),
            (OpCode.LABEL, "L1"),
        ])
        ops = [op for op, _ in out]
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_TRUE, ops)
        self.assertIn((OpCode.JUMP, "L1"), out)


# The end.
