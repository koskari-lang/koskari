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
Unit tests for Compiler.compress peephole optimizations

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile.compiler import _CHECK_SQUASHES, Compiler
from koskari.lib import Module, ModuleNamespace, OpCode


pytestmark = pytest.mark.phase_compile


def make_compiler():
    mod = Module(ModuleNamespace("test", {}))
    return Compiler("test", mod)


def compress_ops(instr):
    return make_compiler().compress(instr)


class TestCompress(unittest.TestCase):

    def test_pop_elides_const(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 1),
            (OpCode.KOS_OP_POP, 1),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_RETURN, 1)])

    def test_pop_elides_dup(self):
        out = compress_ops([
            (OpCode.KOS_OP_DUP, 0),
            (OpCode.KOS_OP_POP, 1),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_RETURN, 1)])

    def test_object_not_folds_const_truthy(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, True),
            (OpCode.KOS_OP_OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_CONST, False),
                               (OpCode.KOS_OP_RETURN, 1)])

    def test_object_not_folds_const_falsy(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 0),
            (OpCode.KOS_OP_OBJECT_NOT, 0),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_CONST, True),
                               (OpCode.KOS_OP_RETURN, 1)])

    def test_jump_true_after_const_true_becomes_jump(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, True),
            (OpCode.KOS_OP_JUMP_TRUE, "L1"),
            (OpCode.KOS_OP_CONST, "nope"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.KOS_OP_JUMP, "L1")])

    def test_jump_true_after_const_false_is_removed(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, False),
            (OpCode.KOS_OP_JUMP_TRUE, "L1"),
            (OpCode.KOS_OP_CONST, "nope"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, "yep"),
        ])
        self.assertNotIn((OpCode.KOS_OP_JUMP_TRUE, "L1"), out)
        self.assertNotIn((OpCode.KOS_OP_JUMP, "L1"), out)

    def test_jump_false_after_const_false_becomes_jump(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, False),
            (OpCode.KOS_OP_JUMP_FALSE, "L1"),
            (OpCode.KOS_OP_CONST, "nope"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, "yep"),
        ])
        self.assertEqual(out[:1], [(OpCode.KOS_OP_JUMP, "L1")])

    def test_jump_false_after_const_true_is_removed(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, True),
            (OpCode.KOS_OP_JUMP_FALSE, "L1"),
            (OpCode.KOS_OP_CONST, "nope"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, "yep"),
        ])
        self.assertNotIn((OpCode.KOS_OP_JUMP_FALSE, "L1"), out)
        self.assertNotIn((OpCode.KOS_OP_JUMP, "L1"), out)

    def test_check_squash_object_not_to_jump_true(self):
        out = compress_ops([
            (OpCode.KOS_OP_OBJECT_NOT, 0),
            (OpCode.KOS_OP_JUMP_FALSE, "L1"),
            (OpCode.KOS_OP_CONST, 123),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, 456),
        ])
        self.assertEqual(out[:1], [(OpCode.KOS_OP_JUMP_TRUE, "L1")])

    def test_check_squash_exception_match_to_check_exception(self):
        """
        EXCEPTION_MATCH then JUMP_FALSE squashes to CHECK_EXCEPTION
        """
        out = compress_ops([
            (OpCode.KOS_OP_EXCEPTION_MATCH, 0),
            (OpCode.KOS_OP_JUMP_FALSE, "handler"),
            (OpCode.KOS_OP_CONST, "matched"),
            (OpCode.KOS_OP_LABEL, "handler"),
            (OpCode.KOS_OP_CONST, "no_match"),
        ])
        self.assertEqual(out[:1], [(OpCode.KOS_OP_CHECK_EXCEPTION, "handler")])

    def test_check_squash_object_ops(self):
        for prior, squashed in _CHECK_SQUASHES.items():
            if prior == OpCode.KOS_OP_OBJECT_NOT:
                continue

            out = compress_ops([
                (prior, 0),
                (OpCode.KOS_OP_JUMP_FALSE, "L1"),
                (OpCode.KOS_OP_CONST, "ok"),
                (OpCode.KOS_OP_LABEL, "L1"),
                (OpCode.KOS_OP_CONST, "bad"),
            ])
            self.assertEqual(out[:1], [(squashed, "L1")],
                             msg="expected %s -> %s" % (prior, squashed))

    def test_jump_to_immediate_label_is_removed(self):
        out = compress_ops([
            (OpCode.KOS_OP_JUMP, "L1"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, 1),
        ])
        self.assertNotIn((OpCode.KOS_OP_JUMP, "L1"), out)
        self.assertIn((OpCode.KOS_OP_LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_with_const_is_removed(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, True),
            (OpCode.KOS_OP_JUMP_TRUE, "L1"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, "one"),
        ])
        self.assertNotIn((OpCode.KOS_OP_CONST, True), out)
        self.assertNotIn((OpCode.KOS_OP_JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.KOS_OP_LABEL, "L1"), out)

    def test_cond_jump_to_immediate_label_becomes_pop(self):
        out = compress_ops([
            (OpCode.KOS_OP_VAR, "x"),
            (OpCode.KOS_OP_JUMP_TRUE, "L1"),
            (OpCode.KOS_OP_LABEL, "L1"),
            (OpCode.KOS_OP_CONST, 1),
        ])
        self.assertIn((OpCode.KOS_OP_POP, 1), out)
        self.assertNotIn((OpCode.KOS_OP_JUMP_TRUE, "L1"), out)
        self.assertIn((OpCode.KOS_OP_LABEL, "L1"), out)

    def test_const_none_before_return_is_removed(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, None),
            (OpCode.KOS_OP_RETURN, 1),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_RETURN, 0)])

    def test_missing_return_appends_return_one(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 123),
        ])
        self.assertEqual(out[-1], (OpCode.KOS_OP_RETURN, 1))

    def test_trailing_const_none_becomes_return_zero(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, None),
        ])
        self.assertEqual(out, [(OpCode.KOS_OP_RETURN, 0)])

    def test_consecutive_cdr_merges_to_sum(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 0),
            (OpCode.KOS_OP_CDR, 1),
            (OpCode.KOS_OP_CDR, 1),
            (OpCode.KOS_OP_CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.KOS_OP_CONST, 0),
             (OpCode.KOS_OP_CDR, 3),
             (OpCode.KOS_OP_RETURN, 1)])

    def test_consecutive_cdr_merges_mixed_counts(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 0),
            (OpCode.KOS_OP_CDR, 2),
            (OpCode.KOS_OP_CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.KOS_OP_CONST, 0),
             (OpCode.KOS_OP_CDR, 3),
             (OpCode.KOS_OP_RETURN, 1)])

    def test_consecutive_car_merges_to_sum(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 0),
            (OpCode.KOS_OP_CAR, 1),
            (OpCode.KOS_OP_CAR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.KOS_OP_CONST, 0),
             (OpCode.KOS_OP_CAR, 2),
             (OpCode.KOS_OP_RETURN, 1)])

    def test_car_then_cdr_not_merged(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 0),
            (OpCode.KOS_OP_CAR, 1),
            (OpCode.KOS_OP_CDR, 1),
        ])
        self.assertEqual(
            out,
            [(OpCode.KOS_OP_CONST, 0),
             (OpCode.KOS_OP_CAR, 1),
             (OpCode.KOS_OP_CDR, 1),
             (OpCode.KOS_OP_RETURN, 1)])

    def test_var_store_const_squash(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 42),
            (OpCode.KOS_OP_VAR_STORE, "x"),
        ])
        self.assertEqual(
            out,
            [(OpCode.KOS_OP_VAR_STORE_CONST, ("x", 42, None, None)),
             (OpCode.KOS_OP_RETURN, 1)])

    def test_var_store_not_squashed_without_const(self):
        out = compress_ops([
            (OpCode.KOS_OP_VAR, "y"),
            (OpCode.KOS_OP_VAR_STORE, "x"),
        ])
        self.assertIn((OpCode.KOS_OP_VAR_STORE, "x"), out)
        self.assertNotIn(OpCode.KOS_OP_VAR_STORE_CONST, [op for op, _ in out])

    def test_var_store_not_squashed_when_prior_not_const(self):
        out = compress_ops([
            (OpCode.KOS_OP_CONST, 1),
            (OpCode.KOS_OP_VAR, "y"),
            (OpCode.KOS_OP_VAR_STORE, "x"),
        ])
        self.assertIn((OpCode.KOS_OP_CONST, 1), out)
        self.assertIn((OpCode.KOS_OP_VAR, "y"), out)
        self.assertIn((OpCode.KOS_OP_VAR_STORE, "x"), out)
        self.assertNotIn(OpCode.KOS_OP_VAR_STORE_CONST, [op for op, _ in out])


# The end.
