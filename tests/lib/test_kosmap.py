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
Tests for KosMap suspension ownership: a suspended map refuses every
fresh entry, and only the suspension's owner resumes it

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest
import weakref

import pytest

from koskari.lib import Domain, FormalsSpec, KosMap
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class TestKosMapSuspendedStepOwnership(unittest.IsolatedAsyncioTestCase):
    """
    While a map step is suspended on an awaitable, concurrent entries are
    refused rather than being misread as the awaited result
    """

    def make_suspended(self):
        release = asyncio.Event()

        async def slow(x):
            await release.wait()
            return x + 1

        return KosMap(slow, [1, 2]), release

    async def test_concurrent_anext_refused(self):
        m, release = self.make_suspended()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await m.__anext__()

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_asend_value_refused(self):
        m, release = self.make_suspended()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        # the injected value must NOT be consumed as the awaited result
        with self.assertRaises(RuntimeError):
            await m.asend(999)

        release.set()
        self.assertEqual(await first, 2)

    async def test_suspended_step_resumes_cleanly(self):
        m, release = self.make_suspended()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)
        release.set()
        self.assertEqual(await first, 2)
        self.assertEqual(await m.__anext__(), 3)
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()

    async def test_concurrent_athrow_refused(self):
        m, release = self.make_suspended()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await m.athrow(ValueError("intruder"))

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_aclose_refused(self):
        m, release = self.make_suspended()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await m.aclose()

        release.set()
        self.assertEqual(await first, 2)

    async def test_fresh_asend_non_none_rejected(self):
        m = KosMap(abs, [-1])
        with self.assertRaises(TypeError):
            await m.asend(5)
        self.assertEqual(await m.__anext__(), 1)


class TestKosMapSourceErrors(unittest.IsolatedAsyncioTestCase):
    """
    A source error propagates out of the map like any other exception;
    the map keeps its state, and whether a retry works is the source's
    own business
    """

    class FlakyIter:
        def __init__(self):
            self.pulls = 0

        def __iter__(self):
            return self

        def __next__(self):
            self.pulls += 1
            if self.pulls == 1:
                raise ValueError("transient")
            if self.pulls <= 3:
                return self.pulls
            raise StopIteration

    async def test_source_error_propagates_and_retry_is_sources_call(self):
        m = KosMap(str, self.FlakyIter())
        with self.assertRaises(ValueError):
            await m.__anext__()
        # the source recovered; the map simply asks it again
        self.assertEqual(await m.__anext__(), "2")
        self.assertEqual(await m.__anext__(), "3")
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()

    async def test_shared_iterator_interleaves_with_direct_pulls(self):
        """
        the map holds a plain reference to its acquired iterator; a
        consumer pulling the same iterator directly interleaves with
        pulls through the map, one step at a time
        """
        it = iter([1, 2, 3, 4])
        m = KosMap(str, it)
        self.assertEqual(await m.__anext__(), "1")
        self.assertEqual(next(it), 2)
        self.assertEqual(await m.__anext__(), "3")
        self.assertEqual(next(it), 4)
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()

    async def test_source_error_propagates_on_demand_drive(self):
        m = KosMap(str, self.FlakyIter())
        it = iter(m)
        with self.assertRaises(ValueError):
            next(it)
        self.assertEqual(next(it), "2")


class TestKosMapSourceCleanup(unittest.IsolatedAsyncioTestCase):
    """
    Closing a map runs its source's cleanup: aclose for async sources,
    close for sync generators, in both the async and demand surfaces
    """

    def make_async_source(self, log):
        async def src():
            try:
                yield 1
                yield 2
            finally:
                log.append("cleaned")
        return src()

    async def test_aclose_runs_async_source_finally(self):
        log = []
        m = KosMap(str, self.make_async_source(log))
        self.assertEqual(await m.__anext__(), "1")
        self.assertIsNone(await m.aclose())
        self.assertEqual(log, ["cleaned"])

    async def test_aclose_idempotent(self):
        log = []
        m = KosMap(str, self.make_async_source(log))
        self.assertEqual(await m.__anext__(), "1")
        self.assertIsNone(await m.aclose())
        self.assertIsNone(await m.aclose())
        self.assertEqual(log, ["cleaned"])
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()

    async def test_sync_close_runs_async_source_finally(self):
        log = []
        m = KosMap(str, self.make_async_source(log))
        self.assertEqual(await m.__anext__(), "1")
        dgen = iter(m)
        dgen.close()
        self.assertEqual(log, ["cleaned"])

    async def test_close_runs_sync_generator_close(self):
        log = []

        def src():
            try:
                yield 1
                yield 2
            finally:
                log.append("cleaned")

        m = KosMap(str, src())
        self.assertEqual(await m.__anext__(), "1")
        self.assertIsNone(await m.aclose())
        self.assertEqual(log, ["cleaned"])

    def test_suspending_cleanup_releases_yield_once(self):
        # the source's finally suspends on an awaitable, so the close
        # falls back to closing the source; the value it yielded keeps
        # its references. keep holds a second reference so an
        # over-release lowers the count instead of freeing the object.
        token = object()
        keep = [token]

        class Suspend:
            def __await__(self):
                yield token

        async def src():
            try:
                yield 1
            finally:
                await Suspend()

        m = KosMap(str, src())
        dgen = iter(m)
        self.assertEqual(next(dgen), "1")
        before = sys.getrefcount(token)
        dgen.close()
        self.assertEqual(sys.getrefcount(token), before)
        self.assertEqual(keep, [token])


class TestKosMapAbandonedMidAwait(unittest.IsolatedAsyncioTestCase):
    """
    A map and the step awaiting on its context can be dropped together
    in one garbage cycle. Whichever of them the collector clears first,
    the awaited coroutine is closed so its cleanup runs, and nothing
    stays uncollectable
    """

    async def test_cycle_with_suspended_step_collects_and_closes(self):
        marks = []

        async def slow(x):
            # a future nothing else holds, so the test's own references
            # are the only roots of the graph built below
            fut = asyncio.get_running_loop().create_future()
            try:
                await fut
                return x + 1
            finally:
                marks.append("closed")

        m = KosMap(slow, [1, 2])
        step = m.__anext__().__await__()
        # drive the step until it suspends on the future
        suspended = step.send(None)
        # the future the map's context awaits now refers back to the
        # step, closing the cycle: step -> map -> context -> slow ->
        # future -> step
        suspended.back_edge = step

        # a leaf hanging off the cycle: it dies with the cycle and can
        # be watched, where the map and the step cannot be weakly held
        class Canary:
            pass

        suspended.canary = Canary()
        canary_ref = weakref.ref(suspended.canary)

        # a second cycle holding the step, made after the map so the
        # collector clears the map first and the step outlives the
        # map's release of their shared context
        keeper = [step]
        keeper.append(keeper)

        del m, step, suspended, keeper
        gc.collect()

        self.assertIsNone(canary_ref())
        self.assertEqual(marks, ["closed"])


class TestKosMapRecycledContext(unittest.TestCase):
    """
    A map drives each step on one durable context and recycles it when
    the step completes at its root. The recycled context carries
    nothing of the finished step: no chain, no acting namespace
    """

    def test_recycled_context_reports_no_frozen_state(self):
        seen = []

        def capture():
            seen.append(Domain.current_domain().get_contexts()[0])
            return 7

        # a Koskari function of one parameter that calls capture, so the
        # map's context drives a frame and capture sees it running
        code, base_ns = build_expr([
            ["CONST", capture],
            ["CALL", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        fn = code.function(FormalsSpec(("x",), None, ())(), base_ns)

        m = KosMap(fn, [1, 2, 3])
        it = iter(m)
        self.assertEqual(next(it), 7)

        ctx = seen[0]
        self.assertFalse(ctx.running)
        self.assertIsNone(ctx.namespace)
        self.assertEqual(ctx.snapshot()["dynnodes"], [])

        # and the next step runs on the same, cleared context
        self.assertEqual(next(it), 7)
        self.assertIs(seen[1], ctx)


# The end.
