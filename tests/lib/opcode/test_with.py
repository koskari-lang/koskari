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
Tests for PUSH_WITH, POP_WITH opcodes.

Single-shot context manager: enter runs before body, exit runs after (or on
exception). Uses KOS_DYN_WITH on the dynamic chain. Supports async
enter/exit (auto-await).
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


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


class TestWithOpcodes(CevalTest):
    """
    Tests for PUSH_WITH, POP_WITH opcodes
    """

    async def test_push_with_pop_with_completes(self):
        """
        PUSH_WITH with matching POP_WITH completes normally.
        Body leaves None on stack; expression result is None.
        """

        mgr = SimpleMgr(100)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["CONST", None],
            ["POP_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_push_with_body_runs_enter_exit(self):
        """
        PUSH_WITH runs enter; body runs; POP_WITH promotes body value.
        """

        mgr = SimpleMgr(200)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["CONST", 99],
            ["POP_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_exception_propagates_when_exit_does_not_swallow(self):
        """
        Exception in body propagates after exit when exit returns False.
        """

        mgr = SimpleMgr()

        def raise_err():
            raise ValueError("oops")

        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["VAR", "raise_err"],
            ["CALL", 0],
            ["POP_WITH", 0],
            ["RETURN", 1],
        ], values={"raise_err": raise_err})
        with self.assertRaises(ValueError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("oops", str(ctx.exception))
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)


    async def test_exception_swallowed_when_exit_returns_true(self):
        """
        When exit returns True, exception is swallowed; execution resumes
        after the with body; expression yields None.
        """

        state = {}
        mgr = SwallowMgr()

        def after_raise(x):
            state["after"] = x

        def raise_err():
            raise ValueError("oops")

        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["VAR", "raise_err"],
            ["CALL", 0],
            ["POP_WITH", 0],
            ["VAR", "after_raise"],
            ["CONST", 1],
            ["CALL", 1],
            ["POP", 1],
            ["RETURN", 1],
        ], values={"raise_err": raise_err, "after_raise": after_raise})
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)
        self.assertEqual(state.get("after"), 1)


    async def test_with_async_enter_completes(self):
        """
        PUSH_WITH with async manager: enter returns awaitable, VM awaits it
        then binds and runs body; POP_WITH promotes body value. Body is None.
        """

        mgr = AsyncMgr(100)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["CONST", None],
            ["POP_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_with_async_exit_normal(self):
        """
        Async manager: normal exit; __aexit__ is awaited, then block and node
        pop; body value 99 is promoted.
        """

        mgr = AsyncMgr(200)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["CONST", 99],
            ["POP_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], None)


    async def test_with_async_exit_swallow(self):
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

        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_WITH", None],
            ["VAR", "raise_err"],
            ["CALL", 0],
            ["POP_WITH", 0],
            ["VAR", "after_raise"],
            ["CONST", 1],
            ["CALL", 1],
            ["POP", 1],
            ["RETURN", 1],
        ], values={"raise_err": raise_err, "after_raise": after_raise})
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)
        self.assertEqual(state.get("after"), 1)


# The end.
