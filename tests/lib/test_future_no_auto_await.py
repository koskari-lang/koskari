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
Unit tests for the asyncio future exclusion from implicit auto-await.

Values passing asyncio's isfuture duck check (type provides
_asyncio_future_blocking, instance value is not None) are handles: a call
returning one pushes it as a plain value instead of eagerly awaiting it.
Explicit AWAIT still drives futures.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import koskari.lib as lib
import pytest

from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class RaisingBlockingAwaitable:
    """
    Awaitable whose _asyncio_future_blocking descriptor raises
    """

    @property
    def _asyncio_future_blocking(self):
        raise ValueError("boom from descriptor")


    def __await__(self):
        return iter(())


class GetattrTrapAwaitable:
    """
    Awaitable whose instance __getattr__ raises for any missing name;
    the class itself does not provide _asyncio_future_blocking
    """

    def __getattr__(self, name):
        raise ValueError("instance __getattr__ consulted for %s" % name)


    def __await__(self):
        if False:
            yield None
        return 42


class TestFutureNoAutoAwait(unittest.IsolatedAsyncioTestCase):
    """
    Call results passing the isfuture duck check come back as handles
    """

    async def test_pending_future_returned_as_handle(self):
        """
        A call returning a pending Future yields the Future, un-awaited
        """

        fut = asyncio.get_running_loop().create_future()

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"fn": lambda: fut})
        result = await make_callable(code, ns)()
        self.assertIs(result, fut)
        self.assertFalse(fut.done())
        fut.set_result(None)


    async def test_task_returned_as_handle(self):
        """
        A call returning a Task yields the Task itself
        """

        async def work():
            return "done"

        task = asyncio.get_running_loop().create_task(work())

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"fn": lambda: task})
        result = await make_callable(code, ns)()
        self.assertIs(result, task)
        self.assertEqual(await task, "done")


    async def test_explicit_await_resolves_future(self):
        """
        Explicit AWAIT drives a Future to its value
        """

        fut = asyncio.get_running_loop().create_future()
        fut.set_result(99)

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["AWAIT", 1],
            ["RETURN", 1],
        ], values={"fn": lambda: fut})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_coroutine_result_still_auto_awaits(self):
        """
        A call returning a plain coroutine is still eagerly awaited
        """

        async def work():
            return 42

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"fn": work})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


    async def test_excluded_future_push_then_pop(self):
        """
        Fused CALL peephole: excluded future immediately popped is dropped
        """

        fut = asyncio.get_running_loop().create_future()

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["POP", 1],
            ["CONST", 42],
            ["RETURN", 1],
        ], values={"fn": lambda: fut})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)
        self.assertFalse(fut.done())
        fut.set_result(None)


    async def test_raising_blocking_descriptor_propagates(self):
        """
        A raising _asyncio_future_blocking descriptor propagates its error
        """

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"fn": RaisingBlockingAwaitable})
        with self.assertRaisesRegex(ValueError, "boom from descriptor"):
            await make_callable(code, ns)()


    async def test_instance_getattr_never_consulted(self):
        """
        Non-future awaitable with raising __getattr__ still auto-awaits
        """

        code, ns = build_expr([
            ["VAR", "fn"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"fn": GetattrTrapAwaitable})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 42)


class TestFutureUnderDemand(unittest.TestCase):
    """
    Demand extents return future handles instead of denying suspension
    """

    def test_demand_call_returns_future_handle(self):
        """
        A demand-driven call returning a pending Future yields the handle
        """

        loop = asyncio.new_event_loop()
        try:
            fut = loop.create_future()

            code, ns = build_expr([
                ["VAR", "fn"],
                ["CALL", 0],
                ["RETURN", 1],
            ], values={"fn": lambda: fut})
            result = lib.demand(make_callable(code, ns))()
            self.assertIs(result, fut)
        finally:
            loop.close()


# The end.
