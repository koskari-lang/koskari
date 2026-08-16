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

from subprocess import CalledProcessError

from koskari.asyncio import (ProcessResult, as_coroutine, coroutine_fn,
                             create_task, run_process)
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
        (begin
          (define t (create_task (defer work)))
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


    async def test_defer_input_schedules_directly(self):
        """
        A defer passes iscoroutine and is scheduled as the Task's coro
        """

        d = defer(_work)
        task = create_task(d)
        self.assertIs(task.get_coro(), d)
        self.assertEqual(await task, "ok")


    async def test_task_cancel_defer_direct(self):
        """
        Cancelling a directly-scheduled defer mid-flight cancels cleanly
        """

        async def hang():
            await asyncio.sleep(30)

        d = defer(hang)
        task = create_task(d)
        await asyncio.sleep(0)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(task.cancelled())


    async def test_defer_with_kos_coroutine_inner_cancel(self):
        """
        CancelledError forwards through the defer into koskari eval
        """

        src = """
        (await (sleep 30))
        """
        expr, _mod = self.compile_expr(src, sleep=asyncio.sleep)
        d = defer(expr)
        task = create_task(d)
        await asyncio.sleep(0)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(task.cancelled())


    async def test_task_cancel_before_start_no_warning(self):
        """
        Cancel-before-start closes the held coroutine without a
        never-awaited RuntimeWarning
        """

        import warnings

        d = defer(_work)
        task = create_task(d)
        task.cancel()
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            with self.assertRaises(asyncio.CancelledError):
                await task
            del d, task
            import gc
            gc.collect()
        messages = [str(w.message) for w in caught]
        self.assertFalse(
            [m for m in messages if "never awaited" in m], messages)


    async def test_task_on_spent_defer(self):
        """
        Scheduling a spent defer completes the Task with the reuse error
        """

        d = defer(_work)
        self.assertEqual(await d, "ok")
        task = create_task(d)
        with self.assertRaisesRegex(
                RuntimeError, "cannot reuse already awaited defer"):
            await task


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


    async def test_defer_passes_iscoroutine(self):
        """
        defer passes iscoroutine via the structural abc hook
        """

        import collections.abc

        d = defer(_work)
        self.assertTrue(asyncio.iscoroutine(d))
        self.assertTrue(isinstance(d, collections.abc.Coroutine))
        self.assertEqual(await d, "ok")


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


    def test_asyncio_run_accepts_defer(self):
        """
        asyncio.run drives a defer end-to-end (strict iscoroutine gate)
        """

        self.assertEqual(asyncio.run(defer(_work)), "ok")


    async def test_coroutine_fn_passes_iscoroutinefunction(self):
        """
        coroutine_fn wraps a koskari callable into a coroutine function
        """

        import inspect

        src = """
        "wrapped"
        """
        expr, _mod = self.compile_expr(src)
        wrapped = coroutine_fn(expr)
        # inspect variant: asyncio.iscoroutinefunction is deprecated for
        # removal in 3.16 and defers to this one
        self.assertTrue(inspect.iscoroutinefunction(wrapped))
        self.assertEqual(await wrapped(), "wrapped")


class TestRunProcess(SpecialsTestCase):
    """
    The run-process builtin and the ProcessResult it returns
    """

    async def test_run_process_from_koskari(self):
        src = '(run-process "echo" "hello")'
        expr, _mod = self.compile_expr(src)
        result = await expr()
        self.assertIsInstance(result, ProcessResult)
        self.assertEqual(result.args, ("echo", "hello"))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"hello\n")
        self.assertEqual(result.text, "hello\n")


    async def test_raise_for_status_returns_self(self):
        result = await run_process("true")
        self.assertIs(result.raise_for_status(), result)


    async def test_raise_for_status_raises(self):
        result = await run_process("false")
        self.assertEqual(result.returncode, 1)

        with self.assertRaises(CalledProcessError) as cm:
            result.raise_for_status()
        self.assertEqual(cm.exception.returncode, 1)
        self.assertEqual(cm.exception.cmd, ("false",))


    def test_json_and_jsonl(self):
        result = ProcessResult(("x",), 0, b'{"a": 1}', b"")
        self.assertEqual(result.json(), {"a": 1})

        result = ProcessResult(
            ("x",), 0, b'{"a": 1}\n\n{"b": 2}\n', b"")
        self.assertEqual(result.jsonl(), [{"a": 1}, {"b": 2}])


    def test_stderr_text(self):
        result = ProcessResult(("x",), 1, b"", b"oops\n")
        self.assertEqual(result.stderr_text, "oops\n")


    def test_yaml(self):
        from importlib.util import find_spec

        result = ProcessResult(("x",), 0, b"a: 1\nb: [2, 3]\n", b"")
        if find_spec("yaml"):
            self.assertEqual(result.yaml(), {"a": 1, "b": [2, 3]})
        else:
            with self.assertRaises(ImportError):
                result.yaml()


    def test_repr(self):
        result = ProcessResult(("echo", "hi"), 3, b"", b"")
        self.assertIn("'echo'", repr(result))
        self.assertIn("returncode=3", repr(result))


# The end.
