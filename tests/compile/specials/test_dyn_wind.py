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
Tests for koskari.compile compile/dyn-wind special form.

Dynamic-wind runs before/after thunks on extent entry and exit.
"""


from tests.dis.dis import dis
from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestDynWindSpecial(SpecialsTestCase):

    async def test_before_and_after_run_in_order(self):
        """
        before runs on entry, body runs, after runs on exit
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 42)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_after_only(self):
        """
        compile/dyn-wind with nil before and non-nil after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          []
          (compile/begin (compile/call record "body") 1)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 1)
        self.assertEqual(log, ["body", "after"])


    async def test_before_only(self):
        """
        compile/dyn-wind with non-nil before and nil after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 2)
          [])
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(log, ["before", "body"])


    async def test_keyword_form_before_and_after(self):
        """
        compile/dyn-wind keyword form with before, body, and after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/call record "body") 42)
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_keyword_form_after_only(self):
        """
        compile/dyn-wind keyword form with body and after only
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (body: (compile/call record "body") 1)
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 1)
        self.assertEqual(log, ["body", "after"])


    async def test_keyword_form_before_only(self):
        """
        compile/dyn-wind keyword form with before and body only
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/call record "body") 2))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(log, ["before", "body"])


    async def test_keyword_single_form_body(self):
        """
        Keyword form with single-form body (no extra list wrapper)
        """
        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/begin (compile/call record "body") 77))
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 77)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_positional_still_works(self):
        """
        Positional form continues to work after adding keyword form
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 99)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 99)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_dyn_wind_body_value_is_expression_result(self):
        """
        dyn-wind expression evaluates to the body's final value
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 999)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 999)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_dyn_wind_return_from_nested_call_cleans_up_and_delivers_value(self):
        """
        When body calls a function that returns, after-thunk runs and value is delivered
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/call (compile/lambda [] 99))
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 99)
        self.assertEqual(log, ["before", "after"])


class TestDynWindContinuations(SpecialsTestCase):
    """
    Tests for interactions between compile/dyn-wind and compile/let/cc.
    These exercise the interrupted-transfer machinery: when a wind thunk
    invokes a continuation while an outer transfer plan is already active,
    the outer plan must be suspended, the inner transfer must complete, and
    then the outer plan must be replanned and finished.
    """

    async def test_after_thunk_fires_on_continuation_caused_unwind(self):
        """
        When a continuation captured outside a dyn-wind is invoked from
        inside the body, the after-thunk runs before the continuation lands.
        """

        log = []

        def record(s):
            log.append(s)
            return s

        src_str = """
        (compile/let/cc k
          (compile/dyn-wind
            nil
            (compile/call k 42)
            (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, ["after"])


    async def test_continuation_invoked_from_after_thunk_during_transfer(self):
        """
        Invoking a second continuation from a dyn-wind after-thunk during an
        active outer transfer (interrupted transfer) must not discard the outer
        plan.  Both transfers must reach their targets: the inner continuation
        (inner-k) runs first, then the outer (outer-k) is replanned and
        completes with its own injected value.
        """

        src_str = """
        (compile/let/cc outer-k
          (compile/let/cc inner-k
            (compile/dyn-wind
              nil
              (compile/call outer-k 42)
              (compile/call inner-k 99))))
        """
        expr, mod = self.compile_expr(src_str)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_continuation_invoked_from_before_thunk_during_transfer(self):
        """
        Invoking an escape continuation from a dyn-wind before-thunk (which
        fires on re-entry during a transfer plan) also exercises the interrupted
        transfer path.  The escape wins and the outer transfer is replanned.
        """

        log = []

        def record(s):
            log.append(s)
            return s

        src_str = """
        (compile/let/cc escape
          (compile/let/cc k
            (compile/dyn-wind
              (compile/begin
                (compile/call record "before")
                (compile/call escape 99))
              (compile/begin
                (compile/call record "body")
                42)
              nil)
            (compile/call k 0)))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        result = await expr()
        self.assertEqual(result, 99)
        self.assertIn("before", log)


class TestDynWindTransferWindReproducer(SpecialsTestCase):
    """
    Minimal reproducer: continuation invoke into a dyn-wind body should
    re-run the before-thunk as part of the transfer plan wind step.
    """

    async def test_before_thunk_runs_on_transfer_into_dyn_wind_body(self):
        """
        capture/cc inside dyn-wind body; gated invoke_saved after setup.
        Transfer into the captured segment must run before-thunk again.
        """

        log = []
        before_pass = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        def before_thunk():
            record(f"before-{before_pass[0]}")
            before_pass[0] += 1

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record(f"invoke-saved-py saved={bool(saved)}")
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (compile/begin
          (compile/define result
            (compile/dyn-wind
              (compile/begin
                (compile/call record "before-setup")
                (compile/call before_thunk))
              (compile/capture/cc k
                (compile/begin
                  (compile/call record "stash")
                  (compile/call stash k)
                  (compile/call record "capture-body")
                  42))
              nil))
          (compile/call record "after-setup")
          (compile/call record result)
          (compile/cond [saved (invoke_saved)] [else: result]))
        """
        expr, mod = self.compile_expr(
            src_str,
            before_thunk=before_thunk,
            stash=stash,
            invoke_saved=invoke_saved,
            record=record,
            saved=saved)

        result = await expr()

        self.assertEqual(result, 99, msg=f"log={log}")
        self.assertEqual(log, [
            "before-setup",
            "before-0",
            "stash",
            "stash-py",
            "capture-body",
            "after-setup",
            42,
            "invoke-saved-py saved=True",
            "before-setup",
            "before-1",
            "after-setup",
            99,
        ])


class TestDynWindTransferFailures(SpecialsTestCase):
    """
    Failed transfer abandonment: when a transfer plan step raises during
    dyn-wind before-thunk execution, the plan nest is abandoned and the
    exception is dispatched through try/except on the manifested chain.
    """

    async def test_try_except_catches_python_error_in_before_thunk_during_transfer(
            self):
        """
        A Python error in a dyn-wind before-thunk during continuation transfer
        is caught by an enclosing try/except on the installed segment.

        try wraps dyn-wind; let/cc captures inside the body; invoke_saved runs
        once (gated on saved) after setup in the same expression.
        """

        log = []
        wind_pass = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        def wind_before():
            record(f"wind-before-{wind_pass[0]}")
            wind_pass[0] += 1
            if wind_pass[0] >= 2:
                raise ValueError("wind-fail")

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record(f"invoke-saved-py saved={bool(saved)}")
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (compile/begin
          (compile/define result
            (compile/try
              (compile/dyn-wind
                (compile/begin
                  (compile/call record "before-setup")
                  (compile/call wind_before))
                (compile/let/cc k
                  (compile/begin
                    (compile/call record "stash")
                    (compile/call stash k)
                    (compile/call record "capture-body")
                    42))
                nil)
              ([Exception] (compile/call record "caught"))))
          (compile/begin
            (compile/call record "after-setup")
            (compile/cond [saved (invoke_saved)] [else: result])))
        """
        expr, mod = self.compile_expr(
            src_str,
            stash=stash,
            invoke_saved=invoke_saved,
            wind_before=wind_before,
            record=record,
            saved=saved,
            Exception=Exception)

        result = await expr()

        self.assertEqual(result, "caught")
        self.assertEqual(log, [
            "before-setup",
            "wind-before-0",
            "stash",
            "stash-py",
            "capture-body",
            "after-setup",
            "invoke-saved-py saved=True",
            "before-setup",
            "wind-before-1",
            "caught",
            "after-setup",
        ])
        self.assertEqual(wind_pass[0], 2)


    async def test_try_except_catches_koskari_raise_in_before_thunk_during_transfer(
            self):
        """
        A Koskari raise in a dyn-wind before-thunk during continuation transfer
        is caught by an enclosing try/except on the installed segment.
        """

        log = []
        wind_pass = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        def should_raise():
            record(f"should-raise-{wind_pass[0]}")
            wind_pass[0] += 1
            return wind_pass[0] >= 2

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record(f"invoke-saved-py saved={bool(saved)}")
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (compile/begin
          (compile/define result
            (compile/try
              (compile/dyn-wind
                (compile/begin
                  (compile/call record "before-setup")
                  (compile/cond
                    [(compile/call should_raise)
                      (compile/begin
                        (compile/call record "raise")
                        (compile/raise (ValueError "wind-fail")))]
                    [else:
                      (compile/begin
                        (compile/call record "before-setup-ok")
                        0)]))
                (compile/let/cc k
                  (compile/call record "stash")
                  (compile/call stash k)
                  (compile/call record "capture-body")
                  42)
                nil)
              ([Exception] (compile/call record "caught"))))

          (compile/call record "after-setup")
          (compile/call record result)

          (compile/cond
            [saved (invoke_saved)]
            [else: result])

        )
        """
        expr, mod = self.compile_expr(
            src_str,
            stash=stash,
            invoke_saved=invoke_saved,
            should_raise=should_raise,
            record=record,
            saved=saved,
            Exception=Exception,
            ValueError=ValueError)

        try:
          result = await expr()

        finally:
          # Dear AI agent. DO NOT REMOVE THIS PRINT STATEMENT. NO MATTER WHAT.
          print(f"log: {log}")

        self.assertEqual(result, "caught")
        self.assertEqual(log, [
            "before-setup",
            "should-raise-0",
            "before-setup-ok",
            "stash",
            "stash-py",
            "capture-body",
            "after-setup",
            42,
            "invoke-saved-py saved=True",
            "before-setup",
            "should-raise-1",
            "raise",
            "caught",
            "after-setup",
            "caught",
        ])
        self.assertEqual(wind_pass[0], 2)


class TestDynWindRaiseUnwind(SpecialsTestCase):

    async def test_after_thunk_runs_on_exception_unwind(self):
        """
        An exception unwinding through a dyn-wind extent runs the after
        thunk exactly once (parked on the wind while the thunk runs)
        before the enclosing handler catches it.
        """

        events = []

        def rec(tag):
            events.append(tag)
            return None

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (compile/try
          (compile/dyn-wind
            (rec "before")
            (boom 1)
            (rec "after"))
          ([ValueError] "caught"))
        """
        expr, mod = self.compile_expr(
            src_str, rec=rec, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")
        self.assertEqual(events, ["before", "after"])


    async def test_after_thunk_failure_supersedes_chained(self):
        """
        KNOWN DEFECT (intentionally red): when the after thunk itself
        raises during an exception unwind, the thunk's exception
        supersedes the original and propagates with the original chained
        as __context__. Today the parked original is dropped unchained.
        """

        def boom(v):
            raise ValueError("original")

        def boom2(v):
            raise RuntimeError("teardown")

        src_str = """
        (compile/dyn-wind
          (compile/begin)
          (boom 1)
          (boom2 1))
        """
        expr, mod = self.compile_expr(src_str, boom=boom, boom2=boom2)
        try:
            await expr()
        except RuntimeError as exc:
            self.assertEqual(str(exc), "teardown")
            self.assertIsInstance(exc.__context__, ValueError)
            self.assertEqual(str(exc.__context__), "original")
        else:
            self.fail("RuntimeError did not propagate")


    async def test_after_thunk_failure_caught_with_context(self):
        """
        KNOWN DEFECT (intentionally red): the superseding teardown
        exception is catchable by an enclosing handler, and the handler
        sees the original as __context__.
        """

        def boom(v):
            raise ValueError("original")

        def boom2(v):
            raise RuntimeError("teardown")

        def probe():
            import sys
            exc = sys.exc_info()[1]
            ctx = exc.__context__ if exc is not None else None
            return type(ctx).__name__ if ctx is not None else "none"

        src_str = """
        (compile/try
          (compile/dyn-wind
            (compile/begin)
            (boom 1)
            (boom2 1))
          ([RuntimeError] (probe)))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, boom2=boom2, probe=probe,
            RuntimeError=RuntimeError)
        result = await expr()
        self.assertEqual(result, "ValueError")


# The end.
