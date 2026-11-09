# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_coroutine_splice

Full-stack reproducer: a fresh Koskari coroutine handed to the await
path by a Python wrapper is installed on the running chain instead of
being driven on its own context.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import functools
import os
import sys
import unittest

import pytest

from koskari.lib import (
    KoskariSyncViolation, get_frame_depth_limit, set_frame_depth_limit)
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'coroutine_splice.ki')


class Suspender:
    """
    A Python awaitable that suspends once with "suspended" and
    completes with "woken"
    """

    def __await__(self):
        yield "suspended"
        return "woken"


class TestCoroutineSplice(unittest.IsolatedAsyncioTestCase):
    """
    Koskari coroutines reaching the await path from Python wrappers
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.coroutine_splice')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        vars(mod)['sleeper_partial'] = functools.partial(mod.sleeper)
        vars(mod)['Suspender'] = Suspender
        vars(mod)['handled'] = lambda: sys.exc_info()[1]
        return mod


    async def test_partial_runs_on_the_calling_chain(self):
        """
        Non-tail recursion through functools.partial meets the caller's
        frame-depth limit; a nested context per level would reset it
        """

        mod = await self.load_module()
        self.assertEqual(await mod.via_partial(200), 200)
        with self.assertRaisesRegex(RecursionError, "frame depth"):
            await mod.via_partial(get_frame_depth_limit() + 500)


    async def test_defer_runs_on_the_calling_chain(self):
        mod = await self.load_module()
        self.assertEqual(await mod.via_defer(200), 200)
        with self.assertRaisesRegex(RecursionError, "frame depth"):
            await mod.via_defer(get_frame_depth_limit() + 500)


    async def test_awaited_coroutine_object_is_consumed(self):
        """
        An explicit await of a fresh coroutine object delivers its value,
        and the object is spent for Python afterwards
        """

        mod = await self.load_module()
        coro = mod.add(1, 2)
        self.assertEqual(await mod.await_object(coro), 3)
        with self.assertRaises(RuntimeError):
            coro.send(None)


    async def test_started_coroutine_is_spliced(self):
        """
        A coroutine Python started and left parked at an await splices
        onto the awaiter's chain and completes there; the object is
        spent for Python afterwards
        """

        mod = await self.load_module()
        coro = mod.sleeper()
        coro.send(None)
        self.assertEqual(await mod.await_object(coro), "woke")
        with self.assertRaises(RuntimeError):
            coro.send(None)


    def test_splice_resuspends_on_the_parked_await(self):
        """
        The awaiter suspends on the value the parked coroutine handed
        its driver, and the awaiter's driver completes that await: the
        result lands at the coroutine's suspend point and its return is
        the awaiter's value
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_value()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(RuntimeError) as active:
            c.send(None)
        self.assertEqual(
            str(active.exception), "coroutine already being awaited")

        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")
        with self.assertRaises(RuntimeError) as spent:
            c.send(None)
        self.assertEqual(
            str(spent.exception), "cannot reuse already awaited coroutine")


    def test_spliced_coroutine_sees_the_awaiters_parameterize(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_then_read()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_under(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "inner")


    def test_throw_from_the_awaiters_driver_lands_at_the_suspend_point(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_guarded()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.throw(ValueError("t"))
        self.assertEqual(done.exception.value, "caught")


    def test_spliced_handler_keeps_its_handled_exception(self):
        """
        A coroutine parked inside an except handler brings the handler's
        exception with it: handler-visible again on the awaiter's chain
        after the splice, and gone once the handler is left
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_in_handler()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_then_handled(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        inside, after = done.exception.value
        self.assertIsInstance(inside, ValueError)
        self.assertEqual(str(inside), "inside")
        self.assertIsNone(after)


    def test_room_refused_splice_leaves_the_coroutine_parked(self):
        """
        A parked chain the awaiter has no room for is refused at the
        await site and stays where it was, drivable by Python
        """

        mod = asyncio.run(self.load_module())
        c = mod.deep_wait(100)
        self.assertEqual(c.send(None), "suspended")

        saved = get_frame_depth_limit()
        set_frame_depth_limit(50)
        try:
            outer = mod.await_object(c)
            with self.assertRaisesRegex(RecursionError, "frame depth"):
                outer.send(None)
        finally:
            set_frame_depth_limit(saved)

        with self.assertRaises(StopIteration) as done:
            c.send(None)
        self.assertEqual(done.exception.value, 100)


    async def test_demand_denies_a_wrapped_suspension(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            await mod.demand_over_partial()


# The end.
