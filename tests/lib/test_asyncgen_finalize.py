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
tests.lib.test_asyncgen_finalize

An async generator dropped while suspended is closed through its
finalizer, before any clearing, so its finally body runs against its
frame as it was: on a plain drop, and inside a reference cycle
collected by gc. Under a running event loop the finalizer hands a
generator at rest to the loop's finalizer hook, and its cleanup runs in
the aclose task the loop schedules. A generator left mid-await, which
nothing can resume, closes at its drop; one left mid-await on a
transform's chain closes with that chain when the transform is dropped

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest

import pytest

from koskari.lib import AsyncGenerator, KoskariSyncViolation, KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
import-from asyncio sleep

define [produce box record]:
    try:
        (yield 1)
    finally:
        (record box.tag)

define [waits record]:
    try:
        (yield 1)
    finally:
        (await (sleep 0))
        (record "finally")

define [yields_again record]:
    try:
        (yield 1)
    finally:
        (record "fin-start")
        (yield 2)
        (record "fin-end")

define [break_waits record]:
    (for-each [x (waits record)] (break))
    "ok"

define [mid_await_awaits pending record]:
    try:
        (yield 1)
        (await pending)
    finally:
        (record "fin-start")
        (await pending)
        (record "fin-end")

define [mid_await_raises pending record]:
    try:
        (yield 1)
        (await pending)
    finally:
        (record "fin-start")
        raise (ValueError "cleanup failed")

define [consume source]:
    (for-each [x source] None)
    "done"

define [mid_await_plain pending record]:
    try:
        (yield 1)
        (await pending)
    finally:
        (record "finally")
"""


NEVER_RETRIEVED = "Task exception was never retrieved"


IGNORED_BY_ACLOSE = (
    NEVER_RETRIEVED,
    RuntimeError,
    "async generator ignored GeneratorExit")


IGNORED = (
    RuntimeError,
    "async generator ignored GeneratorExit",
    None,
    AsyncGenerator)


CLEANUP_FAILED = (ValueError, "cleanup failed", None, AsyncGenerator)


class Pending:
    """
    An awaitable that suspends once and never resolves
    """

    def __await__(self):
        yield "pending"


class Resurrect:
    """
    Puts what it holds back in reach from its own finalizer
    """

    kept = []

    def __init__(self, obj):
        self.obj = obj
        self.me = self

    def __del__(self):
        Resurrect.kept.append(self.obj)


def finalized(obj):
    """
    obj, back from a collection that ran its finalizer
    """

    Resurrect(obj)
    del obj
    gc.collect()
    return Resurrect.kept.pop()


class Unraisables:
    """
    Collects sys.unraisablehook reports for the extent of a block, each
    as the exception type and message, the hook message, and the type
    of the object, so no report keeps the object alive
    """

    def __init__(self):
        self.seen = []

    def record(self, u):
        self.seen.append((
            type(u.exc_value),
            str(u.exc_value),
            u.err_msg,
            type(u.object)))

    def __enter__(self):
        self.prior = sys.unraisablehook
        sys.unraisablehook = self.record
        return self

    def __exit__(self, *exc):
        sys.unraisablehook = self.prior
        return False


class Box:
    tag = "finally"


def live_wrappers():
    gc.collect()
    return sum(1 for o in gc.get_objects() if isinstance(o, AsyncGenerator))


class TestAsyncGenFinalize(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        self.handled = []
        asyncio.get_running_loop().set_exception_handler(self.handle)

    def handle(self, loop, context):
        """
        Keep each loop exception report as its message and the
        exception's type and text
        """

        exc = context.get("exception")
        self.handled.append((context["message"], type(exc), str(exc)))

    async def settle(self):
        """
        Let the loop run the aclose task the finalizer hook scheduled,
        then collect, so a failed task reports to the loop
        """

        for _ in range(5):
            await asyncio.sleep(0)
        gc.collect()

    async def load(self):
        mod = new_module("asyncgen_finalize")
        vars(mod)["__file__"] = "<asyncgen_finalize>"
        await load_module_source_async(mod, SRC)
        return vars(mod)

    async def suspended(self, log, box):
        """
        A generator suspended at its yield inside try/finally, with box as
        its first argument; the finally reads box
        """

        mod = await self.load()
        agen = mod["produce"](box, log.append)
        self.assertEqual(await agen.__anext__(), 1)
        return agen

    async def test_dropped_suspended_generator_runs_its_finally(self):
        log = []
        with Unraisables() as reports:
            agen = await self.suspended(log, Box())
            del agen
            self.assertEqual(log, [])
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_suspended_generators_run_their_finally_each_time(self):
        """
        A generator of the same function after one was dropped reuses
        that generator's record; dropped suspended, it is closed the
        same way, finally body and all
        """

        log = []
        with Unraisables() as reports:
            for _ in range(3):
                agen = await self.suspended(log, Box())
                del agen
                await self.settle()
        self.assertEqual(log, ["finally"] * 3)
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_inside_a_cycle_runs_its_finally(self):
        """
        The generator's frame holds box in an argument slot, and box
        holds the generator: unreachable together, the finally body
        still reads box whole, which a close run from clearing could
        not promise
        """

        log = []
        box = Box()
        with Unraisables() as reports:
            agen = await self.suspended(log, box)
            box.agen = agen
            del agen, box
            gc.collect()
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_finally_that_awaits_does_not_keep_the_wrapper_alive(self):
        """
        The loop's aclose completes a finally that awaits, and the
        wrapper goes with the task that closed it
        """

        mod = await self.load()
        log = []
        before = live_wrappers()
        with Unraisables() as reports:
            for _ in range(5):
                agen = mod["waits"](log.append)
                self.assertEqual(await agen.__anext__(), 1)
                del agen
            await self.settle()
        self.assertEqual(live_wrappers(), before)
        self.assertEqual(log, ["finally"] * 5)
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_generator_whose_finally_awaits_completes(self):
        """
        A finally that awaits completes in the aclose the loop runs for
        the dropped generator
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["waits"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
            self.assertEqual(log, [])
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_generator_whose_finally_yields_is_reported(self):
        """
        A finally that yields fails the loop's aclose with an ignored
        GeneratorExit, and the failed task goes to the loop's handler
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["yields_again"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
            await self.settle()
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [IGNORED_BY_ACLOSE])

    async def test_break_from_generator_whose_finally_awaits_completes(self):
        """
        A for-each that breaks lets go of its generator, which the loop
        closes as it closes any dropped one
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            self.assertEqual(await mod["break_waits"](log.append), "ok")
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_sync_view_whose_finally_awaits_completes(self):
        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["waits"](log.append)
            view = iter(agen)
            self.assertEqual(next(view), 1)
            del view, agen
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def mid_await(self, name, log):
        """
        A generator of the named function stopped mid-await: its first
        value taken, then a stepper sent into the await that never
        resolves
        """

        mod = await self.load()
        agen = mod[name](Pending(), log.append)
        self.assertEqual(await agen.__anext__(), 1)
        stepper = agen.__anext__()
        self.assertEqual(stepper.send(None), "pending")
        return agen, stepper

    async def test_dropped_mid_await_whose_cleanup_awaits_is_reported(self):
        """
        Dropped with its stepper mid-await, the generator closes at its
        drop, the loop's hook or not: nothing can resume the step
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_awaits", log)
            del stepper, agen
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_dropped_mid_await_whose_cleanup_raises_is_reported(self):
        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_raises", log)
            del stepper, agen
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [CLEANUP_FAILED])
        self.assertEqual(self.handled, [])

    async def test_dropped_stepper_leaves_the_generator_suspended(self):
        """
        Dropping the stepper alone closes nothing: the generator stays
        suspended mid-await, as CPython's does when an asend awaitable
        is dropped, and closes from its own finalizer when it is
        dropped, reported against the generator
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_awaits", log)
            del stepper
            self.assertEqual(log, [])
            self.assertEqual(reports.seen, [])
            del agen
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_suspended_generator_refuses_each_method(self):
        """
        A generator left mid-await by a dropped stepper is still
        running: anext, asend, athrow and aclose each refuse it with
        CPython's text, and none of them runs its cleanup; its drop
        does
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_plain", log)
            del stepper
            refusals = (
                ("anext", agen.__anext__),
                ("anext", lambda: agen.asend(None)),
                ("athrow", lambda: agen.athrow(ValueError)),
                ("aclose", agen.aclose))
            for method, make in refusals:
                with self.subTest(method=method):
                    with self.assertRaises(RuntimeError) as caught:
                        await make()
                    self.assertEqual(
                        str(caught.exception),
                        f"{method}(): asynchronous generator is already"
                        " running")
            del refusals, make, caught
            self.assertEqual(log, [])
            self.assertEqual(reports.seen, [])
            del agen
            self.assertEqual(log, ["finally"])
            await self.settle()
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_suspended_generator_cleanup_raises_at_its_drop(self):
        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_raises", log)
            del stepper
            self.assertEqual(log, [])
            del agen
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [CLEANUP_FAILED])
        self.assertEqual(self.handled, [])

    async def test_suspended_generator_in_a_cycle_closes_when_collected(self):
        """
        The stepper dropped first, the generator then unreachable in a
        cycle: the collector runs its finalizer, which closes it
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_awaits", log)
            del stepper
            self.assertEqual(log, [])
            box = [agen]
            box.append(box)
            del agen, box
            gc.collect()
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_closed_mid_await_stepper_raises_ignored_exit(self):
        """
        Closing the stepper mid-await closes the generator; a cleanup
        that awaits again is raised to the caller of close
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_awaits", log)
            with self.assertRaises(RuntimeError) as caught:
                stepper.close()
        self.assertEqual(
            str(caught.exception), "async generator ignored GeneratorExit")
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(reports.seen, [])
        with self.assertRaises(StopAsyncIteration):
            await agen.__anext__()

    async def test_closed_mid_await_stepper_raises_cleanup_error(self):
        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_raises", log)
            with self.assertRaises(ValueError) as caught:
                stepper.close()
        self.assertEqual(str(caught.exception), "cleanup failed")
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(reports.seen, [])
        with self.assertRaises(StopAsyncIteration):
            await agen.__anext__()

    async def test_dropped_transform_mid_await_is_reported(self):
        """
        A transform stepped into its source's await and dropped closes
        its chain from the top: the generator on it finishes with
        GeneratorExit at the transform's drop, reported against the
        generator
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_awaits"](Pending(), log.append)
            transform = KosMap(str, agen)
            del agen
            self.assertEqual(await transform.__anext__(), "1")
            stepper = transform.__anext__()
            self.assertEqual(stepper.send(None), "pending")
            del stepper, transform
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_dropped_transform_closes_a_held_generator(self):
        """
        A transform dropped mid-await closes the generator on its chain
        even while it is held elsewhere: the step was the transform's,
        so its cleanup runs at the transform's drop, and a later anext
        finds the generator closed
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_awaits"](Pending(), log.append)
            transform = KosMap(str, agen)
            self.assertEqual(await transform.__anext__(), "1")
            stepper = transform.__anext__()
            self.assertEqual(stepper.send(None), "pending")
            del stepper, transform
            self.assertEqual(log, ["fin-start"])
            self.assertEqual(reports.seen, [IGNORED])
            with self.assertRaises(StopAsyncIteration):
                await agen.__anext__()
            del agen
            await self.settle()
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_dropped_transform_stepper_leaves_the_step_in_flight(self):
        """
        Dropping a transform's stepper closes nothing: the transform
        refuses a later step while its step is in flight, and its drop
        closes the generator on its chain
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_awaits"](Pending(), log.append)
            transform = KosMap(str, agen)
            del agen
            self.assertEqual(await transform.__anext__(), "1")
            stepper = transform.__anext__()
            self.assertEqual(stepper.send(None), "pending")
            del stepper
            self.assertEqual(log, [])
            self.assertEqual(reports.seen, [])
            with self.assertRaises(ValueError) as caught:
                await transform.__anext__()
            self.assertEqual(
                str(caught.exception), "map is already being iterated")
            self.assertEqual(log, [])
            del transform, caught
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_dropped_coroutine_closes_a_held_generator(self):
        """
        A coroutine dropped while the generator it iterates awaits
        closes its chain from the top on the demand path: the
        generator's cleanup runs at the coroutine's drop, and its await
        is refused in-eval with KoskariSyncViolation, which unwinds
        through the generator. The generator, held elsewhere, ends
        closed: a later anext finds it exhausted, its own finalizer and
        the loop's hook have nothing left to do, and the close is
        reported once, against the coroutine
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_awaits"](Pending(), log.append)
            coro = mod["consume"](agen)
            coro_type = type(coro)
            self.assertEqual(coro.send(None), "pending")
            del coro
            self.assertEqual(log, ["fin-start"])
            with self.assertRaises(StopAsyncIteration):
                await agen.__anext__()
            del agen
            await self.settle()
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(
            reports.seen,
            [(KoskariSyncViolation,
              "async suspension is not allowed in demand_sync",
              None, coro_type)])
        self.assertEqual(self.handled, [])

    async def test_mid_await_in_a_cycle_runs_its_cleanup_and_is_reported(self):
        """
        The generator and its stepper unreachable together: the cleanup
        runs before the collector clears anything, and is reported
        """

        log = []
        with Unraisables() as reports:
            agen, stepper = await self.mid_await("mid_await_awaits", log)
            box = [agen, stepper]
            box.append(box)
            del agen, stepper, box
            gc.collect()
            self.assertEqual(log, ["fin-start"])
            await self.settle()
        self.assertEqual(reports.seen, [IGNORED])
        self.assertEqual(self.handled, [])

    async def test_stepper_finalized_before_its_await_is_not_closed_again(self):
        """
        A stepper whose finalizer ran while it was primed, then sent
        into an await and dropped, is dead: a finalizer runs once, so
        its dealloc closes nothing
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_plain"](Pending(), log.append)
            self.assertEqual(await agen.__anext__(), 1)
            stepper = finalized(agen.__anext__())
            self.assertEqual(stepper.send(None), "pending")
            del stepper
            self.assertEqual(log, [])
            del agen
        self.assertEqual(reports.seen, [])

    async def test_stepper_finalized_before_its_await_runs_nothing_from_a_clear(self):
        """
        The same dead stepper reached only from a garbage cycle: the
        collector's clear releases it, and no cleanup runs from there
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["mid_await_plain"](Pending(), log.append)
            self.assertEqual(await agen.__anext__(), 1)
            stepper = finalized(agen.__anext__())
            self.assertEqual(stepper.send(None), "pending")
            box = [stepper]
            box.append(box)
            del stepper, box
            gc.collect()
            self.assertEqual(log, [])
        self.assertEqual(reports.seen, [])

    @unittest.skipIf(
        sys.version_info < (3, 11),
        "the caller's value stack keeps a reference to a call argument"
        " until the call returns, so finalized() cannot collect it")
    async def test_finalized_unstarted_generator_is_closed(self):
        """
        A generator finalized before it was started is closed by its
        finalizer, as CPython's is, so a later step finds it exhausted
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = finalized(mod["produce"](Box(), log.append))
            with self.assertRaises(StopAsyncIteration):
                await agen.__anext__()
        self.assertEqual(log, [])
        self.assertEqual(reports.seen, [])


# The end.
