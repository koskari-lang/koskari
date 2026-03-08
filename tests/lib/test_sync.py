# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
Unit tests for run_sync and iter_sync

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import unittest

from koskari.lib import run_sync, iter_sync


class TestRunSync(unittest.IsolatedAsyncioTestCase):
    """Tests for run_sync"""

    async def test_run_sync_no_loop(self):
        """run_sync(asyncio.sleep(0)) returns None when no loop in thread"""

        result = run_sync(asyncio.sleep(0))
        self.assertIsNone(result)

    async def test_run_sync_returns_value(self):
        """run_sync returns the awaited value"""

        async def f():
            return 42

        self.assertEqual(run_sync(f()), 42)

    async def test_run_sync_propagates_exception(self):
        """run_sync propagates exception from awaitable"""

        async def f():
            raise ValueError("expected")

        with self.assertRaises(ValueError) as cm:
            run_sync(f())
        self.assertIn("expected", str(cm.exception))


class TestIterSync(unittest.IsolatedAsyncioTestCase):
    """Tests for iter_sync"""

    async def test_iter_sync_no_loop(self):
        """iter_sync yields values from async generator when no loop"""

        async def gen():
            yield 1
            yield 2
            yield 3

        self.assertEqual(list(iter_sync(gen())), [1, 2, 3])

    async def test_iter_sync_empty(self):
        """iter_sync over empty async generator returns empty list"""

        async def gen():
            return
            yield

        self.assertEqual(list(iter_sync(gen())), [])

    async def test_iter_sync_propagates_exception(self):
        """iter_sync propagates exception from async generator"""

        async def gen():
            yield 1
            raise ValueError("boom")

        it = iter_sync(gen())
        self.assertEqual(next(it), 1)
        with self.assertRaises(ValueError) as cm:
            next(it)
        self.assertIn("boom", str(cm.exception))


# The end.
