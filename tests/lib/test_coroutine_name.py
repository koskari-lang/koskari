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
tests.lib.test_coroutine_name

A Koskari coroutine carries the read-only __name__ and __qualname__ of
the function that made it, or its code's name for an Expression, in
every state from new to done. asyncio names a Task's coroutine by them.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.module import load_module_source_async, new_module
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


SRC = """
(define [add a b] (+ a b))

(define [probe f] (f))

(define [wait_on fut] (await fut))
"""


def current_coro_names():
    coro = asyncio.current_task().get_coro()
    return coro.__name__, coro.__qualname__, repr(coro)


class TestCoroutineName(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("coroutine_name")
        vars(module)["__file__"] = "<coroutine_name>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)


    async def test_new(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        self.assertEqual(coro.__name__, "add")
        self.assertEqual(coro.__qualname__, "add")
        self.assertIn("[new]", repr(coro))
        coro.close()


    async def test_read_only(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        with self.assertRaises(AttributeError):
            coro.__name__ = "other"
        with self.assertRaises(AttributeError):
            coro.__qualname__ = "other"
        self.assertEqual(coro.__name__, "add")
        coro.close()


    async def test_running(self):
        mod = await self.load()
        name, qualname, rep = await asyncio.create_task(
            mod["probe"](current_coro_names))
        self.assertEqual(name, "probe")
        self.assertEqual(qualname, "probe")
        self.assertIn("[running]", rep)


    async def test_suspended(self):
        mod = await self.load()
        fut = asyncio.get_running_loop().create_future()
        coro = mod["wait_on"](fut)
        task = asyncio.create_task(coro)
        await asyncio.sleep(0)
        self.assertIn("[waiting]", repr(coro))
        self.assertEqual(coro.__name__, "wait_on")
        self.assertEqual(coro.__qualname__, "wait_on")
        fut.set_result(7)
        self.assertEqual(await task, 7)


    async def test_done(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        self.assertEqual(await coro, 3)
        self.assertIn("[done]", repr(coro))
        self.assertEqual(coro.__name__, "add")
        self.assertEqual(coro.__qualname__, "add")


    async def test_closed_unstarted(self):
        mod = await self.load()
        coro = mod["add"](1, 2)
        coro.close()
        self.assertEqual(coro.__name__, "add")
        self.assertEqual(coro.__qualname__, "add")


    async def test_qualname_override(self):
        mod = await self.load()
        add = mod["add"]
        add.__qualname__ = "Holder.add"
        coro = add(1, 2)
        self.assertEqual(coro.__name__, "add")
        self.assertEqual(coro.__qualname__, "Holder.add")
        self.assertEqual(await coro, 3)
        self.assertEqual(coro.__qualname__, "Holder.add")


    async def test_name_override(self):
        mod = await self.load()
        add = mod["add"]
        add.__name__ = "plus"
        coro = add(1, 2)
        self.assertEqual(coro.__name__, "plus")
        self.assertEqual(coro.__qualname__, "plus")
        coro.close()


    async def test_expression_new(self):
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]], name="answer")
        coro = make_callable(code, ns)()
        self.assertEqual(coro.__name__, "answer")
        self.assertEqual(coro.__qualname__, "answer")
        coro.close()


    async def test_expression_done(self):
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]], name="answer")
        coro = make_callable(code, ns)()
        self.assertEqual(await coro, 42)
        self.assertIn("[done]", repr(coro))
        self.assertEqual(coro.__name__, "answer")
        self.assertEqual(coro.__qualname__, "answer")


    async def test_task_repr(self):
        mod = await self.load()
        fut = asyncio.get_running_loop().create_future()
        task = asyncio.create_task(mod["wait_on"](fut))
        self.assertIn("coro=<wait_on()", repr(task))
        await asyncio.sleep(0)
        self.assertIn("coro=<wait_on()", repr(task))
        fut.set_result(1)
        self.assertEqual(await task, 1)
        self.assertIn("coro=<wait_on()", repr(task))


# The end.
