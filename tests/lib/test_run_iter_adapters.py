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
Tests for KosCoroutine.run() and KosPyAsyncGen.iter() sync adapters.

These adapters use asyncio.run() under the hood and must not be used when
an event loop is already running. The tests here run in a synchronous
context only.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari.lib import AsyncGenerator
from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestCoroutineRun(unittest.IsolatedAsyncioTestCase):
    """
    KosCoroutine.run() behaves like asyncio.run(coro)
    """

    async def test_run_returns_same_as_asyncio_run(self):
        """
        expr().run() returns the same value as asyncio.run(expr())
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertEqual(coro.run(), 42)
        loop = asyncio.get_running_loop()
        result = await loop.run_in_executor(None, lambda: asyncio.run(expr()))
        self.assertEqual(result, 42)


    def test_run_on_expression_result(self):
        """
        expr().run() works for an expression that returns a constant
        """

        code, ns = build_expr([
            ["CONST", 99],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertEqual(coro.run(), 99)


class TestAsyncGeneratorIter(unittest.IsolatedAsyncioTestCase):
    """
    KosPyAsyncGen.iter() yields the same values as async for
    """

    async def test_iter_matches_async_for(self):
        """
        list(agen.iter()) matches the list from async for over a fresh agen
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)

        agen_sync = expr()
        self.assertIsInstance(agen_sync, AsyncGenerator)
        sync_values = list(agen_sync.iter())
        self.assertEqual(sync_values, [1, 2, 3])

        def run_in_thread():
            agen = expr()
            async def collect(gen):
                out = []
                async for x in gen:
                    out.append(x)
                return out
            return asyncio.run(collect(agen))

        loop = asyncio.get_running_loop()
        async_values = await loop.run_in_executor(None, run_in_thread)
        self.assertEqual(async_values, [1, 2, 3])
        self.assertEqual(sync_values, async_values)


    def test_iter_single_value(self):
        """
        list(agen.iter()) for a single yield returns one element
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen = expr()
        self.assertEqual(list(agen.iter()), [42])


class TestAsyncGeneratorDemandIter(unittest.TestCase):
    """
    __iter__ returns a native DemandGenerator (no asyncio iter_sync)
    """

    def test_iter_builtin_matches_iter_method(self):
        """
        list(agen) uses tp_iter and matches list(agen.iter())
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen_demand = expr()
        agen_loop = expr()
        self.assertIsInstance(agen_demand, AsyncGenerator)
        self.assertEqual(list(agen_demand), list(agen_loop.iter()))


# The end.
