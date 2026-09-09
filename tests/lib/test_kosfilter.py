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
tests.lib.test_kosfilter

KosFilter behavior at the python surface: predicate semantics (None,
python callables, awaitable verdicts), the sync fast path, in-flight
step ownership, source error propagation, and source cleanup

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.lib import KosFilter


pytestmark = pytest.mark.phase_lib


class TestKosFilterBasics(unittest.IsolatedAsyncioTestCase):
    """
    filter semantics match builtins.filter: a None predicate keeps
    truthy elements, a callable predicate's verdict decides, skipped
    elements never emit
    """

    async def test_none_predicate_keeps_truthy(self):
        f = KosFilter(None, [0, 1, "", "x", [], [2], None, 0.0, 5])
        self.assertEqual(list(f), [1, "x", [2], 5])

    async def test_python_predicate(self):
        f = KosFilter(lambda x: x % 2, range(10))
        self.assertEqual(list(f), [1, 3, 5, 7, 9])

    async def test_async_drive(self):
        f = KosFilter(lambda x: x > 2, [1, 2, 3, 4, 5])
        self.assertEqual([x async for x in f], [3, 4, 5])

    async def test_all_rejected_is_empty(self):
        self.assertEqual(list(KosFilter(lambda x: False, [1, 2, 3])), [])

    async def test_exhaustion_is_clean(self):
        f = KosFilter(None, [1, 0, 2])
        self.assertEqual(list(f), [1, 2])
        self.assertEqual(list(f), [])
        self.assertEqual([x async for x in f], [])

    async def test_awaitable_predicate_verdict(self):
        async def pred(x):
            await asyncio.sleep(0)
            return x % 3 == 0

        f = KosFilter(pred, range(10))
        self.assertEqual([x async for x in f], [0, 3, 6, 9])

    async def test_non_callable_predicate_rejected(self):
        with self.assertRaises(TypeError):
            KosFilter(5, [1, 2])

    async def test_no_keyword_arguments(self):
        with self.assertRaises(TypeError):
            KosFilter(None, [1], extra=True)

    async def test_predicate_error_propagates(self):
        def pred(x):
            raise ValueError("bad verdict")

        f = KosFilter(pred, [1, 2])
        with self.assertRaises(ValueError):
            list(f)


class TestKosFilterParkOwnership(unittest.IsolatedAsyncioTestCase):
    """
    While a filter step is parked on an awaitable verdict, concurrent
    entries are refused rather than being misread as the awaited result
    """

    def make_parked(self):
        release = asyncio.Event()

        async def slow(x):
            await release.wait()
            return x > 1

        return KosFilter(slow, [1, 2]), release

    async def test_concurrent_anext_refused(self):
        f, release = self.make_parked()
        first = asyncio.ensure_future(f.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await f.__anext__()

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_athrow_refused(self):
        f, release = self.make_parked()
        first = asyncio.ensure_future(f.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await f.athrow(ValueError("intruder"))

        release.set()
        self.assertEqual(await first, 2)

    async def test_concurrent_aclose_refused(self):
        f, release = self.make_parked()
        first = asyncio.ensure_future(f.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(RuntimeError):
            await f.aclose()

        release.set()
        self.assertEqual(await first, 2)

    async def test_fresh_asend_non_none_rejected(self):
        f = KosFilter(None, [1])
        with self.assertRaises(TypeError):
            await f.asend(5)
        self.assertEqual(await f.__anext__(), 1)


class TestKosFilterSourceErrors(unittest.IsolatedAsyncioTestCase):
    """
    A source error propagates out of the filter like any other
    exception; the filter keeps its state, and whether a retry works is
    the source's own business
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
        f = KosFilter(None, self.FlakyIter())
        with self.assertRaises(ValueError):
            await f.__anext__()
        self.assertEqual(await f.__anext__(), 2)
        self.assertEqual(await f.__anext__(), 3)
        with self.assertRaises(StopAsyncIteration):
            await f.__anext__()

    async def test_shared_iterator_interleaves_with_direct_pulls(self):
        it = iter([1, 2, 3, 4])
        f = KosFilter(None, it)
        self.assertEqual(await f.__anext__(), 1)
        self.assertEqual(next(it), 2)
        self.assertEqual(await f.__anext__(), 3)
        self.assertEqual(next(it), 4)
        with self.assertRaises(StopAsyncIteration):
            await f.__anext__()

    async def test_source_error_propagates_on_demand_drive(self):
        f = KosFilter(None, self.FlakyIter())
        it = iter(f)
        with self.assertRaises(ValueError):
            next(it)
        self.assertEqual(next(it), 2)


class TestKosFilterSourceCleanup(unittest.IsolatedAsyncioTestCase):
    """
    Closing a filter runs its source's cleanup: aclose for async
    sources, close for sync generators, in both surfaces
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
        f = KosFilter(None, self.make_async_source(log))
        self.assertEqual(await f.__anext__(), 1)
        self.assertIsNone(await f.aclose())
        self.assertEqual(log, ["cleaned"])

    async def test_aclose_idempotent(self):
        log = []
        f = KosFilter(None, self.make_async_source(log))
        self.assertEqual(await f.__anext__(), 1)
        self.assertIsNone(await f.aclose())
        self.assertIsNone(await f.aclose())
        self.assertEqual(log, ["cleaned"])
        with self.assertRaises(StopAsyncIteration):
            await f.__anext__()

    async def test_close_runs_sync_generator_close(self):
        log = []

        def src():
            try:
                yield 1
                yield 2
            finally:
                log.append("cleaned")

        f = KosFilter(None, src())
        self.assertEqual(await f.__anext__(), 1)
        self.assertIsNone(await f.aclose())
        self.assertEqual(log, ["cleaned"])


# The end.
