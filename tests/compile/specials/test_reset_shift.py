# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for koskari.compile reset and shift special forms.

reset establishes a prompt; shift captures the continuation and runs a
handler. Mirrors the semantics in design/language/control-flow.md.
"""

import pytest

from koskari.exceptions import KoskariPromptNotFound, KoskariPromptUnreachable
from koskari.lib import DelimitedContinuation, ParamRef
from tests.dis.dis import dis

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestResetShiftSpecials(SpecialsTestCase):

    async def test_reset_shift_invoke(self):
        """
        (reset (+ 1 (shift k (call k 5)))) => 6
        """

        add_fn = lambda a, b: a + b
        src_str = """
        (reset (+ 1 (shift k (call k 5))))
        """
        expr, mod = self.compile_expr(src_str, **{'+': add_fn})
        result = await expr()
        self.assertEqual(result, 6)


    async def test_reset_shift_return_cont(self):
        """
        (reset (shift k k)) => continuation
        """

        src_str = """
        (reset (shift k k))
        """
        expr, mod = self.compile_expr(src_str)
        cont = await expr()
        self.assertIsNotNone(cont)
        self.assertTrue(callable(cont))


    async def test_shift_stores_and_invokes(self):
        """
        reset returns k via (shift k k); store in outer scope, then invoke k(42)
        """

        src_str = """
        (begin
          (setq k (reset (shift k k)))
          (call k 42))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_delimited_param_reset_shift_no_let(self):
        """
        parameterize + reset + shift without let. Continuation resumes add(10)
        = 10 + 100 = 110.
        """

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (parameterize [[amt 100]]
          (reset
            (add (shift k (call k 10)))))
        """

        expr, mod = self.compile_expr(src_str, add=add)

        result = await expr()
        self.assertEqual(result, 110)


    async def test_delimited_param_first_branch_only(self):
        """
        First branch only: parameterize amt=100, reset, shift stores k and
        invokes k(10). Continuation resumes add(10) = 10 + 100 = 110.
        """

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (let [[stolen 0]]
          (parameterize [[amt 100]]
            (reset
              (add (shift k (setq stolen k) (call k 10))))))
        """

        expr, mod = self.compile_expr(src_str, add=add)

        result = await expr()
        self.assertEqual(result, 110)


    async def test_delimited_let_reset_shift_no_param(self):
        """
        let + reset + shift without parameterize. Sanity check.
        """

        def add(a, b):
            print("add", a, b)
            return a + b

        src_str = """
        (let [[stolen 0]]
          (reset
            (add 1 (shift k (setq stolen k) (call k 10)))))
        """

        expr, mod = self.compile_expr(src_str, add=add)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 11)


    async def test_delimited_param_outside_reset_not_captured(self):
        """
        Param outside reset is not captured; stolen k invoked in different
        param scope sees that scope's amt. Expected: (110, 209).

        First seq branch: parameterize amt=100, reset body runs. shift captures
        k, stores it in stolen, invokes k(10). Continuation resumes: add(10)
        = 10 + amt = 10 + 100 = 110.

        Second seq branch: parameterize amt=200, invoke stolen(9). Continuation
        resumes and transfers back to original param scope: add(9) = 9 + 100 = 109.
        """

        def seq(*args):
            return args

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (parameterize [[amt 100]]
          (let [[stolen 0]]
            (call seq None
              (call add 1)
              (reset
                (call add (shift k (setq stolen k) (call k 10))))
              (parameterize [[amt 200]]
                (call stolen 9))
              (call add 2)
            )))
        """

        expr, mod = self.compile_expr(src_str, add=add, seq=seq)
        # dis(expr.code)

        self.assertIs(vars(mod).resolve("add"), add)
        self.assertIs(vars(mod).resolve("seq"), seq)

        result = await expr()
        self.assertEqual(result, (None, 101, 110, 109, 102))


    async def test_dyn_wind_around_shift_captures_in_place(self):
        """
        dyn-wind wrapping shift captures in place; shift body POP_SHIFT and
        POP_WIND complete without extent or wind topology errors.
        """

        src_str = """
        (reset
          (dyn-wind
            (begin)
            (shift k k)
            nil))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertIsInstance(result, DelimitedContinuation)


    async def test_dyn_wind_around_shift_invoke_after_capture(self):
        """
        Remote delimited invoke with dyn-wind wrapping shift delivers the
        injected value to the call site. Thunk lifecycle is covered by
        test_dyn_wind_thunk_count_remote_invoke.
        """

        src_str = """
        (let [[captured nil]]
          (begin
            (setq captured
              (reset
                (dyn-wind
                  (begin)
                  (shift k k)
                  nil)))
            (call captured 5)))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 5)


    async def test_dyn_wind_around_shift_invoke_inside_shift_body(self):
        """
        In-shift delimited invoke with dyn-wind wrapping shift delivers the
        injected value to the call site. Thunk lifecycle is covered by
        test_dyn_wind_thunk_count_invoke_inside_shift_body.
        """

        src_str = """
        (reset
          (dyn-wind
            (begin)
            (shift k (call k 5))
            nil))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 5)


    async def test_dyn_wind_thunk_count_invoke_inside_shift_body(self):
        """
        (reset (dyn-wind before (shift k (call k 5)) after)) returns 5, and
        an in-shift invoke runs the dyn-wind thunks exactly once: a single
        [before, after] lifecycle. Regression guard for Path A in
        design/interpreter/delimited-invoke-model.md (resume in place when
        the prompt is in live ancestry; no graft, no wind replay).
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src_str = """
        (reset
          (dyn-wind
            (rec "before")
            (shift k (call k 5))
            (rec "after")))
        """
        expr, mod = self.compile_expr(src_str, rec=rec)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["before", "after"])


    async def test_dyn_wind_thunk_count_remote_invoke(self):
        """
        Remote invoke (reset finished before call) winds the graft exactly
        once: natural [before, after] during capture, then one [before,
        after] replay around the invoke. Regression guard for Path B in
        design/interpreter/delimited-invoke-model.md.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src_str = """
        (let [[captured nil]]
          (begin
            (setq captured
              (reset
                (dyn-wind
                  (rec "before")
                  (shift k k)
                  (rec "after"))))
            (call captured 5)))
        """
        expr, mod = self.compile_expr(src_str, rec=rec)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["before", "after", "before", "after"])


    async def test_param_in_caller_frame_uses_invoke_scope(self):
        """
        A parameterize established in a caller frame outside the reset's
        owning frame is not captured by the bounded template: remote invoke
        resolves it from the invoke-time dynamic environment (here amt=200,
        so 7 + 200 = 207). Params between the prompt and its owning frame
        remain captured (test_delimited_param_outside_reset_not_captured).
        """

        def add(a):
            return a + ParamRef('amt', 0).deref()

        src_str = """
        (let [[captured nil]]
          (begin
            (parameterize [[amt 100]]
              (setq captured
                (call
                  (lambda []
                    (reset (add (shift k k)))))))
            (parameterize [[amt 200]]
              (call captured 7))))
        """
        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(result, 207)


    async def test_dyn_wind_thunk_count_invoke_in_deeper_frame(self):
        """
        Same single-lifecycle guarantee as
        test_dyn_wind_thunk_count_invoke_inside_shift_body, but the in-shift
        invoke happens in a deeper frame (k passed to and called from a
        lambda inside the shift body). Regression guard for Path A in
        design/interpreter/delimited-invoke-model.md.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src_str = """
        (reset
          (dyn-wind
            (rec "before")
            (shift k
              (call (lambda [f] (call f 5)) k))
            (rec "after")))
        """
        expr, mod = self.compile_expr(src_str, rec=rec)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["before", "after"])


    async def test_dyn_wind_thunk_count_multishot_in_shift(self):
        """
        Multi-shot in-shift invoke: the first call runs in place on the live
        slice (Path A); each re-invoke instantiates the captured template
        via the PUSH graft (Path B fallback). Every invoke gets exactly one
        before/after pair, and add(3, 4) = 7 flows out of the reset.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        def add(a, b):
            return a + b

        src_str = """
        (reset
          (dyn-wind
            (rec "before")
            (shift k
              (add (call k 3) (call k 4)))
            (rec "after")))
        """
        expr, mod = self.compile_expr(src_str, rec=rec, add=add)
        result = await expr()
        self.assertEqual(result, 7)
        self.assertEqual(events, ["before", "after", "before", "after"])


    async def test_recursive_invoke_in_slice_path(self):
        """
        Recursive delimited invoke: the shift body stashes k in a module
        global and the slice conditionally re-invokes it, so k runs in the
        path of k. Recursion is permitted and structurally safe: in-path
        invokes can never take Path A (the live SHIFT node sits in the
        INPLACE delim's stash during the slice, so readiness fails) and
        route to the bounded PUSH graft, one template instance per level.
        Levels unwind independently: guard sees 0, 1, 2, 3 and every level
        returns the base value 3.
        """

        events = []

        def lt(a, b):
            events.append(a)
            return a < b

        def inc(n):
            return n + 1

        src_str = """
        (begin
          (setq g nil)
          (reset
            (begin
              (setq n
                (shift k
                  (begin
                    (setq g k)
                    (call k 0))))
              (cond
                [(lt n 3) (call g (inc n))]
                [else: n]))))
        """
        expr, mod = self.compile_expr(src_str, lt=lt, inc=inc)
        result = await expr()
        self.assertEqual(result, 3)
        self.assertEqual(events, [0, 1, 2, 3])


    async def test_inplace_slice_exception_caught_at_invoke_site(self):
        """
        An exception raised inside an in-place (Path A) slice restitches the
        invoke span and propagates to the invoke call site, where the
        enclosing try in the shift body catches it.
        """

        def boom(v):
            raise ValueError("slice-boom")

        src_str = """
        (reset
          (boom
            (shift k
              (try
                (call k 5)
                ([ValueError] "caught")))))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")


    async def test_inplace_slice_raise_runs_after_thunk(self):
        """
        When an in-place (Path A) slice raises, the dyn-wind after thunk
        wrapping the slice runs exactly once during the raise unwind, before
        the exception reaches the invoke-site handler.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        def boom(v):
            raise ValueError("slice-boom")

        src_str = """
        (reset
          (dyn-wind
            (rec "before")
            (boom
              (shift k
                (try
                  (call k 5)
                  ([ValueError] "caught"))))
            (rec "after")))
        """
        expr, mod = self.compile_expr(
            src_str, rec=rec, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")
        self.assertEqual(events, ["before", "after"])


    async def test_remote_invoke_slice_exception_propagates(self):
        """
        An exception raised by the continuation slice during a remote invoke
        routes to the invoke call site, where the enclosing try catches it
        and this test returns "caught".
        """

        def boom(v):
            raise ValueError("slice-boom")

        src_str = """
        (let [[captured nil]]
          (begin
            (setq captured
              (reset (boom (shift k k))))
            (try
              (call captured 5)
              ([ValueError] "caught"))))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")


    async def test_remote_invoke_with_enter_exit_count(self):
        """
        A with wrapping shift under a remote invoke sees a single
        [enter, exit] lifecycle on the manager.
        """

        events = []

        class Mgr:

            def __enter__(self):
                events.append("enter")
                return self

            def __exit__(self, typ, val, tb):
                events.append("exit")
                return False

        src_str = """
        (let [[captured nil]]
          (begin
            (setq captured
              (reset
                (with [(call make_mgr)]
                  (shift k k))))
            (call captured 5)))
        """
        expr, mod = self.compile_expr(src_str, make_mgr=Mgr)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["enter", "exit"])


class TestShiftCaptureErrors(SpecialsTestCase):

    async def test_shift_without_reset_raises_prompt_not_found(self):
        """
        shift with no enclosing reset raises KoskariPromptNotFound
        carrying the sought prompt id and the operation name
        """

        src_str = """
        (shift k (call k 1))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(KoskariPromptNotFound) as caught:
            await expr()
        self.assertEqual(caught.exception.prompt_id, 0)
        self.assertEqual(caught.exception.operation, "shift")

    async def test_shift_across_generator_raises_prompt_unreachable(self):
        """
        A reset exists outside the generator, but the shift inside the
        generator body cannot capture across the generator boundary:
        KoskariPromptUnreachable, naming the boundary
        """

        src_str = """
        (reset
          (begin
            (setq g
              (call
                (lambda [] (yield (shift k 1)))))
            (next g)))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await expr()
        self.assertEqual(caught.exception.boundary, "generator")
        self.assertEqual(caught.exception.operation, "shift")
        self.assertEqual(caught.exception.prompt_id, 0)


# The end.
