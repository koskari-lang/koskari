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
Includes cross-context continuation pass-through tests verifying the
segmented transfer path (foreign-token KoskariContinuationApply handling).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""

import unittest

from koskari.lib import Code, FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, _op, build_code, build_expr


class TestDelimitedContinuations(CevalTest):
    """
    Tests for PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, INVOKE_K, POP_PROMPT
    """

    def test_reset_shift_invoke(self):
        """
        Program (+ 1 (shift k (k 5))) => 6. Validates PUSH_PROMPT,
        PUSH_SHIFT, INVOKE_K, POP_SHIFT, POP_PROMPT for continuation invocation
        """

        code = [
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["PUSH_SHIFT", 0],
            ["CONST", 5],
            ["INVOKE_K", 0],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = expr().run()
        self.assertEqual(result, 6)

    def test_delimited_continuation_cannot_be_called_from_python(self):
        """
        (reset (shift k k)) returns k; calling k from Python raises TypeError
        """
        # Shift body returns k without invoking via POP_SHIFT (empty body)
        code = [
            ["PUSH_PROMPT", 0],
            ["PUSH_SHIFT", 0],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        cont = expr().run()
        self.assertIsNotNone(cont)
        with self.assertRaises(TypeError) as ctx:
            cont(42)
        self.assertIn("cannot be called from Python", str(ctx.exception))

    def test_invoke_k_var_lookup_and_invoke(self):
        """
        INVOKE_K_VAR: shift body stores cont in ns, then INVOKE_K_VAR name
        invokes it with value. (+ 1 (shift k (do (var-store k k) (invoke-k-var k 5)))) => 6
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["PUSH_SHIFT", 0],
            ["VAR_STORE", "k"],
            ["CONST", 5],
            ["INVOKE_K_VAR", "k"],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = expr().run()
        self.assertEqual(result, 6)

    def test_invoke_k_with_non_continuation_raises_type_error(self):
        """
        INVOKE_K expects DelimitedContinuation at TOS-1; int => TypeError
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["PUSH_SHIFT", 0],
            ["POP", 1],
            ["CONST", 1],
            ["CONST", 2],
            ["INVOKE_K", 0],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        with self.assertRaises(TypeError) as ctx:
            expr().run()
        self.assertIn("DelimitedContinuation", str(ctx.exception))

    def test_delimited_continuation_stored_and_invoked_outside_shift(self):
        """
        (reset (shift k (pop-shift k))) stores k; then (k 42) invokes from
        outside the shift body. Result 42 returns to call site.
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["PUSH_SHIFT", 0],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["VAR_STORE", "k"],
            ["VAR", "k"],
            ["CONST", 42],
            ["INVOKE_K", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = expr().run()
        self.assertEqual(result, 42)

    def test_delimited_result_from_outside_shift(self):
        """
        (reset (shift k (pop-shift k))) then (k 99) => 99. When invoking from
        outside (after POP_SHIFT), the continuation's first op is POP_SHIFT
        which yields the value; the result 99 flows to the call site.
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["PUSH_SHIFT", 0],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["VAR_STORE", "k"],
            ["VAR", "k"],
            ["CONST", 99],
            ["INVOKE_K", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = expr().run()
        self.assertEqual(result, 99)

    def test_invoke_delimited_continuation_after_pop_prompt_returns_value(self):
        """
        (reset (shift k k)) then (k 42): shift returns k via POP_SHIFT, then
        we invoke k(42) after the prompt is gone. Push model allows invoke
        from outside; result 42 returns to call site.
        """
        # (reset (shift k k)) returns k; (k 42) invokes from outside the reset
        code = [
            ["PUSH_PROMPT", 0],
            ["PUSH_SHIFT", 0],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = expr().run()
        self.assertEqual(result, 42)


class TestUndelimitedContinuations(CevalTest):
    """
    Tests for PUSH_CC, POP_CC, CAPTURE_CC (undelimited call/cc block).
    """

    def test_call_cc_capture_and_invoke(self):
        """
        (call/cc [k] (k 42)) => 42.
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
        result = expr().run()
        self.assertEqual(result, 42)

    def test_call_cc_invoke_with_zero_args_resumes_with_none(self):
        """
        (call/cc [k] (k)) => None.
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
        result = expr().run()
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
        result = expr().run()
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
            expr().run()
        self.assertIn("CAPTURE_CC", str(ctx.exception))
        self.assertIn("no CC block", str(ctx.exception))

    def test_call_continuation_from_different_token_resumes_owner(self):
        """
        Invoking a continuation from a nested koskari context (different eval
        token) raises KoskariContinuationApply in the nested context, which
        propagates upward through the Python awaitable chain until it reaches
        the owning eval loop. The owning loop catches it via
        eval_phase_post_opcode, matches the token, applies the continuation,
        and resumes normally. The result is the value passed to the
        continuation.
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

        result = bottom_expr().run()
        self.assertEqual(result, 42)


    def test_continuation_passes_through_intermediate_context_with_dyn_with_unwind(self):
        """
        Four-level call stack: owner koskari -> python -> middle koskari
        (with an active dyn-with) -> python (invokes foreign continuation).

        The KoskariContinuationApply exception must:
          1. Be intercepted by the middle koskari eval loop (foreign token).
          2. Trigger a NULL-frame unwind in the middle context, running the
             dyn-with __exit__ before the context exits.
          3. Re-raise unchanged so it propagates upward through the Python
             async bridge to the owning koskari eval loop.
          4. Be caught and applied by the owner, resuming with 42.

        Assertions:
          - result == 42 (continuation applied correctly by owner)
          - dyn-with __exit__ was called (middle context unwound cleanly)
          - __exit__ received no exception info (the exception was hidden
            from the context manager during the segmented transfer)
        """

        class Tracker:
            def __init__(self):
                self.exited = False
                self.exc_info = (None, None, None)

            def __enter__(self):
                return self

            def __exit__(self, typ, val, tb):
                self.exited = True
                self.exc_info = (typ, val, tb)
                return False

        tracker = Tracker()

        def python_invoker(cont):
            return cont(42)

        middle_nspec = NamespaceSpec(None, ("cont",), ("invoker",))
        middle_instr = tuple([_op(op), arg] for op, arg in [
            ["CONST", tracker],
            ["PUSH_DYN_WITH", None],
            ["VAR", "invoker"],
            ["VAR", "cont"],
            ["CALL", 1],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ])
        middle_code = Code(middle_nspec, middle_instr, "middle")
        middle_ns = NamespaceSpec(None, ("invoker",), ()).get_namespace(None)
        middle_ns.store("invoker", python_invoker)
        middle_fn = make_callable(
            middle_code, middle_ns, FormalsSpec(("cont",)))

        async def python_bridge(cont):
            return await middle_fn(cont)

        bottom_code, bottom_ns = build_expr([
            ["VAR", "python_bridge"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ], values={"python_bridge": python_bridge})
        bottom_expr = make_callable(bottom_code, bottom_ns)

        result = bottom_expr().run()

        self.assertEqual(result, 42)
        self.assertTrue(tracker.exited, "dyn-with __exit__ must be called during middle-context unwind")
        self.assertIsNone(tracker.exc_info[0], "dyn-with __exit__ must see no exception (hidden during segmented transfer)")

    def test_invoking_continuation_after_eval_exits_raises_koskari_continuation_dead(self):
        """
        Capture a continuation, fully exit the koskari eval, then invoke it
        from Python. The owning context is no longer alive; expect
        KoskariContinuationDead (a normal Exception).
        """
        import koskari.lib as lib
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        cont = expr().run()
        self.assertIsNotNone(cont)
        with self.assertRaises(lib.KoskariContinuationDead) as ctx:
            cont(42)
        self.assertIn("no longer alive", str(ctx.exception))
        try:
            raise ctx.exception
        except Exception:
            pass

    def test_koskari_continuation_dead_is_normal_exception(self):
        """
        KoskariContinuationDead subclasses RuntimeError so it is a normal
        catchable exception, not a BaseException.
        """
        import koskari.lib as lib
        self.assertTrue(issubclass(lib.KoskariContinuationDead, RuntimeError))
        self.assertTrue(issubclass(lib.KoskariContinuationDead, Exception))


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
