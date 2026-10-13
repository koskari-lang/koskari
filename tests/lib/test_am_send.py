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
tests.lib.test_am_send

The send slot on Coroutine and defer: asyncio's task step and a
yield-from drive reach completions as values, the iterator-protocol
entry points still raise StopIteration, a waiting coroutine forwards
into a Python awaitable, and a plain iterator behind a defer keeps its
non-None-send error.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import sys
import types
import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(var add (lambda [a b] (+ a b)))

(var waiter (lambda [aw] (+ 1 (await aw))))

(var waiter_twice (lambda [a b] (+ (await a) (await b))))
"""


def drive_await(target):
    """
    Drive an await of target from a Python coroutine stepped by hand,
    collecting what it yields and returning its completion value. The
    SEND opcode behind the await goes through the send slot when the
    target has one.
    """

    yielded = []

    async def run():
        return await target

    coro = run()
    try:
        while True:
            yielded.append(coro.send(None))
    except StopIteration as stop:
        return stop.value, yielded


class TestAmSend(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("am_send")
        vars(module)["__file__"] = "<am_send>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    def test_slots_are_present(self):
        if sys.version_info >= (3, 10):
            self.assertTrue(hasattr(lib.Coroutine, "__await__"))
            self.assertTrue(hasattr(lib.defer, "__await__"))

    async def test_task_step_returns_the_value(self):
        mod = await self.load()
        self.assertEqual(await asyncio.create_task(mod["add"](2, 3)), 5)
        self.assertEqual(await asyncio.gather(
            mod["add"](1, 1), mod["add"](2, 2)), [2, 4])

    async def test_send_method_still_raises_stop_iteration(self):
        mod = await self.load()
        coro = mod["add"](2, 3)
        with self.assertRaises(StopIteration) as stop:
            coro.send(None)
        self.assertEqual(stop.exception.value, 5)

    async def test_hand_driven_await_completes_with_the_value(self):
        mod = await self.load()
        value, yielded = drive_await(mod["add"](4, 5))
        self.assertEqual(value, 9)
        self.assertEqual(yielded, [])

    async def test_waiting_coroutine_forwards_to_a_future(self):
        mod = await self.load()
        loop = asyncio.get_running_loop()
        fut = loop.create_future()
        loop.call_soon(fut.set_result, 41)
        self.assertEqual(await mod["waiter"](fut), 42)

    async def test_waiting_coroutine_forwards_twice(self):
        mod = await self.load()
        loop = asyncio.get_running_loop()
        a, b = loop.create_future(), loop.create_future()
        loop.call_soon(a.set_result, 10)
        loop.call_later(0.01, b.set_result, 20)
        self.assertEqual(await mod["waiter_twice"](a, b), 30)

    async def test_waiting_coroutine_receives_the_awaitable_error(self):
        mod = await self.load()
        loop = asyncio.get_running_loop()
        fut = loop.create_future()
        loop.call_soon(fut.set_exception, ValueError("boom"))
        with self.assertRaises(ValueError):
            await mod["waiter"](fut)

    async def test_done_defer_completes_as_a_value(self):
        d = lib.defer(lambda: 7)
        self.assertEqual(await d, 7)
        value, yielded = drive_await(lib.defer(lambda: 8))
        self.assertEqual((value, yielded), (8, []))

    async def test_done_defer_send_method_raises_stop_iteration(self):
        d = lib.defer(lambda: 7)
        with self.assertRaises(StopIteration) as stop:
            d.send(None)
        self.assertEqual(stop.exception.value, 7)

    async def test_delegating_defer_forwards_to_a_python_coroutine(self):
        async def inner():
            await asyncio.sleep(0)
            return "inner"
        self.assertEqual(await lib.defer(inner), "inner")

    async def test_delegating_defer_forwards_to_a_generator_awaitable(self):
        @types.coroutine
        def two_steps():
            yield "first"
            yield "second"
            return "done"

        class Aw:
            def __await__(self):
                return two_steps()

        value, yielded = drive_await(lib.defer(Aw))
        self.assertEqual(value, "done")
        self.assertEqual(yielded, ["first", "second"])

    async def test_sendless_iterator_rejects_a_non_none_send(self):
        class Aw:
            def __await__(self):
                return iter(["a", "b"])

        d = lib.defer(Aw)
        self.assertEqual(d.send(None), "a")
        with self.assertRaises(AttributeError):
            d.send("x")

    async def test_none_send_takes_next_over_a_send_method(self):
        """
        A None send on an iterator that has both __next__ and send is a
        step of the iteration protocol, as PyIter_Send makes it
        """

        calls = []

        class Both:
            def __iter__(self):
                return self

            def __next__(self):
                calls.append("next")
                raise StopIteration("done")

            def send(self, value):
                calls.append(("send", value))
                raise StopIteration("done")

        class Aw:
            def __await__(self):
                return Both()

        value, yielded = drive_await(lib.defer(Aw))
        self.assertEqual((value, yielded), ("done", []))
        self.assertEqual(calls, ["next"])

    async def test_provider_await_resumes_a_sendless_iterator(self):
        """
        A provider's function awaiting an object whose __await__ is a
        plain iterator resumes it with next after its yield settles
        """

        loop = asyncio.get_running_loop()

        def make(x):
            fut = loop.create_future()
            fut.set_result(None)
            fut._asyncio_future_blocking = True

            class Aw:
                def __await__(self):
                    return iter([fut])

            return Aw()

        mod = new_module("provider_await")
        vars(mod)["__file__"] = "<provider_await>"
        await load_module_source_async(
            mod,
            "(var run (lambda [mk]"
            " (map (lambda [x] (begin (await (mk x)) x)) #[1 2])))",
            layout=False)
        provider = await vars(mod)["run"](make)
        self.assertEqual([v async for v in provider], [1, 2])

    async def test_koskari_await_of_a_sendless_iterator(self):
        """
        A Koskari await of an object whose __await__ returns a plain
        iterator steps it with next: a None send takes the iteration
        protocol on every version, as PyIter_Send does
        """

        class Aw:
            def __await__(self):
                return iter(["a"])

        v = await self.load()

        async def run():
            return await v["waiter"](Aw())

        coro = run()
        self.assertEqual(coro.send(None), "a")
        with self.assertRaises(TypeError):
            # the completion is None, and (+ 1 None) fails as it should
            coro.send(None)

    async def test_sendless_iterator_completes_with_none(self):
        class Aw:
            def __await__(self):
                return iter(["a"])

        value, yielded = drive_await(lib.defer(Aw))
        self.assertEqual((value, yielded), (None, ["a"]))


# The end.
