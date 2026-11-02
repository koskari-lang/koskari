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
tests.lib.test_code_trust_boundary

Code is the trust boundary for bytecode from any source: its accessors
bound their index at full width, its finalizer refuses an attribute
name that is not a str and an operand outside its range, a frame
refuses a namespace with fewer fast slots than the code needs, a jump
cannot enter a block past its push, and a label that is not a str or
int is refused.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    Code, Compare, Expression, FormalsSpec, Function, NamespaceSpec, OpCode,
    Operand, Ref, cons, nil)
from koskari.module import load_module_source_async, new_module
from tests.lib.opcode import opcount


pytestmark = pytest.mark.phase_lib


def make(instrs, spec=None):
    return Code(spec or NamespaceSpec("t", (), ()), instrs, "t")


class TestIndexAccessors(unittest.TestCase):

    def setUp(self):
        self.code = make([[OpCode.CONST, 1], [OpCode.RETURN, 1]])

    def test_oparg_index_past_32_bits(self):
        with self.assertRaises(IndexError):
            self.code.oparg(2 ** 32)
        with self.assertRaises(IndexError):
            self.code.oparg(2 ** 70)
        with self.assertRaises(IndexError):
            self.code.oparg(-1)

    def test_position_index_past_32_bits(self):
        with self.assertRaises(IndexError):
            self.code.position(2 ** 32)
        with self.assertRaises(IndexError):
            self.code.position(-1)

    def test_constant_index_past_32_bits(self):
        self.assertEqual(1, self.code.constant(0))
        with self.assertRaises(IndexError):
            self.code.constant(2 ** 32)
        with self.assertRaises(IndexError):
            self.code.constant(1)


class TestAttributeNameConstant(unittest.TestCase):

    def test_non_str_attribute_name_is_refused(self):
        for name in (None, ("a",), 7):
            with self.assertRaises(TypeError) as caught:
                make([[OpCode.CONST, "receiver"],
                      [OpCode.CALL_ATTR, (name, 0, 0, 0)],
                      [OpCode.RETURN, 1]])
            self.assertIn("instruction 1", str(caught.exception))

    def test_str_attribute_name_is_accepted(self):
        make([[OpCode.CONST, "receiver"],
              [OpCode.CALL_ATTR, ("upper", 0, 0, 0)],
              [OpCode.RETURN, 1]])
        make([[OpCode.CONST, "receiver"],
              [OpCode.CALL_ATTR_NAMES, ("upper", (), 0, 0)],
              [OpCode.RETURN, 1]])


class TestLabelTypes(unittest.TestCase):

    def test_label_that_is_not_a_str_or_int_is_refused(self):
        class Name(str):
            pass

        for label in (None, ("a",), 1.0, True, Name("a")):
            shapes = (
                [[OpCode.LABEL, label], [OpCode.RETURN, 0]],
                [[OpCode.JUMP, label], [OpCode.LABEL, "a"],
                 [OpCode.RETURN, 0]],
                [[OpCode.CONST, 0],
                 [OpCode.SWITCH_EQ_CONST, (label, 1, 0, 0)],
                 [OpCode.LABEL, "a"], [OpCode.RETURN, 1]],
            )
            for instrs in shapes:
                with self.assertRaises(TypeError) as caught:
                    make(instrs)
                self.assertIn("label must be a str or int",
                              str(caught.exception))

    def test_str_and_int_labels_are_accepted(self):
        make([[OpCode.JUMP, "a"], [OpCode.LABEL, "a"],
              [OpCode.RETURN, 0]])
        make([[OpCode.JUMP, 7], [OpCode.LABEL, 7], [OpCode.RETURN, 0]])


class TestCompilerPositionWidth(unittest.IsolatedAsyncioTestCase):

    async def test_a_position_past_16_bits_saturates(self):
        mod = new_module("position_width")
        src = "\n" * 70000 + "var v 1\n"
        await load_module_source_async(mod, src, layout=True, seal=False)
        self.assertEqual(1, vars(mod)["v"])


class TestOperandRanges(unittest.TestCase):

    def test_non_int_opcount_is_refused_with_the_instruction_named(self):
        with self.assertRaises(TypeError) as caught:
            make([[OpCode.CONST, 1], [OpCode.CONST, 2],
                  [OpCode.OBJECT_OPERAND, "x"], [OpCode.RETURN, 1]])
        self.assertIn("instruction 2", str(caught.exception))

    def test_opcount_outside_16_bits_is_refused(self):
        with self.assertRaises(ValueError):
            make([[OpCode.CONST, 1], [OpCode.CONST, 2],
                  [OpCode.OBJECT_OPERAND, 0x10002], [OpCode.RETURN, 1]])
        with self.assertRaises(ValueError):
            make([[OpCode.CONST, 1], [OpCode.CONST, 2],
                  [OpCode.OBJECT_OPERAND, 2 ** 70], [OpCode.RETURN, 1]])

    def test_opcount_selector_past_the_operators_is_refused(self):
        bad = opcount(len(Operand), 2)
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1], [OpCode.CONST, 2],
                  [OpCode.OBJECT_OPERAND, bad], [OpCode.RETURN, 1]])
        self.assertIn("instruction 2", str(caught.exception))
        make([[OpCode.CONST, 1], [OpCode.CONST, 2],
              [OpCode.OBJECT_OPERAND, opcount(Operand.ADD, 2)],
              [OpCode.RETURN, 1]])

    def test_compare_selector_past_the_comparisons_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1],
                  [OpCode.OBJECT_COMPARE_VAR, (len(Compare), 0, "x", 0)],
                  [OpCode.RETURN, 1]])
        self.assertIn("instruction 1", str(caught.exception))
        make([[OpCode.CONST, 1],
              [OpCode.OBJECT_COMPARE_VAR, (Compare.LT, 0, "x", 0)],
              [OpCode.RETURN, 1]])

    def test_operand_selector_past_the_operators_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1],
                  [OpCode.VAR_INPLACE, (len(Operand), "x", None, None)],
                  [OpCode.RETURN, 1]])
        self.assertIn("instruction 1", str(caught.exception))
        make([[OpCode.CONST, 1],
              [OpCode.VAR_INPLACE, (Operand.ADD, "x", None, None)],
              [OpCode.RETURN, 1]])

    def test_compare_source_selector_past_the_comparisons_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CHECK_COMPARE_VAR, (0x0f, "a", "b", "out")],
                  [OpCode.LABEL, "out"],
                  [OpCode.CONST, 1], [OpCode.RETURN, 1]])
        self.assertIn("instruction 0", str(caught.exception))
        make([[OpCode.CHECK_COMPARE_VAR, (Compare.LT, "a", "b", "out")],
              [OpCode.LABEL, "out"],
              [OpCode.CONST, 1], [OpCode.RETURN, 1]])

    def test_position_outside_16_bits_is_refused(self):
        with self.assertRaises(ValueError):
            make([[OpCode.POSITION, (70000, 1)], [OpCode.CONST, 1],
                  [OpCode.RETURN, 1]])
        with self.assertRaises(ValueError):
            make([[OpCode.POSITION, (1, -1)], [OpCode.CONST, 1],
                  [OpCode.RETURN, 1]])
        make([[OpCode.POSITION, (65535, 65535)], [OpCode.CONST, 1],
              [OpCode.RETURN, 1]])


class TestFastSlotsAgainstTheFrame(unittest.IsolatedAsyncioTestCase):

    async def test_formals_that_dedup_leave_too_few_slots(self):
        spec = NamespaceSpec(None, ("x",) + ("a",) * 2000, ())
        code = Code(spec, [[OpCode.CONST, "x"], [OpCode.FAST_STORE, 2000],
                           [OpCode.CONST, 1], [OpCode.RETURN, 1]], "t")
        fn = Function(code, NamespaceSpec(None, (), ()).get_namespace(None),
                      FormalsSpec(("x",)))
        with self.assertRaises(RuntimeError):
            await fn(1)

    async def test_expression_over_a_smaller_namespace(self):
        spec = NamespaceSpec(None, ("a", "b", "c"), ())
        code = Code(spec, [[OpCode.CONST, "x"], [OpCode.FAST_STORE, 2],
                           [OpCode.CONST, 1], [OpCode.RETURN, 1]], "t")
        small = NamespaceSpec(None, ("a",), ()).get_namespace(None)
        with self.assertRaises(RuntimeError):
            await Expression(code, small)()
        self.assertEqual(1, await Expression(code, spec.get_namespace(None))())

    async def test_dedup_formals_in_tail_position_raise_cleanly(self):
        spec = NamespaceSpec(None, ("x",) + ("a",) * 2000, ())
        code = Code(spec, [[OpCode.CONST, "x"], [OpCode.FAST_STORE, 2000],
                           [OpCode.CONST, 1], [OpCode.RETURN, 1]], "t")
        callee = Function(code,
                          NamespaceSpec(None, (), ()).get_namespace(None),
                          FormalsSpec(("x",)))
        outer = NamespaceSpec(None, ("f",), ())
        caller_code = Code(outer, [[OpCode.FAST, 0], [OpCode.CONST, 1],
                                   [OpCode.TAIL_CALL, 1]], "caller")
        ns = outer.get_namespace(None)
        ns.bind("f", Ref("f", callee))
        with self.assertRaises(RuntimeError):
            await Expression(caller_code, ns)()
        self.assertEqual(1, await Expression(
            Code(outer, [[OpCode.CONST, 1], [OpCode.RETURN, 1]], "ok"),
            ns)())

    async def test_expression_over_a_module_namespace(self):
        spec = NamespaceSpec(None, ("a",), ())
        code = Code(spec, [[OpCode.CONST, "x"], [OpCode.FAST_STORE, 0],
                           [OpCode.CONST, 1], [OpCode.RETURN, 1]], "t")
        mod = new_module("trust_boundary_mod")
        with self.assertRaises(RuntimeError):
            await Expression(code, vars(mod))()

    def test_jump_to_a_nested_blocks_pop_is_refused(self):
        inner = NamespaceSpec("inner", ("q", "r", "s"), ())
        outer = NamespaceSpec("outer", tuple("abcdefghij"), ())
        with self.assertRaises(RuntimeError) as caught:
            make([[OpCode.PUSH_NS, (outer, None, 0, 0)],
                  [OpCode.JUMP, "p"],
                  [OpCode.PUSH_NS, (inner, None, 0, 0)],
                  [OpCode.LABEL, "p"],
                  [OpCode.POP_NS, 0],
                  [OpCode.CONST, "x"], [OpCode.FAST_STORE, 9],
                  [OpCode.POP_NS, 0],
                  [OpCode.CONST, 1], [OpCode.RETURN, 1]])
        self.assertIn("POP", str(caught.exception))

    def test_jump_into_a_pushed_block_is_refused(self):
        inner = NamespaceSpec("inner", ("q", "r", "s"), ())
        with self.assertRaises(RuntimeError) as caught:
            make([[OpCode.JUMP, "body"],
                  [OpCode.PUSH_NS, (inner, None, 0, 0)],
                  [OpCode.LABEL, "body"],
                  [OpCode.CONST, "x"], [OpCode.FAST_STORE, 2],
                  [OpCode.POP_NS, 0],
                  [OpCode.CONST, 1], [OpCode.RETURN, 1]])
        self.assertIn("JUMP", str(caught.exception))

    def test_handler_into_a_nested_block_is_refused(self):
        inner = NamespaceSpec("inner", ("q", "r", "s"), ())
        with self.assertRaises(RuntimeError) as caught:
            make([[OpCode.PUSH_TRY, "handler"],
                  [OpCode.PUSH_NS, (inner, None, 0, 0)],
                  [OpCode.LABEL, "handler"],
                  [OpCode.CONST, "x"], [OpCode.FAST_STORE, 2],
                  [OpCode.POP_NS, 0],
                  [OpCode.POP_TRY, 0],
                  [OpCode.CONST, 1], [OpCode.RETURN, 1]])
        self.assertIn("JUMP", str(caught.exception))

    def test_handler_after_the_pop_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            make([[OpCode.PUSH_TRY, "handler"],
                  [OpCode.CONST, 1],
                  [OpCode.POP_TRY, 0],
                  [OpCode.RETURN, 1],
                  [OpCode.LABEL, "handler"],
                  [OpCode.CONST, 2], [OpCode.RETURN, 1]])
        self.assertIn("JUMP", str(caught.exception))

    def test_handler_inside_its_own_block_builds(self):
        code = make([[OpCode.PUSH_TRY, "handler"],
                     [OpCode.CONST, 1],
                     [OpCode.JUMP, "end"],
                     [OpCode.LABEL, "handler"],
                     [OpCode.CONST, 2],
                     [OpCode.LABEL, "end"],
                     [OpCode.POP_TRY, 0],
                     [OpCode.RETURN, 1]])
        self.assertIsInstance(code, Code)

    def test_handler_at_its_own_pop_builds(self):
        code = make([[OpCode.PUSH_FINALLY, "cleanup"],
                     [OpCode.CONST, 1],
                     [OpCode.LABEL, "cleanup"],
                     [OpCode.POP_FINALLY, 0],
                     [OpCode.RETURN, 1]])
        self.assertIsInstance(code, Code)


class TestMatchCursorMoves(unittest.IsolatedAsyncioTestCase):
    """
    MATCH_SYNC and MATCH_RESET move a cursor to a position over its
    own subject. A hand-built Code can name a cursor or mark over
    another subject, or of another kind; the move is refused rather
    than carrying the cursor's bounds onto a subject they do not fit
    """

    def spec(self):
        return NamespaceSpec(None, (), ())

    async def run_code(self, instrs):
        spec = self.spec()
        code = Code(spec, instrs, "t")
        return await Expression(code, spec.get_namespace(None))()

    def two_cursors(self, first, second):
        return [[OpCode.CONST, first],
                [OpCode.MATCH_DESCEND, "miss"],
                [OpCode.CONST, second],
                [OpCode.MATCH_DESCEND, "miss"]]

    def walk(self, steps):
        body = []
        for _ in range(steps):
            body += [[OpCode.MATCH_NEXT, ("miss", 0, 0, 0)], [OpCode.POP, 1]]
        return body + [[OpCode.MATCH_NEXT, ("miss", 0, 0, 0)],
                       [OpCode.RETURN, 1],
                       [OpCode.LABEL, "miss"],
                       [OpCode.CONST, "missed"], [OpCode.RETURN, 1]]

    async def test_sync_onto_another_subject_is_refused(self):
        instrs = (self.two_cursors(tuple(range(1000)), (7,))
                  + [[OpCode.MATCH_SYNC, (1, 0, 0, 0)], [OpCode.POP, 1]]
                  + self.walk(200))
        with self.assertRaises(SystemError):
            await self.run_code(instrs)

    async def test_reset_onto_a_mark_over_another_subject_is_refused(self):
        instrs = (self.two_cursors(tuple(range(1000)), (7,))
                  + [[OpCode.MATCH_SCOPE, (0, 0, 0, 0)],
                     [OpCode.MATCH_RESET, (2, 0, 0, 0)],
                     [OpCode.POP, 1], [OpCode.POP, 1]]
                  + self.walk(200))
        with self.assertRaises(SystemError):
            await self.run_code(instrs)

    async def test_sync_onto_a_cursor_of_another_kind_is_refused(self):
        instrs = (self.two_cursors((1, 2, 3), cons(1, 2, 3, nil))
                  + [[OpCode.MATCH_SYNC, (1, 0, 0, 0)], [OpCode.POP, 1]]
                  + self.walk(1))
        with self.assertRaises(SystemError):
            await self.run_code(instrs)

    async def test_sync_and_reset_over_one_subject_still_move(self):
        subject = (10, 20, 30)
        instrs = ([[OpCode.CONST, subject],
                   [OpCode.MATCH_DESCEND, "miss"],
                   [OpCode.MATCH_NEXT, ("miss", 0, 0, 0)], [OpCode.POP, 1],
                   [OpCode.MATCH_SCOPE, (0, 0, 0, 0)],
                   [OpCode.MATCH_NEXT, ("miss", 1, 0, 0)], [OpCode.POP, 1],
                   [OpCode.MATCH_RESET, (1, 0, 0, 0)],
                   [OpCode.POP, 1]]
                  + self.walk(0))
        self.assertEqual(20, await self.run_code(instrs))


# The end.
