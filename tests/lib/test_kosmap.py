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
Tests for KosMap park ownership: a parked map refuses every fresh
entry, and only the park's owner resumes it

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.lib import KosMap


pytestmark = pytest.mark.phase_lib


class TestKosMapParkOwnership(unittest.IsolatedAsyncioTestCase):
    """
    While a map step is parked on an awaitable, concurrent entries are
    refused rather than being misread as the awaited result
    """

    def make_parked(self):
        release = asyncio.Event()

        async def slow(x):
            await release.wait()
            return x + 1

        return KosMap(slow, [1, 2]), release

    async def test_concurrent_anext_refused(self):
        m, release = self.make_parked()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await m.__anext__()

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_asend_value_refused(self):
        m, release = self.make_parked()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        # the injected value must NOT be consumed as the awaited result
        with self.assertRaises(RuntimeError):
            await m.asend(999)

        release.set()
        self.assertEqual(await first, 2)

    async def test_parked_step_resumes_cleanly(self):
        m, release = self.make_parked()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)
        release.set()
        self.assertEqual(await first, 2)
        self.assertEqual(await m.__anext__(), 3)
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()

    async def test_concurrent_athrow_refused(self):
        m, release = self.make_parked()
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await m.athrow(ValueError("intruder"))

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_aclose_refused(self):
        m, release = self.make_parked()
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


# The end.
