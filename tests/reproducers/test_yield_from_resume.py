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
test_yield_from_resume - yield-from delegation as a frame resume

The delegate of a yield-from is the frame's resume state. A sent value,
a thrown exception and a close reach the delegate, the delegation ends
with the instruction after the yield-from, and a delegation inside a
for-each body interleaves with the loop's own step resume.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import os
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'yield_from_resume.ki')


class StopWithValue:
    """
    A sync iterator whose first step ends with StopIteration(42)
    """

    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration(42)


def py_echo(n):
    got = []
    for i in range(n):
        got.append((yield i))
    return got


async def apy_gen(n):
    for i in range(n):
        yield i * 2


async def apy_sleeper(n, log):
    try:
        for i in range(n):
            await asyncio.sleep(0)
            yield i * 2
    finally:
        log.append("sleeper finally")


async def apy_echo_sleeper():
    got = yield 0
    await asyncio.sleep(0)
    yield ("got", got)


class SlowCounter:
    """
    A python async iterator with no asend whose every step awaits
    """

    def __init__(self, n, fail_at=None):
        self.i = 0
        self.n = n
        self.fail_at = fail_at

    def __aiter__(self):
        return self

    async def __anext__(self):
        await asyncio.sleep(0)
        if self.i == self.fail_at:
            raise ValueError("step failed")
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return self.i


class TestYieldFromResume(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('yield_from_resume')
        await run_file_async(mod, _SCRIPT)
        return vars(mod)

    async def test_sync_iterator_then_the_instruction_after(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, "after"],
                         await ns['collect'](ns['outer'](range(3))))

    async def test_empty_delegate(self):
        ns = await self.load()
        self.assertEqual(["after"],
                         await ns['collect'](ns['outer'](iter(()))))

    async def test_explicit_stop_iteration_from_a_sync_iterator(self):
        ns = await self.load()
        self.assertEqual(["after"],
                         await ns['collect'](ns['outer'](StopWithValue())))

    async def test_python_generator_delegate(self):
        ns = await self.load()
        self.assertEqual([0, 1, "after"],
                         await ns['collect'](ns['outer'](py_echo(2))))

    async def test_python_async_generator_delegate(self):
        ns = await self.load()
        self.assertEqual([0, 2, 4, "after"],
                         await ns['collect'](ns['outer'](apy_gen(3))))

    async def test_koskari_generator_delegate(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, "after"],
                         await ns['collect'](ns['outer'](ns['echo'](3))))

    async def test_nested_delegation(self):
        ns = await self.load()
        self.assertEqual([0, 1, "after", "outer-after"],
                         await ns['collect'](ns['nested'](range(2))))

    async def test_delegation_inside_a_for_each_body(self):
        ns = await self.load()
        expect = [(a, b) for a in [0, 1, "after"]
                  for b in [0, "after"]]
        self.assertEqual(expect, await ns['collect_pairs'](2, 1))

    async def test_sent_values_reach_a_koskari_delegate(self):
        ns = await self.load()
        self.assertEqual([0, 1, 2, "after", None],
                         await ns['send_through'](3))
        self.assertEqual([[10, 20, 30]], list(ns['log']))

    async def test_thrown_exception_reaches_the_delegate_then_the_try(self):
        ns = await self.load()
        self.assertEqual((0, "zap", "after"), await ns['throw_through'](3))

    async def test_close_closes_the_delegate(self):
        ns = await self.load()
        self.assertEqual((1, ["closed"]), await ns['close_through']())

    async def test_suspending_python_async_generator_delegate(self):
        ns = await self.load()
        log = []
        self.assertEqual([0, 2, 4, "after"],
                         await ns['collect'](ns['outer'](apy_sleeper(3, log))))
        self.assertEqual(["sleeper finally"], log)

    async def test_suspending_python_async_iterator_delegate(self):
        ns = await self.load()
        self.assertEqual([1, 2, 3, "after"],
                         await ns['collect'](ns['outer'](SlowCounter(3))))

    async def test_failure_after_an_await_reaches_the_try(self):
        ns = await self.load()
        self.assertEqual(
            [1, "step failed", "after"],
            await ns['collect'](ns['guarded'](SlowCounter(3, fail_at=1))))

    async def test_send_after_an_awaited_step(self):
        ns = await self.load()
        g = ns['outer'](apy_echo_sleeper())
        self.assertEqual(0, await g.__anext__())
        self.assertEqual(("got", "x"), await g.asend("x"))
        self.assertEqual("after", await g.__anext__())


# The end.
