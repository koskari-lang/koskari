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
Tests for PUSH_DYN_WITH, POP_DYN_WITH opcodes.

Context manager: enter runs before body, exit runs after (or on
exception). Uses a KOS_DYN_WITH node carrying KOS_DYN_CAP_WINDS on the
dynamic chain. Supports async enter/exit (auto-await). A continuation
transfer runs it (exit on unwind, enter on wind).
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class SimpleMgr:
    """Python context manager for tests; tracks enter/exit."""

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
    """Context manager that swallows exceptions."""

    def __exit__(self, typ, val, tb):
        self.exited = True
        self.exc_info = (typ, val, tb)
        return True


class AsyncMgr:
    """Async context manager."""

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


class TestDynWithOpcodes(CevalTest):
    """
    Tests for PUSH_DYN_WITH, POP_DYN_WITH opcodes.
    """

    async def test_push_dyn_with_pop_dyn_with_completes(self):
        """
        PUSH_DYN_WITH with matching POP_DYN_WITH completes normally.
        Expression result is the body value (None here).
        """

        mgr = SimpleMgr(100)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_DYN_WITH", None],
            ["CONST", None],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_push_dyn_with_body_runs_enter_exit(self):
        """
        PUSH_DYN_WITH runs enter; body runs; POP_DYN_WITH runs exit.
        Expression result is the body value (99).
        """

        mgr = SimpleMgr(200)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_DYN_WITH", None],
            ["CONST", 99],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_binding_entered_value(self):
        """
        PUSH_DYN_WITH [name] binds enter result to name in body.
        Body uses VAR_REF x to verify binding, then CONST 300 as result.
        """

        mgr = SimpleMgr(300)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_DYN_WITH", "x"],
            ["VAR_REF", "x"],
            ["POP", 1],
            ["CONST", 300],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 300)
        self.assertEqual(300, mgr.value)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


    async def test_dyn_with_exception_propagates(self):
        """
        Exception in body propagates after exit when exit returns False.
        """

        mgr = SimpleMgr()

        def raise_err():
            raise ValueError("oops")

        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_DYN_WITH", None],
            ["VAR", "raise_err"],
            ["CALL", 0],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ], values={"raise_err": raise_err})
        with self.assertRaises(ValueError) as ctx:
            await make_callable(code, ns)()
        self.assertIn("oops", str(ctx.exception))
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)
        self.assertEqual(mgr.exc_info[0], ValueError)


    async def test_dyn_with_async_enter_completes(self):
        """
        PUSH_DYN_WITH with async manager: enter returns awaitable, VM awaits
        then binds and runs body; POP_DYN_WITH runs exit.
        """

        mgr = AsyncMgr(100)
        code, ns = build_expr([
            ["CONST", mgr],
            ["PUSH_DYN_WITH", None],
            ["CONST", None],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertIsNone(result)
        self.assertTrue(mgr.entered)
        self.assertTrue(mgr.exited)


# The end.
