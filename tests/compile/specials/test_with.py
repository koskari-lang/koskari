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
Tests for koskari.compile compile/with special form.

Single-shot context manager: enter runs before body, exit runs after (or on
exception). Uses PUSH_WITH/POP_WITH. Covers sync and async __enter__/__exit__
(auto-await).
"""

from . import SpecialsTestCase
from koskari.dis import dis


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
        (compile/with [(compile/call make_mgr)]
          (compile/call set_y 1))
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
        (compile/with [x (compile/call make_mgr)]
          (compile/call check x))
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
        (compile/with [(compile/call make_mgr)]
          (compile/call raise_err))
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
        (compile/begin
          (compile/with [(compile/call make_mgr)]
            (compile/call raise_err))
          (compile/call after_raise 1))
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
        compile/with with async manager: VM awaits enter then body then exit;
        expression result is None.
        """
        state = {}
        mgr = AsyncMgr(100)

        def set_y(val):
            state["y"] = val

        src = """
        (compile/with [(compile/call make_mgr)]
          (compile/call set_y 1))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, set_y=set_y)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("y"), 1)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_async_with_binding_entered_value(self):
        """compile/with [name expr] with async manager binds enter result in body."""
        state = {}
        mgr = AsyncMgr(200)

        def check(x):
            state["got"] = x

        src = """
        (compile/with [x (compile/call make_mgr)]
          (compile/call check x))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr, check=check)
        result = await expr()
        self.assertIsNone(result)
        self.assertEqual(state.get("got"), 200)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


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
        (compile/begin
          (compile/with [(compile/call make_mgr)]
            (compile/call raise_err))
          (compile/call after_raise 1))
        """
        expr, mod = self.compile_expr(
            src,
            make_mgr=lambda: mgr,
            raise_err=raise_err,
            after_raise=after_raise,
        )
        # dis(expr.code)
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
        (compile/with [x (compile/call make_mgr)]
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
        (compile/with [x (compile/call make_mgr)]
          (compile/call (compile/lambda [] 99)))
        """
        expr, mod = self.compile_expr(src, make_mgr=lambda: mgr)
        result = await expr()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


# The end.
