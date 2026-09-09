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
Tests for koskari.compile dyn-with special form.

Multi-shot context manager: enter runs before body, exit runs after (or on
exception). Uses PUSH_DYN_WITH/POP_DYN_WITH. Participates in transfer plans.
"""

import pytest

from . import Park, SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class SimpleMgr:
    """
    Python context manager for tests; tracks enter/exit
    """

    def __init__(self, value=42):
        self.value = value
        self.entered = False
        self.exited = False
        self.exc_info = (None, None, None)


    def __enter__(self):
        self.entered = True
        return self.value


    def __exit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return False


class TestDynWithSpecial(SpecialsTestCase):
    """
    Tests for dyn-with special form
    """

    async def test_normal_enter_exit_expression_returns_none(self):
        """
        dyn-with runs enter, body, exit; expression result is None
        """

        state = {}

        def set_y(val):
            state["y"] = val

        mgr = SimpleMgr(100)
        src = """
        (dyn-with [(call make_mgr)]
          (call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_binding_entered_value(self):
        """
        dyn-with [name expr] binds the enter result to name in body
        """

        state = {}
        mgr = SimpleMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (dyn-with [x (call make_mgr)]
          (call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_emits_push_dyn_with_pop_dyn_with(self):
        """
        dyn-with emits PUSH_DYN_WITH and POP_DYN_WITH opcodes
        """

        from koskari.lib import OpCode

        src = "(dyn-with [x (call make_mgr)] (call noop x))"
        code = self.compile_to_code(src, make_mgr=lambda: None, noop=lambda x: None)
        ops = [code.oparg(i)[0] for i in range(code.length())]
        self.assertIn(OpCode.PUSH_DYN_WITH, ops)
        self.assertIn(OpCode.POP_DYN_WITH, ops)


    async def test_dyn_with_body_value_is_expression_result(self):
        """
        dyn-with block evaluates to the body's final value (TDD: will fail until
        POP_DYN_WITH preserves body value instead of pushing None)
        """

        mgr = SimpleMgr(100)
        src = """
        (dyn-with [x (call make_mgr)]
          100)
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 100)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_return_from_nested_call_runs_exit_and_delivers_value(self):
        """
        When body calls a function that returns, exit runs and value is delivered
        """

        mgr = SimpleMgr(200)
        src = """
        (dyn-with [x (call make_mgr)]
          (call (lambda [] 99)))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


class TestDynWithShiftUnwind(SpecialsTestCase):

    async def test_shift_result_delivery_runs_exit_once(self):
        """
        a shift body completing inside a dyn-with delivers its value to
        the reset and runs the manager's __exit__ exactly once
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
        (reset
          (dyn-with [(call make_mgr)]
            (shift k 5)))
        """
        expr, mod = self.compile_expr(src_str, make_mgr=Mgr)
        result = await expr()
        self.assertEqual(result, 5)
        self.assertEqual(events, ["enter", "exit"])


class TestDynWithShiftSuspendedExit(SpecialsTestCase):

    def make_manager(self, events):
        class Mgr:

            async def __aenter__(self):
                events.append("aenter")
                return self

            async def __aexit__(self, typ, val, tb):
                events.append("aexit-start")
                await Park()
                events.append("aexit-done")
                return False

        return Mgr


    async def test_shift_result_delivery_awaits_exit(self):
        """
        a shift body completing inside a dyn-with whose __aexit__
        suspends parks the transfer; the second send finishes __aexit__
        and delivers the shift result to the reset
        """

        events = []
        src_str = """
        (lambda []
          (reset
            (dyn-with [(call make_mgr)]
              (shift k 5))))
        """
        expr, mod = self.compile_expr(
            src_str, make_mgr=self.make_manager(events))
        fn = await expr()

        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        self.assertEqual(events, ["aenter", "aexit-start"])
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, 5)
        self.assertEqual(events, ["aenter", "aexit-start", "aexit-done"])


    async def test_abandoned_mid_transfer_collects(self):
        """
        a coroutine parked in a transfer's __aexit__ that refers back to
        itself through the manager is a cycle, and is collected
        """

        import gc
        import weakref

        events = []
        Mgr = self.make_manager(events)
        src_str = """
        (lambda []
          (reset
            (dyn-with [(call make_mgr)]
              (shift k 5))))
        """
        expr, mod = self.compile_expr(src_str, make_mgr=Mgr)
        fn = await expr()

        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        mgr = [m for m in gc.get_referrers(Mgr) if isinstance(m, Mgr)][0]
        mgr.coro = coro
        alive = weakref.ref(mgr)

        del coro, mgr, fn, expr, mod
        gc.collect()
        self.assertIsNone(alive())


class TestDynWithTransferWindReproducer(SpecialsTestCase):
    """
    Minimal reproducer: continuation invoke into a dyn-with body should
    re-run __enter__ as part of the transfer plan wind step.
    """

    async def test_enter_runs_on_transfer_into_dyn_with_body(self):
        """
        capture/cc inside dyn-with body; gated invoke_saved after setup.
        Transfer into the captured segment must run enter again.
        """

        log = []
        enter_count = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        class CountingMgr(SimpleMgr):
            """
            Counts enters on the type: the protocol methods resolve on
            the manager's type, so an instance attribute would not do
            """

            def __enter__(self):
                record("enter-%s" % enter_count[0])
                enter_count[0] += 1
                return SimpleMgr.__enter__(self)

        def make_mgr():
            return CountingMgr(42)

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record("invoke-saved-py saved=%s" % bool(saved))
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (begin
          (define result
            (dyn-with [x (call make_mgr)]
              (capture/cc k
                (begin
                  (call record "stash")
                  (call stash k)
                  (call record "capture-body")
                  42))))
          (call record "after-setup")
          (call record result)
          (cond [saved (invoke_saved)] [else: result]))
        """
        expr, mod = self.compile_expr(
            src_str,
            make_mgr=make_mgr,
            stash=stash,
            invoke_saved=invoke_saved,
            record=record,
            saved=saved)
        await expr()
        self.assertGreaterEqual(
            enter_count[0], 2,
            msg="log=%r enter_count=%r" % (log, enter_count[0]))
        self.assertTrue(
            any(isinstance(entry, str) and "invoke-saved-py" in entry
                for entry in log),
            msg="log=%r" % (log,))


class TestDynWithTransferFailures(SpecialsTestCase):
    """
    Failed transfer abandonment across dyn-with extents: when a transfer
    plan wind step fails synchronously, local handler dispatch must honor
    an async dyn-with exit on the manifested chain (suspend and complete
    __aexit__) instead of dropping it.
    """

    async def test_async_exit_completes_when_transfer_wind_step_fails(self):
        """
        Nested dyn-withs inside a try: the outer manager has async
        __aenter__/__aexit__, the inner manager's sync __enter__ raises on
        transfer re-entry (a synchronous plan-step failure).  The abandon
        path must run the outer manager's async __aexit__ to completion
        before the enclosing try/except catches. The inner manager's
        __exit__ does not run: its __enter__ never returned.
        """

        import asyncio

        log = []
        enter_count = [0]
        aexit_count = [0]
        saved = []

        def record(s):
            log.append(s)
            return s

        class AsyncExitMgr:
            async def __aenter__(self):
                record("aenter")
                return self

            async def __aexit__(self, typ, val, tb):
                n = aexit_count[0]
                aexit_count[0] += 1
                record(f"aexit-start-{n}")
                await asyncio.sleep(0)
                record(f"aexit-done-{n}")
                return False

        class FailingEnterMgr:
            def __enter__(self):
                n = enter_count[0]
                enter_count[0] += 1
                record(f"enter-{n}")
                if n >= 1:
                    raise ValueError("enter-fail")
                return self

            def __exit__(self, typ, val, tb):
                record("exit")
                return False

        def make_outer():
            return AsyncExitMgr()

        def make_inner():
            return FailingEnterMgr()

        def stash(k):
            record("stash-py")
            saved.append(k)

        def invoke_saved():
            record(f"invoke-saved-py saved={bool(saved)}")
            k = saved[0]
            saved.clear()
            return k(99)

        src_str = """
        (begin
          (define result
            (try
              (dyn-with [a (call make_outer)]
                (dyn-with [b (call make_inner)]
                  (capture/cc k
                    (begin
                      (call stash k)
                      (call record "capture-body")
                      42))))
              ([Exception] (call record "caught"))))
          (call record "after-setup")
          (call record result)
          (cond [saved (invoke_saved)] [else: result]))
        """
        expr, mod = self.compile_expr(
            src_str,
            make_outer=make_outer,
            make_inner=make_inner,
            stash=stash,
            invoke_saved=invoke_saved,
            record=record,
            saved=saved,
            Exception=Exception)

        result = await expr()

        self.assertEqual(result, "caught", msg=f"log={log}")
        self.assertEqual(log, [
            "aenter",
            "enter-0",
            "stash-py",
            "capture-body",
            "exit",
            "aexit-start-0",
            "aexit-done-0",
            "after-setup",
            42,
            "invoke-saved-py saved=True",
            "aenter",
            "enter-1",
            "aexit-start-1",
            "aexit-done-1",
            "caught",
            "after-setup",
            "caught",
        ])
        self.assertEqual(enter_count[0], 2)
        self.assertEqual(aexit_count[0], 2)


class TestReenteredWithBindsItsOwnScope(SpecialsTestCase):
    """
    When a transfer re-enters a dyn-with, __enter__ runs while the
    landed frame above the with is current. The result must bind into
    the with's own scope, the namespace the with was pushed in, not the
    namespace that happens to be live at dyn_top.
    """

    async def test_reentered_with_binds_manager_in_its_own_frame(self):
        managers = []

        class Mgr:
            def __enter__(self):
                managers.append(self)
                return self
            def __exit__(self, *exc):
                return False

        saved = []
        helper_expr, mod = self.compile_expr(
            "(lambda [] (let/cc k (begin (call stash k) 0)))",
            stash=saved.append)
        helper = await helper_expr()

        src_str = """
        (let [[n 0]]
          (begin
            (dyn-with [x (call mgr)]
              (begin
                (call helper)
                (setq n (+ n 1))
                (call record x)))
            (call record "after-with")
            (cond ((== n 1) (call (call take) 0)) (else: nil))
            (call record "after-cond")
            n))
        """
        log = []
        expr, mod = self.compile_expr(
            src_str, mgr=Mgr, helper=helper, record=log.append,
            take=lambda: saved[-1])
        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(len(managers), 2)
        self.assertEqual(
            log,
            [managers[0], "after-with", managers[1], "after-with",
             "after-cond"])


# The end.
