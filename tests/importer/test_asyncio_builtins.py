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
tests.importer.test_asyncio_builtins

koskari.asyncio builtin surface: create-task returns a real asyncio.Task
(a handle -- excluded from implicit auto-await, cancellable from koskari,
resolved by explicit await), and the as_coroutine / coroutine_fn adapters
satisfy asyncio's strict type checks.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio

from koskari.asyncio import as_coroutine, coroutine_fn, create_task
from koskari.lib import defer
from tests.compile.specials import SpecialsTestCase

import pytest


pytestmark = pytest.mark.phase_basics


async def _work():
    await asyncio.sleep(0)
    return "ok"


class TestCreateTask(SpecialsTestCase):
    """
    create-task returns the Task itself and koskari treats it as a handle
    """

    async def test_returns_real_task(self):
        """
        (create_task (defer work)) evaluates to the Task, un-awaited
        """

        src = """
        (create_task (defer work))
        """
        expr, _mod = self.compile_expr(
            src, create_task=create_task, defer=defer, work=_work)
        result = await expr()
        self.assertIsInstance(result, asyncio.Task)
        self.assertFalse(result.done())
        self.assertEqual(await result, "ok")


    async def test_task_survives_call_boundary(self):
        """
        A Task passed through koskari call boundaries stays a Task
        """

        src = """
        (identity (create_task (defer work)))
        """
        expr, _mod = self.compile_expr(
            src, create_task=create_task, defer=defer, work=_work,
            identity=lambda x: x)
        result = await expr()
        self.assertIsInstance(result, asyncio.Task)
        self.assertEqual(await result, "ok")


    async def test_cancel_from_koskari(self):
        """
        (.cancel task) works from koskari source
        """

        src = """
        (compile/begin
          (compile/define t (create_task (defer work)))
          (await (sleep 0))
          (t.cancel)
          t)
        """
        expr, _mod = self.compile_expr(
            src, create_task=create_task, defer=defer, work=_work,
            sleep=asyncio.sleep)
        task = await expr()
        self.assertIsInstance(task, asyncio.Task)
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(task.cancelled())


    async def test_explicit_await_resolves_task(self):
        """
        (await (create_task ...)) yields the task result
        """

        src = """
        (await (create_task (defer work)))
        """
        expr, _mod = self.compile_expr(
            src, create_task=create_task, defer=defer, work=_work)
        self.assertEqual(await expr(), "ok")


    async def test_koskari_coroutine_schedules_directly(self):
        """
        A KosCoroutine input is scheduled without an as_coroutine wrapper
        """

        src = """
        "kos-result"
        """
        expr, _mod = self.compile_expr(src)
        coro = expr()
        task = create_task(coro)
        self.assertIs(task.get_coro(), coro)
        self.assertEqual(await task, "kos-result")


    async def test_defer_input_wraps(self):
        """
        A defer input is adapted through as_coroutine and still resolves
        """

        d = defer(_work)
        task = create_task(d)
        self.assertIsNot(task.get_coro(), d)
        self.assertEqual(await task, "ok")


class TestAdapters(SpecialsTestCase):
    """
    as_coroutine / coroutine_fn satisfy asyncio's strict type checks
    """

    async def test_as_coroutine_passes_iscoroutine(self):
        """
        as_coroutine(defer) is a real Python coroutine
        """

        coro = as_coroutine(defer(_work))
        self.assertTrue(asyncio.iscoroutine(coro))
        self.assertEqual(await coro, "ok")


    async def test_kos_coroutine_passes_iscoroutine(self):
        """
        KosCoroutine passes asyncio.iscoroutine via the abc hook
        """

        src = """
        42
        """
        expr, _mod = self.compile_expr(src)
        coro = expr()
        self.assertTrue(asyncio.iscoroutine(coro))
        self.assertEqual(await coro, 42)


    async def test_coroutine_fn_passes_iscoroutinefunction(self):
        """
        coroutine_fn wraps a koskari callable into a coroutine function
        """

        src = """
        "wrapped"
        """
        expr, _mod = self.compile_expr(src)
        wrapped = coroutine_fn(expr)
        self.assertTrue(asyncio.iscoroutinefunction(wrapped))
        self.assertEqual(await wrapped(), "wrapped")


# The end.
