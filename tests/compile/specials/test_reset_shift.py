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
Tests for koskari.compile compile/reset and compile/shift special forms.

reset establishes a prompt; shift captures the continuation and runs a
handler. Mirrors the semantics in design/language/control-flow.md.
"""

from . import SpecialsTestCase
from koskari.lib import DelimitedContinuation, ParamRef
from koskari.dis import dis

import pytest

pytestmark = pytest.mark.phase_specials


class TestResetShiftSpecials(SpecialsTestCase):

    async def test_reset_shift_invoke(self):
        """
        (compile/reset (+ 1 (compile/shift k (compile/call k 5)))) => 6
        """

        add_fn = lambda a, b: a + b
        src_str = """
        (compile/reset (+ 1 (compile/shift k (compile/call k 5))))
        """
        expr, mod = self.compile_expr(src_str, **{'+': add_fn})
        result = await expr()
        self.assertEqual(result, 6)


    async def test_reset_shift_return_cont(self):
        """
        (compile/reset (compile/shift k k)) => continuation
        """

        src_str = """
        (compile/reset (compile/shift k k))
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
        (compile/begin
          (compile/setq k (compile/reset (compile/shift k k)))
          (compile/call k 42))
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
        (compile/parameterize [[amt 100]]
          (compile/reset
            (add (compile/shift k (compile/call k 10)))))
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
        (compile/let [[stolen 0]]
          (compile/parameterize [[amt 100]]
            (compile/reset
              (add (compile/shift k (compile/setq stolen k) (compile/call k 10))))))
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
        (compile/let [[stolen 0]]
          (compile/reset
            (add 1 (compile/shift k (compile/setq stolen k) (compile/call k 10)))))
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
        (compile/parameterize [[amt 100]]
          (compile/let [[stolen 0]]
            (compile/call seq None
              (compile/call add 1)
              (compile/reset
                (compile/call add (compile/shift k (compile/setq stolen k) (compile/call k 10))))
              (compile/parameterize [[amt 200]]
                (compile/call stolen 9))
              (compile/call add 2)
            )))
        """

        expr, mod = self.compile_expr(src_str, add=add, seq=seq)
        # dis(expr.code)

        self.assertIs(mod.add, add)
        self.assertIs(mod.seq, seq)

        result = await expr()
        self.assertEqual(result, (None, 101, 110, 109, 102))


    async def test_dyn_wind_around_shift_captures_in_place(self):
        """
        dyn-wind wrapping shift captures in place; shift body POP_SHIFT and
        POP_WIND complete without extent or wind topology errors.
        """

        src_str = """
        (compile/reset
          (compile/dyn-wind
            (compile/begin)
            (compile/shift k k)
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
        (compile/let [[captured nil]]
          (compile/begin
            (compile/setq captured
              (compile/reset
                (compile/dyn-wind
                  (compile/begin)
                  (compile/shift k k)
                  nil)))
            (compile/call captured 5)))
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
        (compile/reset
          (compile/dyn-wind
            (compile/begin)
            (compile/shift k (compile/call k 5))
            nil))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 5)


    async def test_dyn_wind_thunk_count_invoke_inside_shift_body(self):
        """
        KNOWN BUG (intentionally red): in-shift invoke double-runs dyn-wind
        thunks.

        (reset (dyn-wind before (shift k (call k 5)) after)) returns 5, but
        the PUSH invoke model grafts a clone of the captured segment while
        the live WIND below is still entered, replaying before on the clone
        and unwinding both copies: [before, before, after, after]. Correct
        lifecycle is a single [before, after]. Fix is Path A in
        design/interpreter/delimited-invoke-model.md (resume in place when
        the prompt is in live ancestry; no graft, no wind replay).
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src_str = """
        (compile/reset
          (compile/dyn-wind
            (rec "before")
            (compile/shift k (compile/call k 5))
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
        (compile/let [[captured nil]]
          (compile/begin
            (compile/setq captured
              (compile/reset
                (compile/dyn-wind
                  (rec "before")
                  (compile/shift k k)
                  (rec "after"))))
            (compile/call captured 5)))
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
        (compile/let [[captured nil]]
          (compile/begin
            (compile/parameterize [[amt 100]]
              (compile/setq captured
                (compile/call
                  (compile/lambda []
                    (compile/reset (add (compile/shift k k)))))))
            (compile/parameterize [[amt 200]]
              (compile/call captured 7))))
        """
        expr, mod = self.compile_expr(src_str, add=add)
        result = await expr()
        self.assertEqual(result, 207)


    async def test_dyn_wind_thunk_count_invoke_in_deeper_frame(self):
        """
        KNOWN BUG (intentionally red): same double-wind as
        test_dyn_wind_thunk_count_invoke_inside_shift_body, but the in-shift
        invoke happens in a deeper frame (k passed to and called from a
        lambda inside the shift body). Correct lifecycle is a single
        [before, after]. Fix is Path A in
        design/interpreter/delimited-invoke-model.md.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        src_str = """
        (compile/reset
          (compile/dyn-wind
            (rec "before")
            (compile/shift k
              (compile/call (compile/lambda [f] (compile/call f 5)) k))
            (rec "after")))
        """
        expr, mod = self.compile_expr(src_str, rec=rec)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["before", "after"])


    async def test_remote_invoke_slice_exception_propagates(self):
        """
        Characterization (KNOWN DEFECT): an exception raised by the
        continuation slice during a remote invoke escapes the evaluator as a
        raw Python exception instead of routing to the invoke call site.

        The enclosing compile/try around (call captured 5) should catch the
        ValueError and this test should return "caught"; today the RAISE walk
        from the grafted segment never reaches the live handlers below the
        DELIM bookmark. Pinned as-is; target behavior is part of the
        delimited-invoke-model.md rework.
        """

        def boom(v):
            raise ValueError("slice-boom")

        src_str = """
        (compile/let [[captured nil]]
          (compile/begin
            (compile/setq captured
              (compile/reset (boom (compile/shift k k))))
            (compile/try
              (compile/call captured 5)
              ([ValueError] "caught"))))
        """
        expr, mod = self.compile_expr(src_str, boom=boom)
        with self.assertRaises(ValueError):
            await expr()


    async def test_remote_invoke_with_enter_exit_count(self):
        """
        Characterization (KNOWN DEFECT): compile/with wrapping shift under a
        remote invoke calls __exit__ twice on the same manager — once at
        POP_SHIFT truncation during capture, once more when the grafted
        clone's WITH unwinds on the invoke return path. Correct lifecycle
        would be [enter, exit] (or a defined re-enter policy). Pinned as-is
        so the Path B rework cannot change it silently; fix tracked
        separately.
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
        (compile/let [[captured nil]]
          (compile/begin
            (compile/setq captured
              (compile/reset
                (compile/with [(compile/call make_mgr)]
                  (compile/shift k k))))
            (compile/call captured 5)))
        """
        expr, mod = self.compile_expr(src_str, make_mgr=Mgr)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["enter", "exit", "exit"])


# The end.
