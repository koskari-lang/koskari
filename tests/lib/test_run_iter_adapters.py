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
Tests for KosCoroutine.demand() and sync iteration over KosPyAsyncGen.

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


class TestCoroutineDemand(unittest.IsolatedAsyncioTestCase):
    """
    KosCoroutine.demand() matches asyncio.run(expr()) for pure Koskari code
    """

    async def test_demand_returns_same_as_asyncio_run(self):
        """
        expr().demand() returns the same value as asyncio.run(expr())
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertEqual(coro.demand(), 42)
        loop = asyncio.get_running_loop()
        result = await loop.run_in_executor(None, lambda: asyncio.run(expr()))
        self.assertEqual(result, 42)


    def test_demand_on_expression_result(self):
        """
        expr().demand() works for an expression that returns a constant
        """

        code, ns = build_expr([
            ["CONST", 99],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertEqual(coro.demand(), 99)


class TestAsyncGeneratorSyncList(unittest.IsolatedAsyncioTestCase):
    """
    list(agen) via DemandGenerator matches async for
    """

    async def test_list_matches_async_for(self):
        """
        list(agen) matches the list from async for over a fresh agen
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
        sync_values = list(agen_sync)
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


    def test_list_single_value(self):
        """
        list(agen) for a single yield returns one element
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen = expr()
        self.assertEqual(list(agen), [42])


class TestAsyncGeneratorDemandIter(unittest.TestCase):
    """
    __iter__ returns a native DemandGenerator
    """

    def test_iter_on_async_gen_returns_demand_generator(self):
        """
        iter(agen) on a Koskari AsyncGenerator returns a DemandGenerator.
        The DemandGenerator is its own iterator and yields the same values
        via next() as the async generator would via anext().
        """
        code, ns = build_expr([
            ["CONST", 7],
            ["YIELD", 1],
            ["CONST", 8],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)
        dsg = iter(agen)
        self.assertIsNot(dsg, agen)
        self.assertIs(iter(dsg), dsg)
        self.assertEqual(next(dsg), 7)
        self.assertEqual(next(dsg), 8)
        with self.assertRaises(StopIteration):
            next(dsg)


    def test_two_fresh_agens_list_same(self):
        """
        Two fresh async generators list to the same values
        """

        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        agen_a = expr()
        agen_b = expr()
        self.assertIsInstance(agen_a, AsyncGenerator)
        out_a = list(agen_a)
        out_b = list(agen_b)
        self.assertEqual(out_a, out_b)
        self.assertEqual(out_a, [1, 2])


# The end.
