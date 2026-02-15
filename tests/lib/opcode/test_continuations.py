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
Tests for delimited and undelimited continuations (PUSH_PROMPT, SHIFT,
INVOKE_K, POP_PROMPT, PUSH_CC, CAPTURE_CC, POP_CC) and evaluator token.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr


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

    def test_invoke_k_var_lookup_and_invoke(self):
        """
        INVOKE_K_VAR: handler stores cont in ns, then INVOKE_K_VAR name
        invokes it with value. (+ 1 (shift k (do (var-store k k) (invoke-k-var k 5)))) => 6
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
            ["VAR_STORE", "k"],
            ["CONST", 5],
            ["INVOKE_K_VAR", "k"],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertEqual(result, 6)

    def test_invoke_k_with_non_continuation_raises_type_error(self):
        """
        INVOKE_K expects DelimitedContinuation at TOS-1; int => TypeError
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["SET_PROMPT_HANDLER", "handler"],
            ["SHIFT", 0],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
            ["NOOP", 0],
            ["LABEL", "handler"],
            ["POP", 1],
            ["CONST", 1],
            ["CONST", 2],
            ["INVOKE_K", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        with self.assertRaises(TypeError) as ctx:
            asyncio.run(expr())
        self.assertIn("DelimitedContinuation", str(ctx.exception))

    def test_invoke_delimited_continuation_after_pop_prompt_raises_continuation_error(self):
        """
        After POP_PROMPT the prompt is gone; invoking the captured delimited
        continuation (via CALL) raises KoskariContinuationError (prompt not
        found in dynamic chain).
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
    """

    def test_call_cc_capture_and_invoke(self):
        """
        (call/cc (lambda (k) (k 42))) => 42.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CONST", 42],
            ["CALL", 1],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertEqual(result, 42)

    def test_call_cc_invoke_with_zero_args_resumes_with_none(self):
        """
        (call/cc (lambda (k) (k))) => None.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = asyncio.run(expr())
        self.assertIsNone(result)

    def test_call_cc_capture_returns_continuation(self):
        """
        PUSH_CC, CAPTURE_CC, POP_CC, return TOS. Result is the continuation.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
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
        KoskariContinuationApply.
        """
        import koskari.lib as lib
        self.assertTrue(hasattr(lib, "KoskariContinuationApply"))

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

        bottom_code, bottom_ns = build_expr([
            ["VAR", "bridge"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
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


# The end.
