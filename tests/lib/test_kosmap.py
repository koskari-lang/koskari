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

from koskari.lib import KosMap

import pytest

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

    async def test_fresh_asend_non_none_rejected(self):
        m = KosMap(abs, [-1])
        with self.assertRaises(TypeError):
            await m.asend(5)
        self.assertEqual(await m.__anext__(), 1)


# The end.
