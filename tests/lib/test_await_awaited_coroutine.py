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
tests.lib.test_await_awaited_coroutine

Awaiting a native coroutine that is already suspended under another
awaiter is refused with RuntimeError, as CPython's await refuses it,
and the first awaiter keeps its step.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


class Suspender:
    def __await__(self):
        yield "suspended"
        return "woken"


SRC = """
(define [waiter aw] (await aw))
"""


class TestAwaitAwaitedCoroutine(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("await_awaited_coroutine")
        vars(module)["__file__"] = "<await_awaited_coroutine>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_second_awaiter_is_refused(self):
        mod = await self.load()

        async def inner():
            return await Suspender()

        async def outer(c):
            return await c

        c = inner()
        first = outer(c)
        self.assertEqual(first.send(None), "suspended")
        self.assertIsNotNone(c.cr_await)

        second = mod["waiter"](c)
        with self.assertRaises(RuntimeError) as caught:
            second.send(None)
        self.assertEqual(
            str(caught.exception), "coroutine is being awaited already")

        with self.assertRaises(StopIteration) as done:
            first.send(None)
        self.assertEqual(done.exception.value, "woken")

    async def test_unawaited_coroutine_is_stepped(self):
        mod = await self.load()

        async def inner():
            return await Suspender()

        coro = mod["waiter"](inner())
        self.assertEqual(coro.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            coro.send(None)
        self.assertEqual(done.exception.value, "woken")


# The end.
