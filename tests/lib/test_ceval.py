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
Opcode-focused tests for Koskari evaluator.

Validates individual bytecode operations and their impact on execution,
including delimited continuations (PUSH_PROMPT, SHIFT, INVOKE_K, etc.)
and stub opcodes (RuntimeError).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari import Code, NamespaceSpec, OpCode
from tests import make_callable


def _op(name):
    """
    Map string opcode name to OpCode enum (e.g. CONST -> KOS_OP_CONST)
    """

    return getattr(OpCode, "KOS_OP_" + name)


def build_code(code, local_syms=None, name="expr"):
    """
    Build Code from a list of [opname, arg] with string opcode names

    :param code: Sequence of [op, arg] (e.g. [["CONST", 42], ["RETURN", 1]])
    :param local_syms: Optional sequence of local names for the namespace spec
    :param name: Code name for debugging
    :return: Code instance
    """

    if local_syms is None:
        local_syms = ()
    elif not isinstance(local_syms, (tuple, list)):
        local_syms = (local_syms,)
    nspec = NamespaceSpec(None, tuple(local_syms), ())
    instr = tuple([_op(op), arg] for op, arg in code)
    return Code(nspec, instr, name)


def build_expr(code, local_syms=None, values=None, name="expr"):
    """
    Build Code and namespace; optionally bind names (for VAR) via values

    :param code: Sequence of [op, arg] as in build_code
    :param local_syms: Optional sequence of local names
    :param values: Optional dict of name -> value to store in the namespace
    :param name: Code name
    :return: (code, ns) for use with make_callable(code, ns)()
    """

    if values:
        syms = set(local_syms or ()) | set(values.keys())
        local_syms = tuple(syms)
    c = build_code(code, local_syms=local_syms, name=name)
    nspec = NamespaceSpec(None, tuple(local_syms or ()), ())
    ns = nspec.get_namespace(None)
    if values:
        for k, v in values.items():
            ns.store(k, v)
    return (c, ns)


def run_to_completion(coro):
    """
    Drive the coroutine until StopIteration; return the yielded value

    :param coro: Coroutine from make_callable(code, ns)() or similar
    :return: The value passed to StopIteration
    """

    try:
        while True:
            coro.send(None)
    except StopIteration as e:
        return e.value


class CevalTest(unittest.TestCase):
    """
    Base class with assertion helpers for opcode tests
    """

    def assertRuntimeError(self, *args, **kwds):
        """
        Assert that calling the given expression raises RuntimeError
        """

        self.assertRaises(RuntimeError, *args, **kwds)


    def assertTypeError(self, *args, **kwds):
        """
        Assert that calling the given expression raises TypeError
        """

        self.assertRaises(TypeError, *args, **kwds)


    def assertStubRaises(self, *args, **kwds):
        """
        Assert stub opcode raises RuntimeError (op_unimplemented_stub)
        """

        self.assertRaises(RuntimeError, *args, **kwds)


class TestDelimitedContinuations(CevalTest):
    """
    Tests for PUSH_PROMPT, SET_PROMPT_HANDLER, SHIFT, INVOKE_K, POP_PROMPT
    """

    def test_reset_shift_invoke(self):
        """
        Program (+ 1 (shift k (k 5))) => 6. Validates PUSH_PROMPT,
        SET_PROMPT_HANDLER, SHIFT, INVOKE_K, POP_PROMPT end-to-end
        """

        code = [
            ["PUSH_PROMPT", 0],
            ["SET_PROMPT_HANDLER", "handler"],
            ["CONST", 1],
            ["SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "handler"],
            ["CONST", 5],
            ["INVOKE_K", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertEqual(result, 6)


class TestStubOpcodes(CevalTest):
    """
    Tests that stub opcodes raise RuntimeError when executed
    """

    def _run_stub_opcode(self, opname, arg=0):
        """
        Build and run bytecode that hits the given stub opcode
        """

        code_obj, ns = build_expr([
            [opname, arg],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        return run_to_completion(expr())


    def test_push_class_raises_not_implemented(self):
        """
        PUSH_CLASS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_CLASS", 0)


    def test_push_wind_raises_not_implemented(self):
        """
        PUSH_WIND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_WIND", "after")


    def test_pop_wind_raises_not_implemented(self):
        """
        POP_WIND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_WIND", 0)


    def test_end_wind_raises_not_implemented(self):
        """
        END_WIND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("END_WIND", 0)


    def test_push_region_raises_not_implemented(self):
        """
        PUSH_REGION raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_REGION", "handler")


    def test_pop_region_raises_not_implemented(self):
        """
        POP_REGION raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_REGION", 0)


    def test_theend_raises_not_implemented(self):
        """
        THEEND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("THEEND", 0)


    def test_break_raises_not_implemented(self):
        """
        BREAK raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BREAK", 0)


    def test_continue_raises_not_implemented(self):
        """
        CONTINUE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CONTINUE", 0)


    def test_yield_raises_not_implemented(self):
        """
        YIELD raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("YIELD", 0)


    def test_yield_from_raises_not_implemented(self):
        """
        YIELD_FROM raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("YIELD_FROM", 0)


    def test_iter_next_raises_not_implemented(self):
        """
        ITER_NEXT raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ITER_NEXT", "target")


    def test_await_next_raises_not_implemented(self):
        """
        AWAIT_NEXT raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("AWAIT_NEXT", "target")


    def test_jump_raises_not_implemented(self):
        """
        JUMP raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("JUMP", "target")


    def test_jump_false_raises_not_implemented(self):
        """
        JUMP_FALSE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("JUMP_FALSE", "target")


    def test_jump_true_raises_not_implemented(self):
        """
        JUMP_TRUE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("JUMP_TRUE", "target")


    def test_jump_and_raises_not_implemented(self):
        """
        JUMP_AND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("JUMP_AND", "target")


    def test_jump_or_raises_not_implemented(self):
        """
        JUMP_OR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("JUMP_OR", "target")


    def test_check_eq_raises_not_implemented(self):
        """
        CHECK_EQ raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_EQ", "target")


    def test_check_ge_raises_not_implemented(self):
        """
        CHECK_GE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_GE", "target")


    def test_check_gt_raises_not_implemented(self):
        """
        CHECK_GT raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_GT", "target")


    def test_check_le_raises_not_implemented(self):
        """
        CHECK_LE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_LE", "target")


    def test_check_lt_raises_not_implemented(self):
        """
        CHECK_LT raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_LT", "target")


    def test_check_ne_raises_not_implemented(self):
        """
        CHECK_NE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_NE", "target")


    def test_check_in_raises_not_implemented(self):
        """
        CHECK_IN raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_IN", "target")


    def test_check_not_in_raises_not_implemented(self):
        """
        CHECK_NOT_IN raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_NOT_IN", "target")


    def test_check_is_raises_not_implemented(self):
        """
        CHECK_IS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_IS", "target")


    def test_check_is_not_raises_not_implemented(self):
        """
        CHECK_IS_NOT raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_IS_NOT", "target")


    def test_check_is_instance_raises_not_implemented(self):
        """
        CHECK_IS_INSTANCE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_IS_INSTANCE", "target")


    def test_check_is_subclass_raises_not_implemented(self):
        """
        CHECK_IS_SUBCLASS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CHECK_IS_SUBCLASS", "target")


    def test_var_clear_raises_not_implemented(self):
        """
        VAR_CLEAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("VAR_CLEAR", "x")


    def test_var_ref_raises_not_implemented(self):
        """
        VAR_REF raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("VAR_REF", "x")


    def test_var_store_raises_not_implemented(self):
        """
        VAR_STORE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("VAR_STORE", "x")


    def test_fast_raises_not_implemented(self):
        """
        FAST raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("FAST", 0)


    def test_fast_bind_raises_not_implemented(self):
        """
        FAST_BIND raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("FAST_BIND", 0)


    def test_fast_clear_raises_not_implemented(self):
        """
        FAST_CLEAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("FAST_CLEAR", 0)


    def test_fast_ref_raises_not_implemented(self):
        """
        FAST_REF raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("FAST_REF", 0)


    def test_fast_store_raises_not_implemented(self):
        """
        FAST_STORE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("FAST_STORE", 0)


    def test_attr_raises_not_implemented(self):
        """
        ATTR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ATTR", "x")


    def test_attr_clear_raises_not_implemented(self):
        """
        ATTR_CLEAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ATTR_CLEAR", "x")


    def test_attr_ref_raises_not_implemented(self):
        """
        ATTR_REF raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ATTR_REF", "x")


    def test_attr_store_raises_not_implemented(self):
        """
        ATTR_STORE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ATTR_STORE", "x")


    def test_item_raises_not_implemented(self):
        """
        ITEM raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ITEM", 0)


    def test_item_clear_raises_not_implemented(self):
        """
        ITEM_CLEAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ITEM_CLEAR", 0)


    def test_item_ref_raises_not_implemented(self):
        """
        ITEM_REF raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ITEM_REF", 0)


    def test_item_store_raises_not_implemented(self):
        """
        ITEM_STORE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("ITEM_STORE", 0)


    def test_const_item_raises_not_implemented(self):
        """
        CONST_ITEM raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CONST_ITEM", "key")


    def test_const_item_clear_raises_not_implemented(self):
        """
        CONST_ITEM_CLEAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CONST_ITEM_CLEAR", "key")


    def test_const_item_ref_raises_not_implemented(self):
        """
        CONST_ITEM_REF raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CONST_ITEM_REF", "key")


    def test_const_item_store_raises_not_implemented(self):
        """
        CONST_ITEM_STORE raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CONST_ITEM_STORE", "key")


    def test_build_args_raises_not_implemented(self):
        """
        BUILD_ARGS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BUILD_ARGS", 0)


    def test_build_function_raises_not_implemented(self):
        """
        BUILD_FUNCTION raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BUILD_FUNCTION", 0)


    def test_build_kwds_raises_not_implemented(self):
        """
        BUILD_KWDS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BUILD_KWDS", 0)


    def test_build_kwds_names_raises_not_implemented(self):
        """
        BUILD_KWDS_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BUILD_KWDS_NAMES", "x")


    def test_combine_args_raises_not_implemented(self):
        """
        COMBINE_ARGS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("COMBINE_ARGS", 0)


    def test_combine_kwds_raises_not_implemented(self):
        """
        COMBINE_KWDS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("COMBINE_KWDS", 0)


    def test_call_names_raises_not_implemented(self):
        """
        CALL_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CALL_NAMES", 0)


    def test_call_star_raises_not_implemented(self):
        """
        CALL_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CALL_STAR", 0)


    def test_call_attr_raises_not_implemented(self):
        """
        CALL_ATTR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CALL_ATTR", 0)


    def test_call_attr_names_raises_not_implemented(self):
        """
        CALL_ATTR_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CALL_ATTR_NAMES", 0)


    def test_call_attr_star_raises_not_implemented(self):
        """
        CALL_ATTR_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CALL_ATTR_STAR", 0)


    def test_recur_raises_not_implemented(self):
        """
        RECUR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("RECUR", 0)


    def test_recur_names_raises_not_implemented(self):
        """
        RECUR_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("RECUR_NAMES", 0)


    def test_recur_star_raises_not_implemented(self):
        """
        RECUR_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("RECUR_STAR", 0)


    def test_tail_call_raises_not_implemented(self):
        """
        TAIL_CALL raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_CALL", 0)


    def test_tail_call_names_raises_not_implemented(self):
        """
        TAIL_CALL_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_CALL_NAMES", 0)


    def test_tail_call_star_raises_not_implemented(self):
        """
        TAIL_CALL_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_CALL_STAR", 0)


    def test_tail_recur_raises_not_implemented(self):
        """
        TAIL_RECUR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_RECUR", 0)


    def test_tail_recur_names_raises_not_implemented(self):
        """
        TAIL_RECUR_NAMES raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_RECUR_NAMES", 0)


    def test_tail_recur_star_raises_not_implemented(self):
        """
        TAIL_RECUR_STAR raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("TAIL_RECUR_STAR", 0)


    def test_push_foreach_raises_not_implemented(self):
        """
        PUSH_FOREACH raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_FOREACH", "target")


    def test_push_with_raises_not_implemented(self):
        """
        PUSH_WITH raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_WITH", "target")


    def test_push_begin_raises_not_implemented(self):
        """
        PUSH_BEGIN raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_BEGIN", 0)


    def test_push_loop_raises_not_implemented(self):
        """
        PUSH_LOOP raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_LOOP", "target")


    def test_push_ns_raises_not_implemented(self):
        """
        PUSH_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_NS", 0)


    def test_clear_loop_raises_not_implemented(self):
        """
        CLEAR_LOOP raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("CLEAR_LOOP", 0)


    def test_pop_with_raises_not_implemented(self):
        """
        POP_WITH raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_WITH", 0)


    def test_pop_begin_raises_not_implemented(self):
        """
        POP_BEGIN raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_BEGIN", 0)


    def test_pop_loop_raises_not_implemented(self):
        """
        POP_LOOP raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_LOOP", 0)


    def test_pop_ns_raises_not_implemented(self):
        """
        POP_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_NS", 0)


class TestStackOpcodes(CevalTest):
    """
    Tests for NOOP, LABEL, POSITION, DUP, POP
    """

    def test_noop_advances_pc(self):
        """
        NOOP is a no-op; execution continues to next instruction
        """

        code, ns = build_expr([
            ["NOOP", 0],
            ["CONST", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_label_is_noop_at_runtime(self):
        """
        LABEL is compile-time only; at runtime it advances PC
        """

        code, ns = build_expr([
            ["LABEL", "here"],
            ["CONST", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 2)


    def test_position_is_noop_at_runtime(self):
        """
        POSITION is compile-time only; at runtime it advances PC
        """

        code, ns = build_expr([
            ["POSITION", (0, 0)],
            ["CONST", 3],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 3)


    def test_dup_copies_stack_top(self):
        """
        DUP n duplicates top n values; RETURN 2 returns (a, a)
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["DUP", 1],
            ["RETURN", 2],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, (7, 7))


    def test_pop_discards_stack_values(self):
        """
        POP n discards top n values; remaining value is returned
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 20],
            ["POP", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 10)


class TestConstAndReturn(CevalTest):
    """
    Tests for CONST and RETURN
    """

    def test_const_return_single_value(self):
        """
        CONST pushes value; RETURN 1 returns it
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_return_multiple_values(self):
        """
        RETURN n returns n values as a tuple
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["RETURN", 3],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, (1, 2, 3))


class TestNamespaceAndCall(CevalTest):
    """
    Tests for VAR, GLOBALS, LOCALS, CALL
    """

    def test_var_loads_from_namespace(self):
        """
        VAR name loads value from namespace onto stack
        """

        code, ns = build_expr([
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 99})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    def test_globals_pushes_globals_dict(self):
        """
        GLOBALS pushes the frame globals (namespace) onto stack
        """

        code, ns = build_expr([
            ["GLOBALS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIs(result, ns)


    def test_locals_pushes_locals_dict(self):
        """
        LOCALS pushes the frame locals (namespace) onto stack
        """

        code, ns = build_expr([
            ["LOCALS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIs(result, ns)


    def test_call_invokes_callable_zero_args(self):
        """
        CALL 0 invokes callable on stack with no args
        """

        def forty_two():
            return 42
        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": forty_two})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestExceptionOpcodes(CevalTest):
    """
    Tests for EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH, RAISE,
    PUSH_TRY, POP_TRY, PUSH_FINALLY, POP_FINALLY, CLEAR_TRY, CLEAR_FINALLY,
    CHECK_EXCEPTION
    """

    def test_push_try_pop_try_exception_handler(self):
        """
        PUSH_TRY, RAISE, EXCEPTION, POP, POP_TRY: catch and return 42
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_push_finally_pop_finally_runs_on_raise(self):
        """
        PUSH_FINALLY, RAISE, finally block runs, POP_FINALLY
        """

        exc_inst = ValueError("test")
        code, ns = build_expr([
            ["PUSH_FINALLY", "finally"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "finally"],
            ["CONST", 99],
            ["POP_FINALLY", 0],
            ["RETURN", 1],
            ["NOOP", 0],
        ])
        with self.assertRaises(ValueError):
            run_to_completion(make_callable(code, ns)())


    def test_exception_push_current_exception(self):
        """
        EXCEPTION pushes current exception onto stack; handler pops and returns 42
        """

        exc_inst = RuntimeError("x")
        code, ns = build_expr([
            ["PUSH_TRY", "handler"],
            ["CONST", exc_inst],
            ["RAISE", 1],
            ["LABEL", "handler"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_raise_propagates_exception(self):
        """
        RAISE 1 raises the exception instance on stack
        """

        exc_inst = TypeError("raised")
        code, ns = build_expr([
            ["CONST", exc_inst],
            ["RAISE", 1],
        ])
        with self.assertRaises(TypeError) as ctx:
            run_to_completion(make_callable(code, ns)())
        self.assertIn("raised", str(ctx.exception))


    def test_clear_try_clears_try_region(self):
        """
        CLEAR_TRY marks try region as cleared
        """

        code, ns = build_expr([
            ["PUSH_TRY", "skip"],
            ["CONST", 1],
            ["CLEAR_TRY", 0],
            ["POP_TRY", 0],
            ["LABEL", "skip"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_clear_finally_clears_finally_region(self):
        """
        CLEAR_FINALLY marks finally region as cleared
        """

        code, ns = build_expr([
            ["PUSH_FINALLY", "fin"],
            ["CONST", 42],
            ["CLEAR_FINALLY", 0],
            ["POP_FINALLY", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "fin"],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_exception_clear(self):
        """
        EXCEPTION_CLEAR clears current exception state
        """

        code, ns = build_expr([
            ["PUSH_TRY", "h"],
            ["CONST", ValueError("x")],
            ["RAISE", 1],
            ["LABEL", "h"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["EXCEPTION_CLEAR", 0],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_exception_match(self):
        """
        EXCEPTION_MATCH checks if current exception matches type on stack
        """

        code, ns = build_expr([
            ["PUSH_TRY", "h"],
            ["CONST", ValueError("v")],
            ["RAISE", 1],
            ["LABEL", "h"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", ValueError],
            ["EXCEPTION_MATCH", 1],
            ["POP_TRY", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, True)


    def test_check_exception_jumps_if_no_exception(self):
        """
        CHECK_EXCEPTION jumps to label if no exception set
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CHECK_EXCEPTION", "ok"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "ok"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestAwaitOpcode(CevalTest):
    """
    Tests for AWAIT opcode
    """

    def test_await_suspends_and_resumes_with_value(self):
        """
        AWAIT suspends on awaitable; resumes with result
        """

        async def return_five():
            return 5
        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["AWAIT", 0],
            ["RETURN", 1],
        ], values={"f": return_five})
        result = asyncio.run(make_callable(code, ns)())
        self.assertEqual(result, 5)


class TestObjectUnaryOpcodes(CevalTest):
    """
    Tests for OBJECT_STR, OBJECT_LEN, OBJECT_NOT, OBJECT_TRUTH,
    OBJECT_TYPE, OBJECT_VARS, OBJECT_AITER, OBJECT_REPR, OBJECT_ITER,
    OBJECT_BIT_INVERT, OBJECT_DIR
    """

    def test_object_str(self):
        code, ns = build_expr([
            ["CONST", "hello"],
            ["OBJECT_STR", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), "hello")


    def test_object_len(self):
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["OBJECT_LEN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 3)


    def test_object_not(self):
        code, ns = build_expr([
            ["CONST", False],
            ["OBJECT_NOT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_truth(self):
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_TRUTH", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), False)


    def test_object_type(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_TYPE", 0],
            ["RETURN", 1],
        ])
        self.assertIs(run_to_completion(make_callable(code, ns)()), int)


    def test_object_vars(self):
        class ObjWithDict(object):
            pass
        obj = ObjWithDict()
        obj.x = 1
        code, ns = build_expr([
            ["CONST", obj],
            ["OBJECT_VARS", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, {"x": 1})


    def test_object_aiter(self):
        class AsyncIterable(object):
            def __aiter__(self):
                return self
            def __anext__(self):
                raise StopAsyncIteration
        code, ns = build_expr([
            ["CONST", AsyncIterable()],
            ["OBJECT_AITER", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsNotNone(result)


    def test_object_repr(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_REPR", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), "42")


    def test_object_iter(self):
        code, ns = build_expr([
            ["CONST", [1, 2]],
            ["OBJECT_ITER", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(list(result), [1, 2])


    def test_object_bit_invert(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["OBJECT_BIT_INVERT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), -1)


    def test_object_dir(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["OBJECT_DIR", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, list)
        self.assertIn("real", result)


class TestObjectBinaryOpcodes(CevalTest):
    """
    Tests for OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY,
    OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_BIT_AND, OBJECT_BIT_OR,
    OBJECT_BIT_XOR, OBJECT_MATRIX_MULTIPLY, OBJECT_POWER, OBJECT_MODULO,
    OBJECT_DIVMOD, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT
    """

    def test_object_add(self):
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_subtract(self):
        code, ns = build_expr([
            ["CONST", 50],
            ["CONST", 8],
            ["OBJECT_SUBTRACT", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_multiply(self):
        code, ns = build_expr([
            ["CONST", 6],
            ["CONST", 7],
            ["OBJECT_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_floor_divide(self):
        code, ns = build_expr([
            ["CONST", 85],
            ["CONST", 2],
            ["OBJECT_FLOOR_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_true_divide(self):
        code, ns = build_expr([
            ["CONST", 84],
            ["CONST", 2],
            ["OBJECT_TRUE_DIVIDE", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42.0)


    def test_object_bit_and(self):
        code, ns = build_expr([
            ["CONST", 0xFF],
            ["CONST", 0x0F],
            ["OBJECT_BIT_AND", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 15)


    def test_object_bit_or(self):
        code, ns = build_expr([
            ["CONST", 32],
            ["CONST", 10],
            ["OBJECT_BIT_OR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_bit_xor(self):
        code, ns = build_expr([
            ["CONST", 63],
            ["CONST", 21],
            ["OBJECT_BIT_XOR", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_matrix_multiply(self):
        class MatMulResult(object):
            def __matmul__(self, other):
                return 42
        code, ns = build_expr([
            ["CONST", MatMulResult()],
            ["CONST", None],
            ["OBJECT_MATRIX_MULTIPLY", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 42)


    def test_object_power(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 5],
            ["OBJECT_POWER", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)


    def test_object_modulo(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_MODULO", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 2)


    def test_object_divmod(self):
        code, ns = build_expr([
            ["CONST", 17],
            ["CONST", 5],
            ["OBJECT_DIVMOD", 2],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), (3, 2))


    def test_object_bit_left(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 5],
            ["OBJECT_BIT_LEFT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)


    def test_object_bit_right(self):
        code, ns = build_expr([
            ["CONST", 64],
            ["CONST", 1],
            ["OBJECT_BIT_RIGHT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), 32)


class TestObjectComparisonOpcodes(CevalTest):
    """
    Tests for OBJECT_EQ, OBJECT_NE, OBJECT_LT, OBJECT_LE,
    OBJECT_GT, OBJECT_GE, OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS,
    OBJECT_IS_NOT, OBJECT_IS_INSTANCE, OBJECT_IS_SUBCLASS
    """

    def test_object_eq(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", 42],
            ["OBJECT_EQ", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_ne(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_NE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_lt(self):
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["OBJECT_LT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_le(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_LE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_gt(self):
        code, ns = build_expr([
            ["CONST", 3],
            ["CONST", 2],
            ["OBJECT_GT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_ge(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", 2],
            ["OBJECT_GE", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_in(self):
        code, ns = build_expr([
            ["CONST", 2],
            ["CONST", [1, 2, 3]],
            ["OBJECT_IN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_not_in(self):
        code, ns = build_expr([
            ["CONST", 0],
            ["CONST", [1, 2, 3]],
            ["OBJECT_NOT_IN", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is(self):
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["OBJECT_IS", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_not(self):
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["OBJECT_IS_NOT", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_instance(self):
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["OBJECT_IS_INSTANCE", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


    def test_object_is_subclass(self):
        code, ns = build_expr([
            ["CONST", bool],
            ["CONST", int],
            ["OBJECT_IS_SUBCLASS", 1],
            ["RETURN", 1],
        ])
        self.assertEqual(run_to_completion(make_callable(code, ns)()), True)


# The end.
