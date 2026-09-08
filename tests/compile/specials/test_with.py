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
Tests for koskari.compile with special form.

Single-shot context manager: enter runs before body, exit runs after (or on
exception). Uses PUSH_WITH/POP_WITH. Covers sync and async __enter__/__exit__
(auto-await).
"""

import pytest

from tests.dis.dis import dis

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class SimpleMgr:
    """Python context manager for tests; tracks enter/exit and optional binding."""

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


class SwallowMgr(SimpleMgr):
    """
    Context manager that swallows exceptions (exit returns True)
    """

    def __exit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return True


class AsyncMgr:
    """
    Async context manager: __aenter__/__aexit__ return awaitables
    """

    def __init__(self, value=42):
        self.value = value
        self.entered = False
        self.exited = False
        self.exc_info = (None, None, None)


    async def __aenter__(self):
        self.entered = True
        return self.value


    async def __aexit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return False


class AsyncSwallowMgr(AsyncMgr):
    """
    Async context manager that swallows exceptions
    """

    async def __aexit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return True


class FutureEnterMgr:
    """
    Context manager whose __aenter__/__aexit__ return asyncio Futures.
    Futures are auto-await-exempt handles, so the with binding receives
    the Future itself rather than its result.
    """

    def __init__(self, enter_fut, exit_fut):
        self.enter_fut = enter_fut
        self.exit_fut = exit_fut


    def __aenter__(self):
        return self.enter_fut


    def __aexit__(self, typ, val, tb):
        return self.exit_fut


class FailingEnterMgr:
    """
    __enter__ raises; __exit__ must then never run
    """

    def __init__(self):
        self.events = []


    def __enter__(self):
        self.events.append("enter")
        raise ValueError("refused")


    def __exit__(self, typ, val, tb):
        self.events.append("exit")
        return False


class TestWithEnterFailure(SpecialsTestCase):

    async def test_failing_enter_runs_no_exit(self):
        """
        a manager whose __enter__ raises is unlinked without __exit__
        """

        mgr = FailingEnterMgr()

        src_str = """
        (try
          (with [(call get_mgr)] 1)
          ([ValueError] "caught"))
        """
        expr, mod = self.compile_expr(
            src_str, get_mgr=lambda: mgr, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")
        self.assertEqual(mgr.events, ["enter"])


class TestWithSpecial(SpecialsTestCase):

    async def test_normal_enter_exit_expression_returns_none(self):
        """
        With runs enter, body, exit; expression result is None
        """

        state = {}

        def set_y(val):
            state["y"] = val

        mgr = SimpleMgr(100)
        src = """
        (with [(call make_mgr)]
          (call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_with_binding_entered_value(self):
        """With [name expr] binds the enter result to name in body."""
        state = {}
        mgr = SimpleMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (with [x (call make_mgr)]
          (call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_exception_propagates_when_exit_does_not_swallow(self):
        """Exception in body propagates after exit when exit returns false."""
        mgr = SimpleMgr()

        def raise_err():
            raise ValueError("oops")

        src = """
        (with [(call make_mgr)]
          (call raise_err))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, raise_err=raise_err)
        with self.assertRaises(ValueError) as ctx:
            await expr()
        self.assertIn("oops", str(ctx.exception))
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)


    async def test_exception_swallowed_when_exit_returns_true(self):
        """
        When exit returns true, exception is swallowed; execution resumes
        after the with body; expression yields None.
        """
        state = {}
        mgr = SwallowMgr()

        def after_raise(x):
            state["after"] = x

        def raise_err():
            raise ValueError("oops")

        src = """
        (begin
          (with [(call make_mgr)]
            (call raise_err))
          (call after_raise 1))
        """
        expr, mod = self.compile_expr(
            src,
            make_mgr=lambda: mgr,
            raise_err=raise_err,
            after_raise=after_raise,
        )
        result = await expr()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)
        self.assertEqual(state.get("after"), 1)


    async def test_async_enter_exit_expression_returns_none(self):
        """
        with with async manager: VM awaits enter then body then exit;
        expression result is None.
        """
        state = {}
        mgr = AsyncMgr(100)

        def set_y(val):
            state["y"] = val

        src = """
        (with [(call make_mgr)]
          (call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_async_with_binding_entered_value(self):
        """with [name expr] with async manager binds enter result in body."""
        state = {}
        mgr = AsyncMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (with [x (call make_mgr)]
          (call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_future_returning_aenter_binds_handle(self):
        """
        __aenter__ returning a Future binds the Future handle, not its
        result: futures are excluded from implicit auto-await.
        """
        import asyncio

        state = {}
        loop = asyncio.get_running_loop()
        enter_fut = loop.create_future()
        enter_fut.set_result(200)
        exit_fut = loop.create_future()
        exit_fut.set_result(False)
        mgr = FutureEnterMgr(enter_fut, exit_fut)

        def check(x):
            state["got"] = x

        src = """
        (with [x (call make_mgr)]
          (call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertIs(state.get("got"), enter_fut)


    async def test_async_exit_swallow(self):
        """
        Async manager: exception in body; __aexit__ returns True (swallow);
        execution resumes after with; expression yields None.
        """
        state = {}
        mgr = AsyncSwallowMgr()

        def after_raise(x):
            state["after"] = x

        def raise_err():
            raise ValueError("oops")

        src = """
        (begin
          (with [(call make_mgr)]
            (call raise_err))
          (call after_raise 1))
        """
        expr, mod = self.compile_expr(
            src,
            make_mgr=lambda: mgr,
            raise_err=raise_err,
            after_raise=after_raise,
        )

        dis(expr.code)

        result = await expr()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)
        self.assertEqual(state.get("after"), 1)


    async def test_with_body_value_is_expression_result(self):
        """
        with block evaluates to the body's final value (TDD: will fail until
        POP_WITH preserves body value instead of pushing None)
        """

        mgr = SimpleMgr(100)
        src = """
        (with [x (call make_mgr)]
          100)
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 100)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_with_return_from_nested_call_runs_exit_and_delivers_value(self):
        """
        When body calls a function that returns, exit runs and value is delivered
        """

        mgr = SimpleMgr(200)
        src = """
        (with [x (call make_mgr)]
          (call (lambda [] 99)))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


# The end.
