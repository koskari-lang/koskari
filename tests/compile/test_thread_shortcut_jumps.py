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
Tests for the short-circuit jump threading pass of the bytecode
processor: a JUMP_AND or JUMP_OR whose label lands on a conditional
jump becomes the jump that landing performs on the value the
short-circuit jump pushes, and only labels the pass orphans are dropped

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.bootstrap import make_bootstrap
from koskari.compile import Compiler, CompilerMode
from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import FunctionSpec, Module, ModuleNamespace, OpCode
from koskari.reader import Reader, source_str

from . import make_fragment


pytestmark = pytest.mark.phase_compile


C = OpCode.CONST
CALL = OpCode.CALL
J = OpCode.JUMP
JA = OpCode.JUMP_AND
JF = OpCode.JUMP_FALSE
JO = OpCode.JUMP_OR
JT = OpCode.JUMP_TRUE
L = OpCode.LABEL
NOT = OpCode.OBJECT_NOT
NOOP = OpCode.NOOP
P = OpCode.POSITION
POP = OpCode.POP
RET = OpCode.RETURN
V = OpCode.VAR

# the first label a fresh Compiler's generator hands out
FRESH = "label_0001"


def thread(instr):
    proc = ByteCodeProcessor(make_fragment(instr))
    proc.thread_shortcut_jumps()
    return proc.instructions


class TestLandings(unittest.TestCase):
    """
    The eight landings: each short-circuit jump becomes the landing's
    jump on the value it pushes, to the landing's target when the
    outcome is the same jump, and to the landing's fall-through when
    the pushed value would fall through
    """


    def test_and_lands_on_jump_false(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"),
            (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_and_lands_on_jump_true(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (JT, "then"),
            (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, FRESH),
            (V, "b"),
            (JT, "then"),
            (L, FRESH), (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])


    def test_and_lands_on_jump_and(self):
        out = thread([
            (V, "a"), (JA, "inner"),
            (V, "b"),
            (L, "inner"), (JA, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])
        self.assertEqual(out, [
            (V, "a"), (JA, "outer"),
            (V, "b"),
            (JA, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])


    def test_and_lands_on_jump_or(self):
        out = thread([
            (V, "a"), (JA, "inner"),
            (V, "b"),
            (L, "inner"), (JO, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, FRESH),
            (V, "b"),
            (JO, "outer"),
            (L, FRESH), (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])


    def test_or_lands_on_jump_true(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (JT, "then"),
            (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, "then"),
            (V, "b"),
            (JT, "then"),
            (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])


    def test_or_lands_on_jump_false(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, FRESH),
            (V, "b"),
            (JF, "else"),
            (L, FRESH), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_or_lands_on_jump_or(self):
        out = thread([
            (V, "a"), (JO, "inner"),
            (V, "b"),
            (L, "inner"), (JO, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])
        self.assertEqual(out, [
            (V, "a"), (JO, "outer"),
            (V, "b"),
            (JO, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])


    def test_or_lands_on_jump_and(self):
        out = thread([
            (V, "a"), (JO, "inner"),
            (V, "b"),
            (L, "inner"), (JA, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, FRESH),
            (V, "b"),
            (JA, "outer"),
            (L, FRESH), (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ])


class TestChains(unittest.TestCase):
    """
    A nested form threads once its inner landing has been rewritten
    """


    def test_or_inside_and_in_test_position(self):
        # (if (and (or a b) c) 1 2)
        out = thread([
            (V, "a"), (JO, "l1"),
            (V, "b"),
            (L, "l1"), (JA, "l2"),
            (V, "c"),
            (L, "l2"), (JF, "l3"),
            (C, 1), (RET, 1),
            (L, "l3"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, FRESH),
            (V, "b"),
            (JF, "l3"),
            (L, FRESH), (V, "c"),
            (JF, "l3"),
            (C, 1), (RET, 1),
            (L, "l3"), (C, 2), (RET, 1),
        ])


    def test_three_clause_and(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"), (JA, "done"),
            (V, "c"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"), (JF, "else"),
            (V, "c"),
            (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_and_inside_and_as_last_clause(self):
        # (if (and a (and b c)) 1 2): the inner done label sits in
        # front of the outer one
        out = thread([
            (V, "a"), (JA, "outer"),
            (V, "b"), (JA, "inner"),
            (V, "c"),
            (L, "inner"),
            (L, "outer"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"), (JF, "else"),
            (V, "c"),
            (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_or_inside_and_as_last_clause(self):
        # (if (and a (or b c)) 1 2)
        out = thread([
            (V, "a"), (JA, "outer"),
            (V, "b"), (JO, "inner"),
            (V, "c"),
            (L, "inner"),
            (L, "outer"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"), (JT, FRESH),
            (V, "c"),
            (JF, "else"),
            (L, FRESH), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_landing_on_a_self_loop_terminates(self):
        instr = [
            (V, "a"),
            (L, "top"), (JA, "top"),
            (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


class TestLabels(unittest.TestCase):
    """
    Only labels the pass orphans go; a fall-through label is reused
    when one is there and inserted when not
    """


    def test_referenced_label_kept(self):
        out = thread([
            (V, "x"), (JF, "skip"),
            (C, 0), (J, "done"),
            (L, "skip"),
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "x"), (JF, "skip"),
            (C, 0), (J, "done"),
            (L, "skip"),
            (V, "a"), (JF, "else"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_fallthrough_label_reused(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (L, "after"), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, "after"),
            (V, "b"),
            (JF, "else"),
            (L, "after"), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_fallthrough_label_inserted_before_position(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (JF, "else"),
            (P, 7), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, FRESH),
            (V, "b"),
            (JF, "else"),
            (L, FRESH), (P, 7), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_one_fallthrough_label_for_two_jumps(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"), (JO, "done"),
            (V, "c"),
            (L, "done"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, FRESH),
            (V, "b"), (JT, FRESH),
            (V, "c"),
            (JF, "else"),
            (L, FRESH), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


class TestLandingSearch(unittest.TestCase):
    """
    The landing is the first instruction after the label that is not
    POSITION, NOOP or LABEL; any landing that is not a conditional jump
    leaves the jump alone
    """


    def test_position_and_noop_are_transparent(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (P, 3), (NOOP, None), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"),
            (P, 3), (NOOP, None), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_value_consumer_landing_stays(self):
        # (f (and a b))
        instr = [
            (V, "f"),
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (CALL, 1), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


    def test_pop_landing_stays(self):
        instr = [
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (POP, 1), (RET, 0),
        ]
        self.assertEqual(thread(instr), instr)


    def test_return_landing_stays(self):
        instr = [
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


    def test_label_landing_threads_through(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (L, "other"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "else"),
            (V, "b"),
            (L, "other"), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_not_then_value_consumer_stays(self):
        # (f (not (and a b)))
        instr = [
            (V, "f"),
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (CALL, 1), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


    def test_not_then_jump_and_stays(self):
        # (and (not (and a b)) c) in value position
        instr = [
            (V, "a"), (JA, "inner"),
            (V, "b"),
            (L, "inner"), (NOT, None), (JA, "outer"),
            (V, "c"),
            (L, "outer"), (POP, 1), (RET, 0),
        ]
        self.assertEqual(thread(instr), instr)


class TestNotLanding(unittest.TestCase):
    """
    A landing OBJECT_NOT ahead of a JUMP_FALSE or JUMP_TRUE resolves
    as the opposite jump
    """


    def test_and_behind_not_lands_on_jump_false(self):
        # (if (not (and a b)) 1 2): a falsy a takes the then branch
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, FRESH),
            (V, "b"),
            (NOT, None), (JF, "else"),
            (L, FRESH), (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_or_behind_not_lands_on_jump_false(self):
        # (if (not (or a b)) 1 2): a truthy a takes the else branch
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, "else"),
            (V, "b"),
            (NOT, None), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_and_behind_not_lands_on_jump_true(self):
        out = thread([
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (JT, "then"),
            (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JF, "then"),
            (V, "b"),
            (NOT, None), (JT, "then"),
            (C, 2), (RET, 1),
            (L, "then"), (C, 1), (RET, 1),
        ])


    def test_position_between_not_and_jump_is_transparent(self):
        out = thread([
            (V, "a"), (JO, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (P, 4), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])
        self.assertEqual(out, [
            (V, "a"), (JT, "else"),
            (V, "b"),
            (NOT, None), (P, 4), (JF, "else"),
            (C, 1), (RET, 1),
            (L, "else"), (C, 2), (RET, 1),
        ])


    def test_not_then_return_stays(self):
        instr = [
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (NOT, None), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


    def test_unknown_label_stays(self):
        instr = [
            (V, "a"), (JA, "elsewhere"),
            (V, "b"), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


    def test_plain_jump_landing_stays(self):
        instr = [
            (V, "a"), (JA, "done"),
            (V, "b"),
            (L, "done"), (J, "out"),
            (L, "out"), (RET, 1),
        ]
        self.assertEqual(thread(instr), instr)


def compile_function_instructions(src):
    """
    Compile a lambda source and return the (op, arg) instructions of
    its body
    """

    mod_ns = ModuleNamespace("test", dict(make_bootstrap()))
    compiler = Compiler("<unittest>", Module(mod_ns), filename=__file__)
    source = Reader().read(source_str(src, "<unittest>"))
    outer = compiler.complete(compiler.full_compile(source),
                              mode=CompilerMode.EXPRESSION)
    for i in range(len(outer.constants_pool)):
        spec = outer.constant(i)
        if isinstance(spec, FunctionSpec):
            code = spec.code
            return [(OpCode(code.oparg(i)[0]), code.oparg(i)[1])
                    for i in range(code.length())]
    raise AssertionError("no function in the compiled expression")


class TestCompiled(unittest.TestCase):
    """
    Through the whole pipeline, an and test over two compares fuses
    both clauses
    """


    def test_and_test_fuses_both_compares(self):
        instr = compile_function_instructions(
            "(lambda [a b c d] (cond [(and (< a b) (< c d)) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.CHECK_COMPARE_FAST), 2)
        self.assertNotIn(OpCode.JUMP_AND, ops)
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)


    def test_not_and_test_fuses_both_compares(self):
        instr = compile_function_instructions(
            "(lambda [a b c d]"
            " (cond [(not (and (< a b) (< c d))) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.CHECK_COMPARE_FAST), 2)
        self.assertNotIn(OpCode.JUMP_AND, ops)
        self.assertNotIn(OpCode.OBJECT_NOT, ops)
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_TRUE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)


    def test_bare_variable_clauses_take_the_feeder_jumps(self):
        instr = compile_function_instructions(
            "(lambda [a b c] (cond [(and (or a b) c) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.JUMP_TRUE_FAST), 1)
        self.assertEqual(ops.count(OpCode.JUMP_FALSE_FAST), 2)
        self.assertNotIn(OpCode.FAST, ops)
        self.assertNotIn(OpCode.JUMP_TRUE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)
        self.assertNotIn(OpCode.JUMP_AND, ops)
        self.assertNotIn(OpCode.JUMP_OR, ops)


    def test_value_position_or_takes_the_feeder_jump(self):
        instr = compile_function_instructions("(lambda [a b] (list (or a b)))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.JUMP_OR_FAST), 1)
        self.assertNotIn(OpCode.JUMP_OR, ops)
        self.assertEqual(ops.count(OpCode.FAST), 1)


    def test_or_test_fuses_both_compares(self):
        instr = compile_function_instructions(
            "(lambda [a b c d] (cond [(or (< a b) (< c d)) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.CHECK_COMPARE_FAST), 2)
        self.assertNotIn(OpCode.JUMP_OR, ops)
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_TRUE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)


    def test_nested_and_as_last_clause_fuses_every_compare(self):
        instr = compile_function_instructions(
            "(lambda [a b c d e f]"
            " (cond [(and (< a b) (and (< c d) (< e f))) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.CHECK_COMPARE_FAST), 3)
        self.assertNotIn(OpCode.JUMP_AND, ops)
        self.assertNotIn(OpCode.OBJECT_COMPARE, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)


    def test_nested_or_as_last_clause_takes_the_feeder_jumps(self):
        instr = compile_function_instructions(
            "(lambda [a b c] (cond [(and a (or b c)) 1] [else: 2]))")
        ops = [op for op, _ in instr]
        self.assertEqual(ops.count(OpCode.JUMP_FALSE_FAST), 2)
        self.assertEqual(ops.count(OpCode.JUMP_TRUE_FAST), 1)
        self.assertNotIn(OpCode.FAST, ops)
        self.assertNotIn(OpCode.JUMP_OR_FAST, ops)
        self.assertNotIn(OpCode.JUMP_FALSE, ops)
        self.assertNotIn(OpCode.JUMP_OR, ops)
        self.assertNotIn(OpCode.JUMP_AND, ops)


# The end.
