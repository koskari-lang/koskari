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
tests.lib.test_coroutine_close_from_await

A Koskari coroutine waiting on a Python awaitable forwards each send,
throw, and close into it. Code inside that awaitable which calls close,
send, or throw on the waiting coroutine is refused with RuntimeError,
and the awaitable it runs in stays alive for the rest of the step.

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


class Reenter:
    """
    Awaitable whose finally calls back into the waiting coroutine
    """

    def __init__(self, action, holder, outcome):
        self.action = action
        self.holder = holder
        self.outcome = outcome

    def __await__(self):
        try:
            yield "suspended"
        finally:
            try:
                self.action(self.holder["coro"])
            except RuntimeError:
                self.outcome["nested"] = "refused"
            else:
                self.outcome["nested"] = "accepted"
        return "woken"


SRC = """
(define [waiter aw] (await aw))
"""


class TestCoroutineCloseFromAwait(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("coroutine_close_from_await")
        vars(module)["__file__"] = "<coroutine_close_from_await>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_close_from_inside_the_awaited_step_is_refused(self):
        mod = await self.load()
        holder = {}

        async def inner():
            await Suspender()
            try:
                holder["coro"].close()
            except RuntimeError:
                return "refused"
            return "closed"

        coro = mod["waiter"](inner())
        holder["coro"] = coro
        self.assertEqual(coro.send(None), "suspended")
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, "refused")
        self.assertIn("done", repr(coro))

    async def test_send_from_inside_the_awaited_step_is_refused(self):
        mod = await self.load()
        holder = {}

        async def inner():
            await Suspender()
            try:
                holder["coro"].send(None)
            except RuntimeError:
                return "refused"
            return "sent"

        coro = mod["waiter"](inner())
        holder["coro"] = coro
        self.assertEqual(coro.send(None), "suspended")
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, "refused")

    async def test_close_from_inside_a_forwarded_throw_is_refused(self):
        mod = await self.load()
        holder = {}

        async def inner():
            try:
                await Suspender()
            except KeyError:
                pass
            try:
                holder["coro"].close()
            except RuntimeError:
                return "refused"
            return "closed"

        coro = mod["waiter"](inner())
        holder["coro"] = coro
        self.assertEqual(coro.send(None), "suspended")
        with self.assertRaises(StopIteration) as caught:
            coro.throw(KeyError("k"))
        self.assertEqual(caught.exception.value, "refused")

    async def check_reenter_during_close(self, action):
        mod = await self.load()
        holder = {}
        outcome = {}

        coro = mod["waiter"](Reenter(action, holder, outcome))
        holder["coro"] = coro
        self.assertEqual(coro.send(None), "suspended")
        self.assertIsNone(coro.close())
        self.assertEqual(outcome, {"nested": "refused"})
        self.assertIn("done", repr(coro))

    async def test_close_from_inside_a_forwarded_close_is_refused(self):
        await self.check_reenter_during_close(lambda coro: coro.close())

    async def test_send_from_inside_a_forwarded_close_is_refused(self):
        await self.check_reenter_during_close(lambda coro: coro.send(None))


# The end.
