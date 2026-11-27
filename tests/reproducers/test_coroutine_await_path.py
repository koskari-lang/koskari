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
tests.reproducers.test_coroutine_await_path

Full-stack reproducer: a fresh Koskari coroutine handed to the await
path, by a Python wrapper or as an object, is installed on the running
chain instead of being evaluated on its own context, and its
suspensions, extents, handlers and continuations are the awaiter's. One
Python already started is refused and left to Python

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import functools
import gc
import os
import signal
import sys
import unittest

import pytest

from koskari.lib import (
    KoskariSyncViolation, defer, get_frame_depth_limit,
    set_frame_depth_limit)
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'coroutine_await_path.ki')


class Suspender:
    """
    A Python awaitable that suspends once with "suspended" and
    completes with "woken"
    """

    def __await__(self):
        yield "suspended"
        return "woken"


class Settling:
    """
    A Python awaitable that suspends once with "suspended", then logs
    its tag and completes with False
    """

    def __init__(self, log, tag):
        self.log = log
        self.tag = tag


    def __await__(self):
        yield "suspended"
        self.log.append(self.tag)
        return False


class AwaitedExit:
    """
    A context manager whose __exit__ hands back an awaitable that
    suspends
    """

    def __init__(self, log):
        self.log = log


    def __enter__(self):
        self.log.append("enter")


    def __exit__(self, typ, val, tb):
        self.log.append("exit")
        return Settling(self.log, "exit2")


class AwaitedEnter:
    """
    A context manager whose __enter__ hands back an awaitable that
    suspends, once gate is non-empty
    """

    def __init__(self, log, gate):
        self.log = log
        self.gate = gate


    def __enter__(self):
        self.log.append("enter")
        if self.gate:
            return Settling(self.log, "enter2")
        return None


    def __exit__(self, typ, val, tb):
        self.log.append("exit")
        return False


class Interrupted(Exception):
    """
    Raised by the SIGINT handler the signal test installs; the
    interrupt is raised with _thread.interrupt_main, which took no
    signal argument before Python 3.10
    """


def raise_interrupted(signum, frame):
    raise Interrupted(signum)


class TestCoroutineAwaitPath(unittest.IsolatedAsyncioTestCase):
    """
    Koskari coroutines reaching the await path from Python wrappers
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.coroutine_await_path')
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


    async def test_started_coroutine_refuses_a_koskari_await(self):
        """
        A coroutine Python started owes its next reply to the party that
        received its yield; a Koskari await of it is a second awaiter
        and is refused, as CPython refuses one
        """

        mod = await self.load_module()
        coro = mod.sleeper()
        coro.send(None)
        with self.assertRaises(RuntimeError) as caught:
            await mod.await_object(coro)
        self.assertEqual(
            str(caught.exception), "coroutine is being awaited already")
        coro.close()


    def test_refused_coroutine_is_left_to_its_starter(self):
        """
        The refusal changes nothing in the coroutine: Python, which
        started it, completes it
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_value()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_object(c)
        with self.assertRaises(RuntimeError) as caught:
            outer.send(None)
        self.assertEqual(
            str(caught.exception), "coroutine is being awaited already")

        with self.assertRaises(StopIteration) as done:
            c.send(None)
        self.assertEqual(done.exception.value, "woken")


    def fresh(self, c, awaiter):
        """
        Await the fresh coroutine c from awaiter, and step the awaiter
        to the await the coroutine suspends on

        :param c: a fresh coroutine that suspends on a Suspender
        :param awaiter: the function of the module that awaits it
        :return: the awaiter's coroutine, suspended
        """

        outer = awaiter(c)
        self.assertEqual(outer.send(None), "suspended")
        return outer


    def finish(self, outer):
        """
        Step a suspended coroutine to its return

        :param outer: the coroutine
        :return: its value
        """

        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        return done.exception.value


    def test_shift_across_an_awaited_coroutine_applies_twice(self):
        """
        A shift in an awaited coroutine reaches a reset around the
        await: the continuation holds the rest of the coroutine and of
        the awaiter's reset body, and applies twice
        """

        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_then_shift_twice(), mod.reset_await)
        self.assertEqual(self.finish(outer), 111 + 121)


    def test_continuation_across_an_awaited_coroutine_outlives_the_reset(
            self):

        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_then_shift_keep(), mod.reset_await_apply)
        self.assertEqual(self.finish(outer), (102, 103))


    def test_continuation_across_an_awaited_coroutine_is_dropped(self):
        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_then_shift_abandon(), mod.reset_await)
        self.assertEqual(self.finish(outer), "abandoned")
        gc.collect()


    def test_callcc_across_an_awaited_coroutine_captures(self):
        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_then_callcc(), mod.await_object)
        self.assertEqual(self.finish(outer), 11)

        box = []
        seen = []
        c = mod.wait_then_callcc_keep(box)
        outer = self.fresh(c, lambda c: mod.await_reentered(c, box, seen))
        self.assertEqual(self.finish(outer), (11, 21))


    def test_applied_continuation_suspends_on_its_own_await(self):
        """
        The applied copy of an awaited coroutine's frames awaits as any
        frame does: the awaiter suspends again and the code stepping it
        completes the await
        """

        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_shift_wait(), mod.reset_await)
        self.assertEqual(outer.send(None), "suspended")
        self.assertEqual(self.finish(outer), 110)


    def test_awaited_coroutine_sees_the_awaiters_parameterize(self):
        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_then_read(), mod.await_under)
        self.assertEqual(self.finish(outer), "inner")


    def test_throw_from_the_awaiters_driver_arrives_at_the_suspend_point(self):
        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_guarded(), mod.await_object)
        with self.assertRaises(StopIteration) as done:
            outer.throw(ValueError("t"))
        self.assertEqual(done.exception.value, "caught")


    def test_demand_over_an_awaited_coroutine_refuses_every_frames_await(
            self):
        """
        An awaited coroutine evaluated under demand: the callee's await is
        refused, and so is the await the caller frame beneath it makes
        once the callee returns, since every frame carries the demand
        bit
        """

        mod = asyncio.run(self.load_module())
        log = []
        c = mod.two_awaits(Suspender(), Suspender(), log)
        d = mod.demand_await(c)
        with self.assertRaises(KoskariSyncViolation):
            d.send(None)
        self.assertEqual(log, [KoskariSyncViolation])


    def test_awaited_handler_keeps_its_handled_exception(self):
        """
        An awaited coroutine suspended inside an except handler keeps
        the handler's exception: handler-visible again when it resumes,
        and gone once the handler is left
        """

        mod = asyncio.run(self.load_module())
        outer = self.fresh(mod.wait_in_handler(), mod.await_then_handled)
        inside, after = self.finish(outer)
        self.assertIsInstance(inside, ValueError)
        self.assertEqual(str(inside), "inside")
        self.assertIsNone(after)


    def test_suspension_of_an_awaited_coroutine_runs_no_extent_thunk(self):
        """
        An awaited coroutine suspended inside a parameterize and a
        dyn-wind: the suspension runs no thunk, the parameter cell is
        the coroutine's when it resumes, and the awaiter's own cell is
        back once the extent is left
        """

        mod = asyncio.run(self.load_module())
        log = []
        outer = self.fresh(mod.wait_in_extents(log), mod.await_then_read)
        self.assertEqual(log, ["before"])
        self.assertEqual(self.finish(outer), ("coro", "root"))
        self.assertEqual(log, ["before", "after"])


    def test_signal_pending_at_the_await_raises_at_the_site(self):
        """
        A signal taken by the poll between the settle and the await
        kind drops the staged awaitable and delivers a failure to the
        kind, which declines it: the handler's exception raises at the
        await site, for a fresh coroutine and for a defer over one, and
        the coroutine never started stays closable
        """

        mod = asyncio.run(self.load_module())
        saved = signal.signal(signal.SIGINT, raise_interrupted)
        try:
            c = mod.add(1, 2)
            outer = mod.await_after_interrupt(c)
            with self.assertRaises(Interrupted):
                outer.send(None)
            self.assertIn("[new]", repr(c))
            c.close()

            d = defer(mod.add, 1, 2)
            outer = mod.await_after_interrupt(d)
            with self.assertRaises(Interrupted):
                outer.send(None)
            self.assertIn("delegating", repr(d))
            self.assertIn("[new]", repr(d))
            d.close()
        finally:
            signal.signal(signal.SIGINT, saved)


    def test_refusal_comes_before_the_room_check(self):
        """
        A started coroutine too deep for the awaiter's room is refused as
        started, not for room, and Python can still step it
        """

        mod = asyncio.run(self.load_module())
        c = mod.deep_wait(100)
        self.assertEqual(c.send(None), "suspended")

        saved = get_frame_depth_limit()
        set_frame_depth_limit(50)
        try:
            outer = mod.await_object(c)
            with self.assertRaises(RuntimeError) as caught:
                outer.send(None)
            self.assertEqual(
                str(caught.exception), "coroutine is being awaited already")
        finally:
            set_frame_depth_limit(saved)

        with self.assertRaises(StopIteration) as done:
            c.send(None)
        self.assertEqual(done.exception.value, 100)


    async def test_demand_denies_a_wrapped_suspension(self):
        mod = await self.load_module()
        with self.assertRaises(KoskariSyncViolation):
            await mod.demand_over_partial()


    def run_out(self, outer, suspensions):
        """
        Step a coroutine to its return, each step before it suspending
        on "suspended"

        :param outer: the coroutine
        :param suspensions: the number of suspensions expected
        :return: its value
        """

        count = 0
        with self.assertRaises(StopIteration) as done:
            while True:
                self.assertEqual(outer.send(None), "suspended")
                count += 1
        self.assertEqual(count, suspensions)
        return done.exception.value


    def test_awaited_coroutine_suspends_in_an_after_segment_at_a_yield(self):
        """
        An awaited coroutine suspended in the after segment a
        generator's yield runs: the segment finishes on the awaiter's
        chain, the consumer has the value, and the next step enters the
        dyn-wind again
        """

        mod = asyncio.run(self.load_module())
        log = []
        outer = mod.await_object(mod.collect(mod.wound(log), log))
        expect = [
            "in", "out", "out2", 1, "in", "out", "out2", 2,
            "in", "out", "out2"]
        self.assertEqual(self.run_out(outer, 3), expect)
        self.assertEqual(log, expect)


    def test_awaited_coroutine_suspends_in_a_before_segment_at_reentry(self):
        mod = asyncio.run(self.load_module())
        log = []
        gate = []
        c = mod.collect_gated(mod.wound_on_entry(log, gate), log, gate)
        outer = mod.await_object(c)
        expect = [
            "in", "in2", "out", 1, "in", "in2", "out", 2,
            "in", "in2", "out"]
        self.assertEqual(self.run_out(outer, 2), expect)
        self.assertEqual(log, expect)


    def test_awaited_coroutine_suspends_in_a_dyn_with_exit_at_a_yield(self):
        mod = asyncio.run(self.load_module())
        log = []
        c = mod.collect(mod.managed(AwaitedExit(log)), log)
        outer = mod.await_object(c)
        expect = [
            "enter", "exit", "exit2", 1, "enter", "exit", "exit2", 2,
            "enter", "exit", "exit2"]
        self.assertEqual(self.run_out(outer, 3), expect)
        self.assertEqual(log, expect)


    def test_awaited_coroutine_suspends_in_a_dyn_with_enter_at_reentry(self):
        mod = asyncio.run(self.load_module())
        log = []
        gate = []
        gen = mod.managed(AwaitedEnter(log, gate))
        outer = mod.await_object(mod.collect_gated(gen, log, gate))
        expect = [
            "enter", "exit", 1, "enter", "enter2", "exit", 2,
            "enter", "enter2", "exit"]
        self.assertEqual(self.run_out(outer, 2), expect)
        self.assertEqual(log, expect)


    def test_awaited_coroutine_suspends_in_an_after_segment_of_an_abort(
            self):
        """
        A shift's abort leaving a dyn-wind whose after segment suspends
        goes on from the awaiter's chain to its reset
        """

        mod = asyncio.run(self.load_module())
        log = []
        outer = mod.await_object(mod.abort_through_wait(log))
        self.assertEqual(self.run_out(outer, 1), "aborted")
        self.assertEqual(log, ["in", "out", "out2"])


# The end.
