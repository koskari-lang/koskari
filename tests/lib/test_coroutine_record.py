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
tests.lib.test_coroutine_record

An unstarted Koskari coroutine is a bound call: its arguments and the
parameterize state at its creation are fixed, and nothing has run.
Awaited from Koskari it flattens onto the awaiter's chain and reads the
awaiter's parameters; driven from Python it runs under the state it was
created in. Close, throw, repr, and collection work before the first
drive, and binding errors surface when the defer is made. Repeated
calls through one function, awaited from Python, gathered by asyncio,
closed or dropped unstarted, or awaited from Koskari, each return their
own call's value.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import types
import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


@types.coroutine
def one_step():
    yield "suspended"
    return "woken"


class Suspender:
    def __await__(self):
        return one_step()


SRC = """
(define-parameter p 0)

(define read_p (lambda [] p))

(define make_under (lambda [v] (parameterize [[p v]] (defer read_p))))

(define await_under (lambda [d v] (parameterize [[p v]] (await d))))

(define add (lambda [a b] (+ a b)))

(define twice (lambda [a b] (await (defer add a b))))

(define finally_box (list))

(define guarded (lambda [aw]
  (try
    (await aw)
    (finally: (finally_box.append "ran")))))

(define mix [a b] (+ (* a 31) b))

(define mix_kw [a b: 10] (+ (* a 31) b))

(define count_down [n] (if (<= n 0) 0 (+ 1 (count_down (- n 1)))))

(define await_it [c] (await c))
"""


class TestCoroutineRecord(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("coroutine_record")
        vars(module)["__file__"] = "<coroutine_record>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_koskari_await_runs_under_the_awaiter_parameters(self):
        mod = await self.load()
        d = await mod["make_under"](1)
        self.assertIsInstance(d, lib.defer)
        self.assertEqual(await mod["await_under"](d, 2), 2)

    async def test_python_drive_runs_under_the_creation_parameters(self):
        mod = await self.load()
        d = await mod["make_under"](1)
        self.assertEqual(await d, 1)

    async def test_python_task_runs_under_the_creation_parameters(self):
        mod = await self.load()
        d = await mod["make_under"](3)
        self.assertEqual(await asyncio.create_task(d), 3)

    async def test_defer_awaited_in_koskari_returns_the_value(self):
        mod = await self.load()
        self.assertEqual(await mod["twice"](2, 3), 5)

    async def test_unstarted_coroutine_repr_close_and_reuse(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        self.assertIn("new", repr(coro))
        self.assertIsNone(coro.close())
        self.assertIn("done", repr(coro))
        with self.assertRaises(RuntimeError):
            coro.send(None)

    async def test_unstarted_coroutine_throw_raises_without_running(self):
        mod = await self.load()
        coro = mod["guarded"](Suspender())
        with self.assertRaises(ValueError):
            coro.throw(ValueError("early"))
        self.assertEqual(mod["finally_box"], [])

    async def test_unstarted_coroutine_is_collected(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        cycle = [coro]
        cycle.append(cycle)
        del coro, cycle
        gc.collect()

    async def test_binding_errors_surface_when_the_defer_is_made(self):
        mod = await self.load()
        with self.assertRaises(TypeError):
            lib.defer(mod["add"], 1)
        with self.assertRaises(TypeError):
            mod["add"](1, 2, 3)

    async def test_started_coroutine_still_closes_through_finally(self):
        mod = await self.load()
        loop = asyncio.get_running_loop()
        fut = loop.create_future()
        coro = mod["guarded"](fut)
        coro.send(None)
        self.assertIsNone(coro.close())
        self.assertEqual(mod["finally_box"], ["ran"])

    async def test_repeated_python_awaits_return_each_value(self):
        mod = await self.load()
        mix = mod["mix"]
        for i in range(20):
            self.assertEqual(await mix(i, 7), i * 31 + 7)

    async def test_self_naming_function_awaited_repeatedly(self):
        mod = await self.load()
        count_down = mod["count_down"]
        for i in range(12):
            self.assertEqual(await count_down(i), i)

    async def test_call_after_a_coroutine_closed_unstarted(self):
        mod = await self.load()
        mix = mod["mix"]
        for i in range(10):
            coro = mix(i, 1)
            self.assertIsNone(coro.close())
            with self.assertRaises(RuntimeError):
                coro.send(None)
            self.assertEqual(await mix(i, 2), i * 31 + 2)

    async def test_unstarted_coroutines_dropped_then_called_again(self):
        mod = await self.load()
        mix = mod["mix"]
        held = [mix(i, 3) for i in range(12)]
        del held
        gc.collect()
        self.assertEqual(await mix(4, 5), 129)

    async def test_koskari_await_of_python_made_coroutines(self):
        mod = await self.load()
        mix = mod["mix"]
        await_it = mod["await_it"]
        for i in range(20):
            self.assertEqual(await await_it(mix(i, 2)), i * 31 + 2)

    async def test_keyword_and_default_shapes_awaited_from_python(self):
        mod = await self.load()
        mix_kw = mod["mix_kw"]
        for i in range(10):
            self.assertEqual(await mix_kw(i), i * 31 + 10)
            self.assertEqual(await mix_kw(i, b=2), i * 31 + 2)
            self.assertEqual(await mix_kw(i, 3), i * 31 + 3)
            self.assertEqual(await mix_kw(a=i, b=4), i * 31 + 4)

    async def test_defers_gathered_by_asyncio(self):
        mod = await self.load()
        mix = mod["mix"]
        for _ in range(3):
            got = await asyncio.gather(
                *[lib.defer(mix, i, 1) for i in range(16)])
            self.assertEqual(got, [i * 31 + 1 for i in range(16)])

    async def test_coroutines_gathered_by_asyncio(self):
        mod = await self.load()
        mix = mod["mix"]
        for _ in range(3):
            got = await asyncio.gather(*[mix(i, 9) for i in range(16)])
            self.assertEqual(got, [i * 31 + 9 for i in range(16)])

    async def test_coroutine_python_surface(self):
        mod = await self.load()
        coro = mod["mix"](1, 2)
        self.assertIs(type(coro), lib.Coroutine)
        public = sorted(n for n in dir(coro) if not n.startswith("_"))
        self.assertEqual(public, ["close", "demand", "send", "throw"])
        self.assertIn("new", repr(coro))
        self.assertEqual(coro.demand(), 33)
        self.assertIn("done", repr(coro))


# The end.
