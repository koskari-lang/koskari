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
tests.lib.test_asyncgen_hooks

A Koskari async generator honours the PEP 525 hooks as CPython's does:
firstiter sees it on its first method call or step, and a generator
dropped at rest is handed to the finalizer hook in place of the
synchronous close, which under an event loop runs its aclose as a task.
One left mid-await still closes at its drop

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest
import weakref

import pytest

from koskari.lib import AsyncGenerator, KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
import-from asyncio sleep

define [produce record]:
    try:
        (yield 1)
        (yield 2)
    finally:
        (record "finally")

define [waits record]:
    try:
        (yield 1)
    finally:
        (await (sleep 0))
        (record "finally")

define [mid_await_plain pending record]:
    try:
        (yield 1)
        (await pending)
    finally:
        (record "finally")

define [consume source]:
    var total 0
    (for-each [x source] (setq total (+ total x)))
    total
"""


IGNORED = (
    RuntimeError,
    "async generator ignored GeneratorExit",
    None,
    AsyncGenerator)


class Pending:
    """
    An awaitable that suspends once and never resolves
    """

    def __await__(self):
        yield "pending"


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


def step(awaitable):
    """
    Drive an awaitable one send, without a loop
    """

    try:
        return ("yield", awaitable.send(None))
    except StopIteration as e:
        return ("stop", e.value)


class Hooks:
    """
    Installs firstiter and finalizer spies for the extent of a block,
    restoring the hooks in force before it. The firstiter spy holds a
    weak reference to each generator it sees, as asyncio's does; the
    finalizer spy keeps each generator it is handed, as an event loop's
    scheduled aclose would
    """

    def __init__(self, firstiter=True, finalizer=True):
        self.seen_first = []
        self.finalized = []
        self.use_first = firstiter
        self.use_finalizer = finalizer

    @property
    def first(self):
        return [ref() for ref in self.seen_first]

    def firstiter(self, agen):
        self.seen_first.append(weakref.ref(agen))

    def finalizer(self, agen):
        self.finalized.append(agen)

    def __enter__(self):
        self.prior = sys.get_asyncgen_hooks()
        sys.set_asyncgen_hooks(
            firstiter=self.firstiter if self.use_first else None,
            finalizer=self.finalizer if self.use_finalizer else None)
        return self

    def __exit__(self, *exc):
        sys.set_asyncgen_hooks(*self.prior)
        return False


class TestAsyncGenHooksNoLoop(unittest.TestCase):
    """
    The hooks driven by hand, with no event loop running
    """

    def setUp(self):
        loop = asyncio.new_event_loop()
        try:
            mod = new_module("asyncgen_hooks")
            vars(mod)["__file__"] = "<asyncgen_hooks>"
            loop.run_until_complete(load_module_source_async(mod, SRC))
        finally:
            loop.close()
        self.ns = vars(mod)

    def test_firstiter_sees_the_first_anext_once(self):
        log = []
        with Hooks() as hooks:
            agen = self.ns["produce"](log.append)
            self.assertEqual(hooks.first, [])
            self.assertEqual(step(agen.__anext__()), ("stop", 1))
            self.assertEqual(hooks.first, [agen])
            self.assertEqual(step(agen.__anext__()), ("stop", 2))
            with self.assertRaises(StopAsyncIteration):
                step(agen.asend(None))
            self.assertEqual(hooks.first, [agen])
        self.assertEqual(log, ["finally"])

    def test_firstiter_sees_each_method_call_before_its_await(self):
        """
        Each protocol method reads the hooks when it makes its
        awaitable, before anything awaits it
        """

        log = []
        makers = (
            ("anext", lambda agen: agen.__anext__()),
            ("asend", lambda agen: agen.asend(None)),
            ("athrow", lambda agen: agen.athrow(ValueError)),
            ("aclose", lambda agen: agen.aclose()))
        for method, make in makers:
            with self.subTest(method=method):
                with Hooks(finalizer=False) as hooks:
                    agen = self.ns["produce"](log.append)
                    awaitable = make(agen)
                    self.assertEqual(hooks.first, [agen])
                    awaitable.close()
                    make(agen).close()
                    self.assertEqual(hooks.first, [agen])
                del agen, awaitable, hooks

    def test_firstiter_error_fails_the_first_call(self):
        def refuse(agen):
            raise ValueError("refused")

        log = []
        prior = sys.get_asyncgen_hooks()
        sys.set_asyncgen_hooks(firstiter=refuse, finalizer=None)
        try:
            agen = self.ns["produce"](log.append)
            with self.assertRaises(ValueError) as caught:
                agen.__anext__()
            self.assertEqual(str(caught.exception), "refused")
            self.assertEqual(step(agen.__anext__()), ("stop", 1))
        finally:
            sys.set_asyncgen_hooks(*prior)

    def test_firstiter_sees_a_koskari_consumers_first_step(self):
        log = []
        with Hooks(finalizer=False) as hooks:
            agen = self.ns["produce"](log.append)
            self.assertEqual(step(self.ns["consume"](agen)), ("stop", 3))
            self.assertEqual(hooks.first, [agen])
        self.assertEqual(log, ["finally"])

    def test_dropped_generator_goes_to_the_finalizer_hook(self):
        """
        The dropped generator is handed to the finalizer hook and not
        closed; the hook's aclose then runs the cleanup
        """

        log = []
        with Unraisables() as reports:
            with Hooks() as hooks:
                agen = self.ns["produce"](log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                ref = weakref.ref(agen)
                del agen
                self.assertEqual(log, [])
                self.assertEqual(len(hooks.finalized), 1)
                agen = hooks.finalized.pop()
                self.assertIs(ref(), None)
                self.assertEqual(step(agen.aclose()), ("stop", None))
            self.assertEqual(log, ["finally"])
            del agen
        self.assertEqual(reports.seen, [])

    def test_finalizer_hook_runs_once(self):
        """
        A finalizer runs once: the generator the hook kept, dropped
        again without a close, is not handed over a second time
        """

        log = []
        with Unraisables() as reports:
            with Hooks() as hooks:
                agen = self.ns["produce"](log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                del agen
                self.assertEqual(len(hooks.finalized), 1)
                hooks.finalized.clear()
                gc.collect()
                self.assertEqual(hooks.finalized, [])
            self.assertEqual(log, [])
        self.assertEqual(reports.seen, [])

    def test_unstarted_generator_with_hooks_goes_to_the_finalizer_hook(self):
        """
        A generator whose hooks were read but that never ran is still
        unfinished, and goes to the hook as CPython's does
        """

        log = []
        with Hooks() as hooks:
            agen = self.ns["produce"](log.append)
            awaitable = agen.__anext__()
            del awaitable, agen
            self.assertEqual(len(hooks.finalized), 1)
            agen = hooks.finalized.pop()
            self.assertEqual(step(agen.aclose()), ("stop", None))
            with self.assertRaises(StopAsyncIteration):
                step(agen.__anext__())
        self.assertEqual(log, [])

    def test_finalizer_hook_error_is_reported(self):
        def refuse(agen):
            raise ValueError("refused")

        log = []
        prior = sys.get_asyncgen_hooks()
        sys.set_asyncgen_hooks(firstiter=None, finalizer=refuse)
        try:
            with Unraisables() as reports:
                agen = self.ns["produce"](log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                del agen
        finally:
            sys.set_asyncgen_hooks(*prior)
        self.assertEqual(log, [])
        self.assertEqual(
            reports.seen,
            [(ValueError, "refused", None, AsyncGenerator)])

    def test_no_hooks_close_synchronously_and_report(self):
        """
        With no finalizer hook the dropped generator closes at the drop;
        a cleanup that awaits is reported as an ignored GeneratorExit
        """

        log = []
        with Hooks(firstiter=False, finalizer=False):
            with Unraisables() as reports:
                agen = self.ns["produce"](log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                del agen
                self.assertEqual(log, ["finally"])
                agen = self.ns["waits"](log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                del agen
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [IGNORED])

    def test_no_hooks_close_a_generator_left_mid_await_at_the_drop(self):
        log = []
        with Hooks(firstiter=False, finalizer=False):
            with Unraisables() as reports:
                agen = self.ns["mid_await_plain"](Pending(), log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                stepper = agen.__anext__()
                self.assertEqual(step(stepper), ("yield", "pending"))
                del stepper
                self.assertEqual(log, [])
                del agen
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])

    def test_generator_left_mid_await_bypasses_the_finalizer_hook(self):
        """
        Nothing can resume a step left mid-await, and an aclose would be
        refused as already running: the generator closes at its drop,
        and the finalizer hook is not handed it
        """

        log = []
        with Hooks() as hooks:
            with Unraisables() as reports:
                agen = self.ns["mid_await_plain"](Pending(), log.append)
                self.assertEqual(step(agen.__anext__()), ("stop", 1))
                stepper = agen.__anext__()
                self.assertEqual(step(stepper), ("yield", "pending"))
                del stepper, agen
            self.assertEqual(hooks.finalized, [])
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])


class TestAsyncGenHooksUnderLoop(unittest.IsolatedAsyncioTestCase):
    """
    The hooks asyncio installs while its loop runs
    """

    async def asyncSetUp(self):
        mod = new_module("asyncgen_hooks_loop")
        vars(mod)["__file__"] = "<asyncgen_hooks_loop>"
        await load_module_source_async(mod, SRC)
        self.ns = vars(mod)
        self.handled = []
        loop = asyncio.get_running_loop()
        loop.set_exception_handler(self.handle)

    def handle(self, loop, context):
        exc = context.get("exception")
        self.handled.append((context["message"], type(exc), str(exc)))

    async def settle(self):
        """
        Let the loop run the aclose task a finalizer hook scheduled
        """

        for _ in range(5):
            await asyncio.sleep(0)

    async def test_dropped_generator_whose_cleanup_awaits_completes(self):
        log = []
        with Unraisables() as reports:
            agen = self.ns["waits"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
            self.assertEqual(log, [])
            await self.settle()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_dropped_generator_consumed_from_koskari_completes(self):
        """
        A generator a Koskari consumer stepped reads the hooks on its
        first step: a for-each runs it to the end, and one a transform
        let go of suspended is the loop's to close
        """

        log = []
        with Unraisables() as reports:
            agen = self.ns["mid_await_plain"](asyncio.sleep(0), log.append)
            self.assertEqual(await self.ns["consume"](agen), 1)
            self.assertEqual(log, ["finally"])
            agen = self.ns["waits"](log.append)
            transform = KosMap(str, agen)
            del agen
            self.assertEqual(await transform.__anext__(), "1")
            del transform
            self.assertEqual(log, ["finally"])
            await self.settle()
        self.assertEqual(log, ["finally", "finally"])
        self.assertEqual(reports.seen, [])

    async def test_generator_left_mid_await_closes_at_its_drop(self):
        """
        A generator left mid-await by a dropped awaitable closes at its
        own drop, not through the loop
        """

        log = []
        with Unraisables() as reports:
            agen = self.ns["mid_await_plain"](Pending(), log.append)
            self.assertEqual(await agen.__anext__(), 1)
            stepper = agen.__anext__()
            self.assertEqual(stepper.send(None), "pending")
            del stepper, agen
            self.assertEqual(log, ["finally"])
            await self.settle()
            gc.collect()
        self.assertEqual(reports.seen, [])
        self.assertEqual(self.handled, [])

    async def test_shutdown_asyncgens_closes_a_live_generator(self):
        log = []
        agen = self.ns["waits"](log.append)
        self.assertEqual(await agen.__anext__(), 1)
        await asyncio.get_running_loop().shutdown_asyncgens()
        self.assertEqual(log, ["finally"])
        with self.assertRaises(StopAsyncIteration):
            await agen.__anext__()


# The end.
