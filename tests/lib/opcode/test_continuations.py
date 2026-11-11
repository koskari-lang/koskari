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
CALL on DelimitedContinuation, POP_PROMPT, PUSH_CC, CAPTURE_CC, POP_CC)
and evaluator token.
Includes cross-context continuation pass-through tests verifying the
segmented transfer path (foreign-token KoskariContinuationApply handling).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from koskari.exceptions import KoskariPromptNotFound
from koskari.lib import Code, FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, _op, build_code, build_expr


pytestmark = pytest.mark.phase_lib


class TestDelimitedContinuations(CevalTest):
    """
    Tests for PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, CALL, POP_PROMPT
    """

    async def test_reset_shift_invoke(self):
        """
        Program (+ 1 (shift k (k 5))) => 6. Validates PUSH_PROMPT,
        PUSH_SHIFT, CALL, POP_SHIFT, POP_PROMPT for continuation invocation
        """

        code = [
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["PUSH_SHIFT", 0],
            ["CONST", 5],
            ["CALL", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_delimited_continuation_cannot_be_called_from_python(self):
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
        cont = await expr()
        self.assertIsNotNone(cont)
        with self.assertRaises(TypeError) as ctx:
            cont(42)
        self.assertIn("cannot be called from Python", str(ctx.exception))


    async def test_var_store_then_var_call_invokes_delimited(self):
        """
        Shift body stores cont in ns, then VAR and CALL invoke it with value.
        (+ 1 (shift k (do (var-store k k) (k 5)))) => 6
        """
        code = [
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["PUSH_SHIFT", 0],
            ["VAR_STORE", "k"],
            ["VAR", "k"],
            ["CONST", 5],
            ["CALL", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_delimited_continuation_stored_and_invoked_outside_shift(self):
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
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_delimited_result_from_outside_shift(self):
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
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 99)


    async def test_invoke_delimited_continuation_after_pop_prompt_returns_value(self):
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
        result = await expr()
        self.assertEqual(result, 42)


    async def test_tagged_shift_passes_inner_prompt_graft(self):
        """
        A shift tagged A inside a default-tag prompt inside a prompt
        tagged A: the handler's value k is delivered at the outer
        prompt, and invoking k from there re-enters the region through
        the inner prompt, whose own POP_PROMPT pops it as a reset.
        (+ 1 5) then (+ 6 10): 16. Delivery at the inner prompt would
        add 10 to k; an early return at the inner prompt would give 6.
        """

        code = [
            ["VAR", "tag_a"],
            ["PUSH_PROMPT", 1],
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["VAR", "tag_a"],
            ["PUSH_SHIFT", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["CONST", 10],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["CONST", 5],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": object()})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 16)


    async def test_tagged_shift_passes_inner_prompt_inplace(self):
        """
        The same shape with k invoked inside the handler: the slice
        resumes on the live chain, the inner prompt pops as a reset, and
        the outer prompt's POP_PROMPT returns 16 to the handler, which
        delivers it at the outer prompt. Delivery at the inner prompt
        would give 26.
        """

        code = [
            ["VAR", "tag_a"],
            ["PUSH_PROMPT", 1],
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["VAR", "tag_a"],
            ["PUSH_SHIFT", 1],
            ["CONST", 5],
            ["CALL", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["CONST", 10],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": object()})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 16)


    async def test_prompt_tags_match_by_identity(self):
        """
        Two tags that compare equal are different tags: a shift seeking
        one does not stop at a prompt carrying the other, and the
        failure names the tag it sought
        """

        tag_a = [1]
        tag_b = [1]
        code = [
            ["VAR", "tag_a"],
            ["PUSH_PROMPT", 1],
            ["VAR", "tag_b"],
            ["PUSH_SHIFT", 1],
            ["POP_SHIFT", 0],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(
            code, values={"tag_a": tag_a, "tag_b": tag_b})
        expr = make_callable(code_obj, ns)
        with self.assertRaises(KoskariPromptNotFound) as caught:
            await expr()
        self.assertIs(caught.exception.tag, tag_b)


    async def test_stack_tag_is_not_a_captured_operand(self):
        """
        A tag an instruction takes from the stack is gone before the
        prompt is pushed or the continuation captured: the region's one
        pending operand is the 1, so (k 5) is 6. None taken from the
        stack is the default tag, so it reaches the prompt a count of
        0 made
        """

        code = [
            ["PUSH_PROMPT", 0],
            ["CONST", 1],
            ["VAR", "tag"],
            ["PUSH_SHIFT", 1],
            ["CONST", 5],
            ["CALL", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag": None})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_none_stack_tag_makes_a_default_prompt(self):
        """
        A prompt pushed with None taken from the stack carries the
        default tag: a shift whose count is 0 captures up to it
        """

        code = [
            ["VAR", "tag"],
            ["PUSH_PROMPT", 1],
            ["CONST", 1],
            ["PUSH_SHIFT", 0],
            ["CONST", 5],
            ["CALL", 1],
            ["POP_SHIFT", 0],
            ["OBJECT_ADD", 2],
            ["POP_PROMPT", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag": None})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_prompt_count_above_1_refused_at_build(self):
        """
        A prompt instruction's count is 0 for the default tag or 1 for
        a tag from the stack; building code with any other count fails
        """

        for name in ("PUSH_PROMPT", "PUSH_SHIFT"):
            code = [
                [name, 2],
                ["RETURN", 0],
            ]
            with self.assertRaises(ValueError) as caught:
                build_expr(code)
            self.assertEqual(
                "Code instruction 0 argument 2 is not 0 or 1",
                str(caught.exception))


class TestUndelimitedContinuations(CevalTest):
    """
    Tests for PUSH_CC, POP_CC, CAPTURE_CC (undelimited call/cc block).
    """

    async def test_call_cc_capture_and_invoke(self):
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
        result = await expr()
        self.assertEqual(result, 42)


    async def test_call_cc_invoke_with_zero_args_resumes_with_none(self):
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
        result = await expr()
        self.assertIsNone(result)


    async def test_call_cc_capture_returns_continuation(self):
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
        result = await expr()
        self.assertIsNotNone(result)
        import koskari.lib as lib
        self.assertIsInstance(result, lib.Continuation)


    async def test_capture_cc_without_push_cc_raises(self):
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
            await expr()
        self.assertIn("CAPTURE_CC", str(ctx.exception))
        self.assertIn("no CC block", str(ctx.exception))


    async def test_call_continuation_from_different_token_resumes_owner(self):
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

        result = await bottom_expr()
        self.assertEqual(result, 42)


    async def test_continuation_passes_through_intermediate_context_with_dyn_with_unwind(self):
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

        result = await bottom_expr()

        self.assertEqual(result, 42)
        self.assertTrue(tracker.exited, "dyn-with __exit__ must be called during middle-context unwind")
        self.assertIsNone(tracker.exc_info[0], "dyn-with __exit__ must see no exception (hidden during segmented transfer)")


    async def test_invoking_continuation_after_eval_exits_raises_koskari_continuation_dead(self):
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
        cont = await expr()
        self.assertIsNotNone(cont)

        with self.assertRaises(lib.KoskariContinuationDead) as ctx:
            cont(42)

        self.assertIn("no longer alive", str(ctx.exception))

        try:
            raise ctx.exception
        except Exception:
            pass


    async def test_koskari_continuation_dead_is_normal_exception(self):
        """
        KoskariContinuationDead subclasses RuntimeError so it is a normal
        catchable exception, not a BaseException.
        """

        import koskari.lib as lib
        self.assertTrue(issubclass(lib.KoskariContinuationDead, RuntimeError))
        self.assertTrue(issubclass(lib.KoskariContinuationDead, Exception))


    async def test_foreign_continuation_apply_thrown_into_gen_unwinds(self):
        """
        A KoskariContinuationApply delivered into a suspended generator via
        asend.throw() reaches the unified RAISE ingress on the revive-throw
        path and is dispatched as a continuation signal: the generator's
        local extents unwind through the cc-unwind plan (dyn-with __exit__
        runs with no exception info) and the apply re-raises unchanged so it
        can travel onward to its owner.
        """

        import koskari.lib as lib

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
        caught = []

        def steal(k):
            try:
                k(42)
            except lib.KoskariContinuationApply as e:
                caught.append(e)
            return "stolen"

        owner_code, owner_ns = build_expr([
            ["VAR", "steal"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
            ["RETURN", 1],
        ], values={"steal": steal})
        owner_result = await make_callable(owner_code, owner_ns)()
        self.assertEqual(owner_result, "stolen")
        self.assertEqual(len(caught), 1)

        other_code, other_ns = build_expr([
            ["CONST", tracker],
            ["PUSH_DYN_WITH", None],
            ["CONST", "other-ready"],
            ["YIELD", 1],
            ["POP_DYN_WITH", 0],
            ["RETURN", 0],
        ])
        other = make_callable(other_code, other_ns)()
        ready = await other.__anext__()
        self.assertEqual(ready, "other-ready")

        asend = other.__anext__()
        with self.assertRaises(lib.KoskariContinuationApply):
            await asend.throw(lib.KoskariContinuationApply, caught[0])

        self.assertTrue(
            tracker.exited,
            "dyn-with __exit__ must run during foreign-apply unwind")
        self.assertIsNone(
            tracker.exc_info[0],
            "dyn-with __exit__ must see no exception info")


class TestEvaluatorToken(CevalTest):
    """
    Tests for KosEvaluatorToken and exception types from evaluator-token model
    """

    async def test_evaluator_token_and_exceptions_exist_on_module(self):
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
