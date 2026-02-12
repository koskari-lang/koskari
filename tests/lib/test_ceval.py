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
including delimited continuations (PUSH_PROMPT, SHIFT, CALL for invocation)
and stub opcodes (RuntimeError).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari import Code, FormalsSpec, Function, NamespaceSpec, OpCode
from koskari.lib import AsyncGenerator, FunctionSpec
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
    Tests for PUSH_PROMPT, SET_PROMPT_HANDLER, SHIFT, CALL (continuation), POP_PROMPT
    """

    def test_reset_shift_invoke(self):
        """
        Program (+ 1 (shift k (k 5))) => 6. Validates PUSH_PROMPT,
        SET_PROMPT_HANDLER, SHIFT, INVOKE_K for continuation invocation, POP_PROMPT
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

    def test_delimited_continuation_cannot_be_called_from_python(self):
        """
        (reset (shift k k)) returns k; calling k from Python raises TypeError
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["SET_PROMPT_HANDLER", "handler"],
            ["SHIFT", 0],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "handler"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        cont = asyncio.run(expr())
        self.assertIsNotNone(cont)
        with self.assertRaises(TypeError) as ctx:
            cont(42)
        self.assertIn("cannot be called from Python", str(ctx.exception))

    def test_invoke_delimited_continuation_after_pop_prompt_raises_continuation_error(self):
        """
        After POP_PROMPT the prompt is gone; invoking the captured delimited
        continuation (via CALL) raises KoskariContinuationError (prompt not
        found in dynamic chain). Handler returns the continuation so main has
        it on stack; main then POP_PROMPT, pushes 42, CALL 1.
        """
        import koskari.lib as lib
        code = [
            ["PUSH_PROMPT", 0],
            ["SET_PROMPT_HANDLER", "handler"],
            ["SHIFT", 0],
            ["POP_PROMPT", 0],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "handler"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        with self.assertRaises(lib.KoskariContinuationError) as ctx:
            asyncio.run(expr())
        self.assertIn("prompt not found", str(ctx.exception))


class TestUndelimitedContinuations(CevalTest):
    """
    Tests for PUSH_CC, POP_CC, CAPTURE_CC (undelimited call/cc block).
    Continuation is invoked via CALL (same evaluator).
    """

    def test_call_cc_capture_and_invoke(self):
        """
        (call/cc (lambda (k) (k 42))) => 42. PUSH_CC, CAPTURE_CC, POP_CC,
        then invoke continuation with 42 via CALL 1; resume at label, RETURN 1.
        """
        code = [
            ["PUSH_CC", "resume"],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["CONST", 42],
            ["CALL", 1],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertEqual(result, 42)

    def test_call_cc_invoke_with_zero_args_resumes_with_none(self):
        """
        (call/cc (lambda (k) (k))) => None. Invoke continuation with CALL 0
        (no args); resume_val is Py_None, RETURN 1 yields None.
        """
        code = [
            ["PUSH_CC", "resume"],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["CALL", 0],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertIsNone(result)

    def test_call_cc_capture_returns_continuation(self):
        """
        PUSH_CC, CAPTURE_CC, POP_CC, return TOS. Result is the continuation
        (without invoking it).
        """
        code = [
            ["PUSH_CC", "resume"],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertIsNotNone(result)
        import koskari.lib as lib
        self.assertIsInstance(result, lib.Continuation)

    def test_capture_cc_without_push_cc_raises(self):
        """
        CAPTURE_CC with no PUSH_CC on the dynamic chain raises RuntimeError.
        """
        code = [
            ["CAPTURE_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        with self.assertRaises(RuntimeError) as ctx:
            asyncio.run(expr())
        self.assertIn("CAPTURE_CC", str(ctx.exception))
        self.assertIn("no CC block", str(ctx.exception))

    def test_call_continuation_from_different_token_raises_continuation_apply(self):
        """
        Invoking a continuation from a different evaluator token raises
        KoskariContinuationApply. Bottom Koskari captures a continuation and
        calls a Python async bridge; the bridge invokes a top Koskari function
        (new token) which calls the continuation with a constant, triggering
        the exception boundary.
        """
        import koskari.lib as lib
        self.assertTrue(hasattr(lib, "KoskariContinuationApply"))

        # Top Koskari function: one formal k (the continuation). Body: invoke k
        # with 42. Runs in a different evaluator run (new token) when called
        # from Python.
        top_code = build_code([
            ["VAR", "k"],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
        ], local_syms=("k",), name="top")
        top_ns = NamespaceSpec(None, ("k",), ()).get_namespace(None)
        top_fn = make_callable(top_code, top_ns, FormalsSpec(("k",)))

        async def bridge(cont):
            return await top_fn(cont)

        # Bottom Koskari: push bridge (callable), capture continuation (arg),
        # then call bridge(cont). Stack for CALL 1 must be [callable, arg].
        bottom_code, bottom_ns = build_expr([
            ["VAR", "bridge"],
            ["PUSH_CC", "resume"],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["CALL", 1],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ], values={"bridge": bridge})
        bottom_expr = make_callable(bottom_code, bottom_ns)

        with self.assertRaises(lib.KoskariContinuationApply):
            asyncio.run(bottom_expr())


class TestEvaluatorToken(CevalTest):
    """
    Tests for KosEvaluatorToken and exception types from evaluator-token model
    """

    def test_evaluator_token_and_exceptions_exist_on_module(self):
        """
        koskari.lib exposes EvaluatorToken, DelimitedContinuation,
        Continuation, and exception types
        """
        import koskari.lib as lib
        self.assertTrue(hasattr(lib, "EvaluatorToken"))
        self.assertTrue(hasattr(lib, "DelimitedContinuation"))
        self.assertTrue(hasattr(lib, "Continuation"))
        self.assertTrue(hasattr(lib, "KoskariContinuationDead"))
        self.assertTrue(hasattr(lib, "KoskariContinuationApply"))
        self.assertTrue(hasattr(lib, "KoskariContinuationError"))


class TestStubOpcodes(CevalTest):
    """
    Tests for opcodes that have no implementation yet.
    Each test runs code that hits a stub opcode and expects RuntimeError.
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


    def test_theend_raises_unimpl(self):
        """
        THEEND has no runtime implementation; raises RuntimeError with fixed message
        """

        with self.assertRaises(RuntimeError) as ctx:
            self._run_stub_opcode("THEEND", 0)
        self.assertIn("THEEND", str(ctx.exception))
        self.assertIn("no runtime implementation", str(ctx.exception))


    def test_yield_from_raises_not_implemented(self):
        """
        YIELD_FROM raises RuntimeError when the async gen is driven
        """

        code_obj, ns = build_expr([
            ["YIELD_FROM", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            await anext(agen)

        with self.assertRaises(RuntimeError):
            asyncio.run(run())


    def test_build_args_raises_not_implemented(self):
        """
        BUILD_ARGS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("BUILD_ARGS", 0)


    def test_build_function_stack_underrun_raises(self):
        """
        BUILD_FUNCTION with no fspec on stack raises (stack underrun)
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


    def test_push_ns_raises_not_implemented(self):
        """
        PUSH_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("PUSH_NS", 0)


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


    def test_pop_ns_raises_not_implemented(self):
        """
        POP_NS raises RuntimeError
        """

        with self.assertRaises(RuntimeError):
            self._run_stub_opcode("POP_NS", 0)


class TestBuildFunction(CevalTest):
    """
    Tests for BUILD_FUNCTION: CONST (FunctionSpec), BUILD_FUNCTION N, RETURN 1.
    """

    def test_build_function_no_defaults_returns_function(self):
        """
        CONST fspec, BUILD_FUNCTION 0, RETURN 1 yields a Function
        """

        inner_code = build_code([["RETURN", 0]], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code_obj, ns)())
        self.assertIsInstance(result, Function)

    def test_build_function_returned_function_is_callable(self):
        """
        Function built via BUILD_FUNCTION is callable and returns a coroutine
        """

        inner_code = build_code([
            ["CONST", 99],
            ["RETURN", 1],
        ], name="inner")
        fspec = inner_code.function_spec(FormalsSpec())
        code_obj, ns = build_expr([
            ["CONST", fspec],
            ["BUILD_FUNCTION", 0],
            ["RETURN", 1],
        ])
        fn = run_to_completion(make_callable(code_obj, ns)())
        self.assertIsInstance(fn, Function)
        coro = fn()
        self.assertEqual(run_to_completion(coro), 99)


class TestNoopLabelPositionOpcodes(CevalTest):
    """
    Tests for NOOP, LABEL, POSITION (no-ops / compile-time only at runtime).
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


class TestStackOpcodes(CevalTest):
    """
    Tests for DUP, POP
    """

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


class TestJumpOpcodes(CevalTest):
    """
    Tests for JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
    """

    def test_jump_unconditional(self):
        """
        JUMP branches to label; skips instructions in between
        """
        code, ns = build_expr([
            ["JUMP", "target"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "target"],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    def test_jump_false_jumps_when_falsy(self):
        """
        JUMP_FALSE jumps to label when TOS is false
        """
        code, ns = build_expr([
            ["CONST", False],
            ["JUMP_FALSE", "yes"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "yes"],
            ["CONST", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_jump_false_continues_when_truthy(self):
        """
        JUMP_FALSE does not jump when TOS is true
        """
        code, ns = build_expr([
            ["CONST", True],
            ["JUMP_FALSE", "skip"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "skip"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_jump_true_jumps_when_truthy(self):
        """
        JUMP_TRUE jumps to label when TOS is true
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["JUMP_TRUE", "yes"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "yes"],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    def test_jump_true_continues_when_falsy(self):
        """
        JUMP_TRUE does not jump when TOS is false
        """
        code, ns = build_expr([
            ["CONST", None],
            ["JUMP_TRUE", "skip"],
            ["CONST", 7],
            ["RETURN", 1],
            ["LABEL", "skip"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 7)


    def test_jump_and_short_circuit(self):
        """
        JUMP_AND leaves value and jumps when falsy; pops when truthy
        """
        code, ns = build_expr([
            ["CONST", 0],
            ["JUMP_AND", "end"],
            ["CONST", 100],
            ["JUMP_AND", "end"],
            ["CONST", 5],
            ["LABEL", "end"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_jump_or_short_circuit(self):
        """
        JUMP_OR leaves value and jumps when truthy; pops when falsy
        """
        code, ns = build_expr([
            ["CONST", 0],
            ["JUMP_OR", "next"],
            ["CONST", 0],
            ["JUMP_OR", "next"],
            ["CONST", 42],
            ["LABEL", "next"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestCheckOpcodes(CevalTest):
    """
    Tests for CHECK_EQ, CHECK_NE, CHECK_IN, CHECK_NOT_IN, CHECK_IS,
    CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS
    """

    def test_check_eq_jumps_on_mismatch(self):
        """
        CHECK_EQ jumps when left != right
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 2],
            ["CHECK_EQ", "fail"],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_eq_continues_on_match(self):
        """
        CHECK_EQ does not jump when left == right
        """
        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 10],
            ["CHECK_EQ", "fail"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "fail"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_check_in_jumps_when_not_contained(self):
        """
        CHECK_IN jumps when value not in container
        """
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["CONST", 99],
            ["CHECK_IN", "miss"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_in_continues_when_contained(self):
        """
        CHECK_IN does not jump when value in container
        """
        code, ns = build_expr([
            ["CONST", [1, 2, 3]],
            ["CONST", 2],
            ["CHECK_IN", "miss"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "miss"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_check_is_jumps_when_different_identity(self):
        """
        CHECK_IS jumps when left is not right
        """
        code, ns = build_expr([
            ["CONST", []],
            ["CONST", []],
            ["CHECK_IS", "diff"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_is_continues_when_same_identity(self):
        """
        CHECK_IS does not jump when left is right
        """
        code, ns = build_expr([
            ["CONST", None],
            ["CONST", None],
            ["CHECK_IS", "diff"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "diff"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_check_is_instance_jumps_when_false(self):
        """
        CHECK_IS_INSTANCE jumps when isinstance(value, cls) is False
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", str],
            ["CHECK_IS_INSTANCE", "no"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    def test_check_is_instance_continues_when_true(self):
        """
        CHECK_IS_INSTANCE does not jump when isinstance(value, cls) is True
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", int],
            ["CHECK_IS_INSTANCE", "no"],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "no"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


class TestNextOpcodes(CevalTest):
    """
    Tests for NEXT (stack-based).
    """

    def test_next_1_arg_pushes_value(self):
        """
        NEXT 1 on iterator pushes first value
        """
        code, ns = build_expr([
            ["CONST", [10]],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 10)


    def test_next_1_arg_stopiteration_raises(self):
        """
        NEXT 1 on exhausted iterator raises StopIteration (or wraps it)
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["NEXT", 1],
            ["RETURN", 1],
        ])
        coro = make_callable(code, ns)()
        with self.assertRaises((StopIteration, RuntimeError)):
            while True:
                coro.send(None)


    def test_next_2_arg_pushes_value(self):
        """
        NEXT 2 on iterator pushes value; stack is [iterator, default]
        """
        code, ns = build_expr([
            ["CONST", [7]],
            ["OBJECT_ITER", 0],
            ["CONST", 0],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 7)


    def test_next_2_arg_exhausted_returns_default(self):
        """
        NEXT 2 on exhausted iterator pushes default
        """
        code, ns = build_expr([
            ["CONST", []],
            ["OBJECT_ITER", 0],
            ["CONST", 42],
            ["NEXT", 2],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestYieldOpcodes(CevalTest):
    """
    Tests for YIELD and for NEXT/ITER_NEXT over async generators.
    """

    def test_yielding_expression_returns_async_generator(self):
        """
        Calling an Expression whose code has YIELD returns AsyncGenerator
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(gen_code, gen_ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

    def test_yield_suspends_and_resumes(self):
        """
        Yielding code returns AsyncGenerator; anext() yields value then StopAsyncIteration
        """

        code = [
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)

        async def run():
            v = await anext(agen)
            self.assertEqual(v, 42)
            with self.assertRaises(StopAsyncIteration):
                await anext(agen)

        asyncio.run(run())

    def test_next_accumulates_values_from_koskari_async_gen(self):
        """
        NEXT 1 on a Koskari async gen yields 1, 2, 3; consumer accumulates and returns 3
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v1"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v2"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v3"],
            ["VAR", "v3"],
            ["RETURN", 1],
        ], values={"gen": gen, "v1": None, "v2": None, "v3": None})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 3)

    def test_next_with_await_in_generator(self):
        """
        Koskari async gen that AWAITs a Python async noop between yields; consumer still gets 1,2,3
        """
        async def noop():
            return None

        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["VAR", "noop"],
            ["CALL", 0],
            ["AWAIT", 0],
            ["POP", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ], values={"noop": noop})
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v1"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v2"],
            ["VAR", "gen"],
            ["NEXT", 1],
            ["VAR_STORE", "v3"],
            ["VAR", "v3"],
            ["RETURN", 1],
        ], values={"gen": gen, "v1": None, "v2": None, "v3": None})
        result = asyncio.run(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 3)

    def test_foreach_iter_next_over_koskari_async_gen(self):
        """
        PUSH_FOREACH over a Koskari async gen; ITER_NEXT accumulates; last value is 3
        """
        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        gen = make_callable(gen_code, gen_ns)()

        consumer_code, consumer_ns = build_expr([
            ["CONST", None],
            ["VAR", "gen"],
            ["PUSH_FOREACH", "break_lbl"],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["VAR_STORE", "last"],
            ["VAR", "count"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "count"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["POP", 1],
            ["VAR", "last"],
            ["RETURN", 1],
        ], values={"gen": gen, "count": 0, "last": None})
        result = run_to_completion(make_callable(consumer_code, consumer_ns)())
        self.assertEqual(result, 3)


class TestLoopOpcodes(CevalTest):
    """
    Tests for PUSH_LOOP, POP_LOOP, CLEAR_LOOP, BREAK, CONTINUE.

    PUSH_LOOP initializes region->result to None.
    BREAK/CONTINUE update region->result with their arg-derived value.
    POP_LOOP pushes region->result to the stack as the loop expression value.
    """

    def test_simple_loop_break(self):
        """
        PUSH_LOOP, body, BREAK, POP_LOOP returns value after loop.
        POP_LOOP pushes the loop result (None from BREAK 0).
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "after_pop"],
            ["CONST", 99],
            ["BREAK", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_pop"],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_push_loop_with_pop_loop_completes(self):
        """
        PUSH_LOOP with matching POP_LOOP completes normally.
        POP_LOOP pushes the loop result (None) to the stack.
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "break_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_loop_continue(self):
        """
        PUSH_LOOP with CONTINUE jumps back to loop head
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "n"],
            ["PUSH_LOOP", "break_lbl"],
            ["VAR", "n"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "n"],
            ["VAR", "n"],
            ["CONST", 3],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "break_lbl"],
            ["CONTINUE", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed loop result (None)
            ["POP", 1],
            ["VAR", "n"],
            ["RETURN", 1],
        ], values={"n": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 3)


    def test_nested_loops_break_inner(self):
        """
        Nested loops: inner BREAK exits inner loop only
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "outer"],
            ["CONST", 0],
            ["VAR_BIND", "inner"],
            ["PUSH_LOOP", "outer_end"],
            ["CONST", 0],
            ["VAR_STORE", "inner"],
            ["PUSH_LOOP", "inner_end"],
            ["VAR", "inner"],
            ["CONST", 2],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "inner_end"],
            ["VAR", "inner"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "inner"],
            ["CONTINUE", 0],
            ["LABEL", "inner_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed inner loop result
            ["POP", 1],
            ["VAR", "outer"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "outer"],
            ["VAR", "outer"],
            ["CONST", 2],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "outer_end"],
            ["CONTINUE", 0],
            ["LABEL", "outer_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed outer loop result
            ["POP", 1],
            ["VAR", "outer"],
            ["RETURN", 1],
        ], values={"outer": 0, "inner": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 2)


    def test_nested_loops_break_outer(self):
        """
        Nested loops: inner BREAK targets outer exit, exiting both loops
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "x"],
            ["PUSH_LOOP", "outer_end"],
            ["PUSH_LOOP", "outer_end"],
            ["CONST", 1],
            ["VAR_STORE", "x"],
            ["BREAK", 0],
            ["LABEL", "inner_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed inner loop result
            ["POP", 1],
            ["LABEL", "outer_end"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed outer loop result
            ["POP", 1],
            ["VAR", "x"],
            ["RETURN", 1],
        ], values={"x": 0})
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    def test_break_outside_loop_raises(self):
        """
        BREAK without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["BREAK", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            run_to_completion(make_callable(code_obj, ns)())
        self.assertIn("break outside loop", str(ctx.exception))


    def test_continue_outside_loop_raises(self):
        """
        CONTINUE without LOOP/FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["CONTINUE", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError) as ctx:
            run_to_completion(make_callable(code_obj, ns)())
        self.assertIn("continue outside loop", str(ctx.exception))


    def test_clear_loop_outside_loop_raises(self):
        """
        CLEAR_LOOP without preceding PUSH_LOOP/PUSH_FOREACH raises RuntimeError
        """

        code_obj, ns = build_expr([["CLEAR_LOOP", 0], ["RETURN", 1]])
        with self.assertRaises(RuntimeError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_pop_loop_without_push_loop_raises(self):
        """
        POP_LOOP without preceding PUSH_LOOP/PUSH_FOREACH raises RuntimeError
        (at code build or run).
        """

        with self.assertRaises(RuntimeError):
            code_obj, ns = build_expr([["POP_LOOP", 0], ["RETURN", 1]])
            run_to_completion(make_callable(code_obj, ns)())


class TestForeachOpcodes(CevalTest):
    """
    Tests for PUSH_FOREACH with ITER_NEXT.

    PUSH_FOREACH expects stack: [loop_result, iterable].
    It pops both, creates an iterator from the iterable, and stores
    loop_result and iterator in the region. ITER_NEXT reads from region.
    POP_LOOP pushes region->result to the stack as the loop expression value.
    """

    def test_iter_next_outside_foreach_fails(self):
        """
        ITER_NEXT outside PUSH_FOREACH fails at Code construction or runtime
        """
        try:
            code, ns = build_expr([
                ["CONST", [10]],
                ["ITER_NEXT", "else_lbl"],
                ["RETURN", 1],
                ["LABEL", "else_lbl"],
                ["CONST", 0],
                ["RETURN", 1],
            ])
        except RuntimeError as e:
            self.assertIn("ITER_NEXT", str(e))
            self.assertIn("PUSH_FOREACH", str(e))
            return
        with self.assertRaises(RuntimeError) as cm:
            run_to_completion(make_callable(code, ns)())
        self.assertIn("ITER_NEXT", str(cm.exception))
        self.assertIn("PUSH_FOREACH", str(cm.exception))


    def test_foreach_iterates_list(self):
        """
        FOREACH over list returns first element
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", [10, 20, 30]],  # iterable
            ["PUSH_FOREACH", "after_pop"],
            ["ITER_NEXT", "else_lbl"],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_pop"],
            # POP_LOOP pushed the loop result; we want to return -1
            ["POP", 1],
            ["CONST", -1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 10)


    def test_foreach_break_exits_early(self):
        """
        FOREACH BREAK exits when value equals 2. BREAK with arg=0 uses None
        as the result value.
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", [1, 2, 3, 4]],  # iterable
            ["PUSH_FOREACH", "after_pop"],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", 2],
            ["OBJECT_EQ", 2],
            ["JUMP_FALSE", "loop_cont"],
            ["BREAK", 0],
            ["LABEL", "loop_cont"],
            ["JUMP", "loop_head"],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_pop"],
            # POP_LOOP pushed the loop result (None from break)
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_foreach_empty_exits_to_else(self):
        """
        FOREACH over empty list exits to else target
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", "after_pop"],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_pop"],
            # POP_LOOP pushed the loop result (None)
            ["POP", 1],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    def test_push_foreach_with_iter_next_completes(self):
        """
        PUSH_FOREACH with ITER_NEXT on empty list exits to else.
        POP_LOOP pushes the loop result (None in this case).
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", []],    # iterable
            ["PUSH_FOREACH", "break_lbl"],
            ["ITER_NEXT", "else_lbl"],
            ["CONST", -1],
            ["RETURN", 1],
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP now pushes the loop result (None)
            ["POP", 1],  # discard the loop result
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestLoopResultOpcodes(CevalTest):
    """
    Tests for break/continue result values and loop result semantics.

    BREAK N and CONTINUE N pop N values from the stack:
    - N=0: result is None
    - N=1: result is the single popped value
    - N>1: result is a tuple of the N popped values

    BREAK pushes the result and jumps to break target.
    CONTINUE updates region->result and jumps to continue target.
    POP_LOOP pushes region->result as the loop expression value.
    """

    def test_break_with_single_value(self):
        """
        BREAK 1 pops one value and uses it as the loop result
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "after_loop"],
            ["CONST", 42],
            ["BREAK", 1],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK pushed the result (42)
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    def test_break_with_multiple_values_creates_tuple(self):
        """
        BREAK 3 pops three values and creates a tuple result
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "after_loop"],
            ["CONST", 1],
            ["CONST", 2],
            ["CONST", 3],
            ["BREAK", 3],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK pushed the result (1, 2, 3)
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, (1, 2, 3))


    def test_continue_with_single_value(self):
        """
        CONTINUE 1 pops one value and updates region->result
        """

        code, ns = build_expr([
            ["CONST", 0],
            ["VAR_BIND", "n"],
            ["PUSH_LOOP", "break_lbl"],
            ["VAR", "n"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["DUP", 1],
            ["VAR_STORE", "n"],
            # Stack now has n on top; use it as continue result
            ["VAR", "n"],
            ["CONST", 3],
            ["OBJECT_GE", 2],
            ["JUMP_TRUE", "break_lbl"],
            ["CONTINUE", 1],  # continue with n as result
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed the last continue result
            ["RETURN", 1],
        ], values={"n": 0})
        result = run_to_completion(make_callable(code, ns)())
        # Loop runs 3 times: n=1, n=2, n=3; last continue was with n=2
        self.assertEqual(result, 2)


    def test_pop_loop_returns_loop_result(self):
        """
        POP_LOOP pushes region->result (None when no break/continue)
        """

        code, ns = build_expr([
            ["PUSH_LOOP", "break_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            # POP_LOOP pushed None (initial result)
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsNone(result)


    def test_foreach_break_with_value(self):
        """
        FOREACH BREAK 1 uses the value as the loop result
        """

        code, ns = build_expr([
            ["CONST", None],  # loop_result
            ["CONST", [10, 20, 30]],  # iterable
            ["PUSH_FOREACH", "after_loop"],
            ["LABEL", "loop_head"],
            ["ITER_NEXT", "else_lbl"],
            # Stack: [iter_value]
            ["DUP", 1],
            ["CONST", 20],
            ["OBJECT_EQ", 2],
            # Stack: [iter_value, is_20]
            ["JUMP_FALSE", "loop_cont"],
            # If iter_value == 20, break with 99
            ["POP", 1],  # discard iter_value
            ["CONST", 99],
            ["BREAK", 1],  # break with value 99
            ["LABEL", "loop_cont"],
            # Stack: [iter_value]
            ["POP", 1],  # discard the iteration value
            ["CONTINUE", 0],  # continue to next iteration
            ["LABEL", "else_lbl"],
            ["CLEAR_LOOP", 0],
            ["LABEL", "break_lbl"],
            ["POP_LOOP", 0],
            ["LABEL", "after_loop"],
            # BREAK or POP_LOOP pushed the result
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


class TestFastOpcodes(CevalTest):
    """
    Tests for FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
    """

    def _run_fast_out_of_bounds(self, opname, arg=0):
        """
        Run bytecode that hits the given FAST opcode with no fast slots
        """

        if opname == "FAST_STORE":
            code_list = [["CONST", 42], [opname, arg], ["RETURN", 1]]
        else:
            code_list = [[opname, arg], ["RETURN", 1]]
        code_obj, ns = build_expr(code_list)
        expr = make_callable(code_obj, ns)
        return run_to_completion(expr())


    def test_fast_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            self._run_fast_out_of_bounds("FAST", 0)


    def test_fast_bind_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_BIND raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            self._run_fast_out_of_bounds("FAST_BIND", 0)


    def test_fast_clear_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_CLEAR raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            self._run_fast_out_of_bounds("FAST_CLEAR", 0)


    def test_fast_ref_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_REF raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            self._run_fast_out_of_bounds("FAST_REF", 0)


    def test_fast_store_raises_name_error_for_out_of_bounds_index(self):
        """
        FAST_STORE raises NameError when slot index is out of bounds
        """

        with self.assertRaises(NameError):
            self._run_fast_out_of_bounds("FAST_STORE", 0)


    def test_fast_clear_clears_slot(self):
        """
        FAST_CLEAR clears fast slot; subsequent FAST yields empty/ref
        """
        code, ns = build_expr([
            ["CONST", 100],
            ["FAST_BIND", 0],
            ["FAST_CLEAR", 0],
            ["CONST", 42],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestAttrOpcodes(CevalTest):
    """
    Tests for ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
    """

    def test_attr_getattr(self):
        """
        ATTR pushes getattr(TOS, name)
        """
        class Box(object):
            value = 3
        code, ns = build_expr([
            ["CONST", Box],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 3)


    def test_attr_store_and_getattr(self):
        """
        ATTR_STORE sets attribute; ATTR loads it
        """
        class Box(object):
            pass
        obj = Box()
        code, ns = build_expr([
            ["CONST", obj],
            ["CONST", 99],
            ["ATTR_STORE", "value"],
            ["CONST", obj],
            ["ATTR", "value"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)
        self.assertEqual(obj.value, 99)


    def test_attr_clear_delattr(self):
        """
        ATTR_CLEAR deletes attribute
        """
        class Box(object):
            pass
        obj = Box()
        obj.x = 1
        code, ns = build_expr([
            ["CONST", obj],
            ["ATTR_CLEAR", "x"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)
        self.assertFalse(hasattr(obj, "x"))


    def test_attr_ref_pushes_attribute_ref(self):
        """
        ATTR_REF pushes AttributeRef for TOS.attr
        """
        from koskari.lib import AttributeRef
        class Box(object):
            pass
        obj = Box()
        obj.foo = "bar"
        code, ns = build_expr([
            ["CONST", obj],
            ["ATTR_REF", "foo"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, AttributeRef)
        self.assertEqual(result.deref(), "bar")


class TestItemOpcodes(CevalTest):
    """
    Tests for ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE
    """

    def test_item_getitem(self):
        """
        ITEM pushes TOS1[TOS]
        """
        code, ns = build_expr([
            ["CONST", {"a": 1, "b": 2}],
            ["CONST", "b"],
            ["ITEM", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 2)


    def test_item_store_setitem(self):
        """
        ITEM_STORE does TOS2[TOS1] = TOS (val, key, obj popped)
        """
        code, ns = build_expr([
            ["CONST", {}],
            ["DUP", 1],
            ["CONST", "k"],
            ["CONST", "v"],
            ["ITEM_STORE", 0],
            ["CONST", "k"],
            ["ITEM", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, "v")


    def test_item_clear_delitem(self):
        """
        ITEM_CLEAR deletes TOS1[TOS]
        """
        d = {"x": 1}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST", "x"],
            ["ITEM_CLEAR", 0],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)
        self.assertNotIn("x", d)


    def test_item_ref_pushes_item_ref(self):
        """
        ITEM_REF pushes ItemRef for TOS1[TOS]
        """
        from koskari.lib import ItemRef
        d = {"k": "v"}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST", "k"],
            ["ITEM_REF", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, ItemRef)
        self.assertEqual(result.deref(), "v")


class TestConstOpcodes(CevalTest):
    """
    Tests for CONST, CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE
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


    def test_const_item_getitem(self):
        """
        CONST_ITEM pushes TOS[key] with key from constant pool
        """
        code, ns = build_expr([
            ["CONST", {"a": 10, "b": 20}],
            ["CONST_ITEM", "b"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 20)


    def test_const_item_store_setitem(self):
        """
        CONST_ITEM_STORE does TOS1[key] = TOS; key from constant pool
        """
        code, ns = build_expr([
            ["CONST", {}],
            ["DUP", 1],
            ["CONST", "x"],
            ["CONST_ITEM_STORE", "key"],
            ["CONST_ITEM", "key"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, "x")


    def test_const_item_clear_delitem(self):
        """
        CONST_ITEM_CLEAR deletes TOS[key]
        """
        d = {"z": 1}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST_ITEM_CLEAR", "z"],
            ["CONST", 42],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)
        self.assertNotIn("z", d)


    def test_const_item_ref_pushes_item_ref(self):
        """
        CONST_ITEM_REF pushes ItemRef for TOS[key]
        """
        from koskari.lib import ItemRef
        d = {"k": "v"}
        code, ns = build_expr([
            ["CONST", d],
            ["CONST_ITEM_REF", "k"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertIsInstance(result, ItemRef)
        self.assertEqual(result.deref(), "v")


class TestControlOpcodes(CevalTest):
    """
    Tests for RETURN, AWAIT.
    """

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


    def test_var_clear_raises_name_error_for_missing_name(self):
        """
        VAR_CLEAR raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_CLEAR", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_var_ref_raises_name_error_for_missing_name(self):
        """
        VAR_REF raises NameError when name is not defined
        """

        code_obj, ns = build_expr([["VAR_REF", "x"], ["RETURN", 1]])
        with self.assertRaises(NameError):
            run_to_completion(make_callable(code_obj, ns)())


    def test_var_store_with_empty_stack_raises(self):
        """
        VAR_STORE with empty stack raises (stack underrun or RuntimeError)
        """

        code_obj, ns = build_expr([["VAR_STORE", "x"], ["RETURN", 1]])
        with self.assertRaises(RuntimeError):
            run_to_completion(make_callable(code_obj, ns)())


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

    def test_call_invokes_callable_one_arg(self):
        """
        CALL 1 invokes callable with one argument (vectorcall path)
        """
        def double(x):
            return x * 2
        code, ns = build_expr([
            ["VAR", "f"],
            ["CONST", 21],
            ["CALL", 1],
            ["RETURN", 1],
        ], values={"f": double})
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
