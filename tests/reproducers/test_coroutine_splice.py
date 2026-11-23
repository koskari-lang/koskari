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
import gc
import os
import signal
import sys
import types
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


class Twice:
    """
    A Python awaitable that suspends with "first", then "second", and
    completes with "woken"
    """

    def __await__(self):
        yield "first"
        yield "second"
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


def started(fn, *args):
    """
    Call a Koskari function from Python and step its coroutine once

    :return: a namespace holding the coroutine as c and what its first
      step suspended on as first
    """

    c = fn(*args)
    first = c.send(None)
    return types.SimpleNamespace(c=c, first=first)


def finish_via(fn, *args):
    """
    Call a Koskari function from Python and step its coroutine to its
    return, discarding what it suspends on

    :return: the coroutine's value
    """

    c = fn(*args)
    try:
        while True:
            c.send(None)
    except StopIteration as done:
        return done.value


class Interrupted(Exception):
    """
    Raised by the SIGINT handler the signal test installs; the
    interrupt is raised with _thread.interrupt_main, which took no
    signal argument before Python 3.10
    """


def raise_interrupted(signum, frame):
    raise Interrupted(signum)


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
        vars(mod)['Twice'] = Twice
        vars(mod)['handled'] = lambda: sys.exc_info()[1]
        vars(mod)['started'] = started
        vars(mod)['finish_via'] = finish_via
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
        result is delivered at the coroutine's suspend point and its return is
        the awaiter's value
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_value()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "suspended")

        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")
        with self.assertRaises(RuntimeError) as spent:
            c.send(None)
        self.assertEqual(
            str(spent.exception), "cannot reuse already awaited coroutine")


    def spliced(self, mod, c, awaiter):
        """
        Start c from Python, await it from awaiter, and step the awaiter
        to the await the coroutine was suspended on

        :param mod: the loaded script module
        :param c: a fresh coroutine that suspends on a Suspender
        :param awaiter: the function of mod that awaits it
        :return: the awaiter's coroutine, suspended
        """

        self.assertEqual(c.send(None), "suspended")
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


    def test_spliced_coroutine_is_done_from_the_splice(self):
        """
        The coroutine object is finished once its chain is the
        awaiter's: with the awaiter still suspended it refuses a send
        and a throw as a spent coroutine does, and its close does
        nothing
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_value()
        outer = self.spliced(mod, c, mod.await_object)

        with self.assertRaises(RuntimeError) as sent:
            c.send(None)
        self.assertEqual(
            str(sent.exception), "cannot reuse already awaited coroutine")
        with self.assertRaises(RuntimeError) as thrown:
            c.throw(ValueError("t"))
        self.assertEqual(
            str(thrown.exception), "cannot reuse already awaited coroutine")
        self.assertIsNone(c.close())

        self.assertEqual(self.finish(outer), "woken")


    def test_spliced_coroutine_refuses_a_second_await(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_value()
        outer = self.spliced(mod, c, mod.await_twice)
        self.assertEqual(
            self.finish(outer),
            ("woken", "cannot reuse already awaited coroutine"))


    def test_spliced_coroutine_dropped_leaves_the_chain_running(self):
        """
        Nothing holds the coroutine object after the splice, and
        dropping the last reference closes nothing
        """

        mod = asyncio.run(self.load_module())
        log = []
        c = mod.wait_in_extents(log)
        outer = self.spliced(mod, c, mod.await_then_read)
        del c
        gc.collect()
        self.assertEqual(log, ["before"])
        self.assertEqual(self.finish(outer), ("coro", "root"))
        self.assertEqual(log, ["before", "after"])


    def test_shift_across_a_spliced_coroutine_applies_twice(self):
        """
        A shift in the spliced coroutine reaches a reset around the
        await: the continuation holds the rest of the coroutine and of
        the awaiter's reset body, and applies twice
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_then_shift_twice()
        outer = self.spliced(mod, c, mod.reset_await)
        self.assertEqual(self.finish(outer), 111 + 121)


    def test_continuation_across_a_spliced_coroutine_outlives_the_reset(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_then_shift_keep()
        outer = self.spliced(mod, c, mod.reset_await_apply)
        self.assertEqual(self.finish(outer), (102, 103))


    def test_continuation_across_a_spliced_coroutine_is_dropped(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_then_shift_abandon()
        outer = self.spliced(mod, c, mod.reset_await)
        self.assertEqual(self.finish(outer), "abandoned")
        gc.collect()


    def test_callcc_across_a_spliced_coroutine_captures(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_then_callcc()
        outer = self.spliced(mod, c, mod.await_object)
        self.assertEqual(self.finish(outer), 11)


    def test_applied_continuation_suspends_on_its_own_await(self):
        """
        The applied copy of a spliced coroutine's frames awaits as any
        frame does: the awaiter suspends again and its driver completes
        the await
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_shift_wait()
        outer = self.spliced(mod, c, mod.reset_await)
        self.assertEqual(outer.send(None), "suspended")
        self.assertEqual(self.finish(outer), 110)


    def test_splice_resuspends_on_the_latest_forwarded_value(self):
        """
        A Python driver that forwarded into the parked await and got a
        second yield leaves the coroutine parked on that one; the splice
        re-suspends the awaiter on it, not on the first
        """

        mod = asyncio.run(self.load_module())
        c = mod.wait_on(mod.Twice())
        self.assertEqual(c.send(None), "first")
        self.assertEqual(c.send(None), "second")

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "second")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")


    def test_spliced_coroutine_sees_the_awaiters_parameterize(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_then_read()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_under(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "inner")


    def test_throw_from_the_awaiters_driver_arrives_at_the_suspend_point(self):
        mod = asyncio.run(self.load_module())
        c = mod.wait_guarded()
        self.assertEqual(c.send(None), "suspended")

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.throw(ValueError("t"))
        self.assertEqual(done.exception.value, "caught")


    def test_demand_over_a_spliced_chain_refuses_every_frames_await(self):
        """
        A chain parked in a callee splices under a demand drive: the
        callee's await is refused, and so is the await the caller frame
        beneath it makes once the callee returns, since every spliced
        frame carries the demand bit
        """

        mod = asyncio.run(self.load_module())
        c = mod.two_awaits(Suspender(), Suspender())
        self.assertEqual(c.send(None), "suspended")

        d = mod.demand_await(c)
        with self.assertRaises(KoskariSyncViolation):
            d.send(None)


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


    def test_splice_moves_extents_without_running_them(self):
        """
        A coroutine suspended inside a parameterize and a dyn-wind
        brings both with it: the splice is a context switch, so no
        thunk runs for it, the parameter cell is the coroutine's again
        on the awaiter's chain, and the awaiter's own cell is back once
        the extent is left
        """

        mod = asyncio.run(self.load_module())
        log = []
        c = mod.wait_in_extents(log)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["before"])

        outer = mod.await_then_read(c)
        self.assertEqual(outer.send(None), "suspended")
        self.assertEqual(log, ["before"])
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, ("coro", "root"))
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
            c.close()

            d = mod.make_defer()
            outer = mod.await_after_interrupt(d)
            with self.assertRaises(Interrupted):
                outer.send(None)
            d.close()
        finally:
            signal.signal(signal.SIGINT, saved)


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


    def run_out(self, outer):
        """
        Step a suspended coroutine to its return, each step suspending
        on "suspended"

        :param outer: the coroutine
        :return: its value
        """

        with self.assertRaises(StopIteration) as done:
            while True:
                self.assertEqual(outer.send(None), "suspended")
        return done.exception.value


    def spliced_wound(self, mod, c):
        """
        Await c, started from Python and suspended in a segment, from
        Koskari, and step the awaiter to its return

        :param mod: the loaded script module
        :param c: a coroutine stepped once from Python
        :return: the awaiter's value
        """

        outer = mod.await_object(c)
        self.assertEqual(outer.send(None), "suspended")
        return self.run_out(outer)


    def test_splice_in_an_after_segment_at_a_yield(self):
        """
        A coroutine suspended in the after segment a generator's yield
        runs brings the goal leaving the dyn-wind with it: the segment
        finishes on the awaiter's chain, the consumer has the value,
        and the next step enters the dyn-wind again
        """

        mod = asyncio.run(self.load_module())
        log = []
        c = mod.collect(mod.wound(log), log)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["in", "out"])

        expect = ["in", "out", "out2", 1, "in", "out", "out2", 2,
                  "in", "out", "out2"]
        self.assertEqual(self.spliced_wound(mod, c), expect)
        self.assertEqual(log, expect)


    def test_splice_in_a_before_segment_at_reentry(self):
        mod = asyncio.run(self.load_module())
        log = []
        gate = []
        c = mod.collect_gated(mod.wound_on_entry(log, gate), log, gate)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["in", "in2", "out", 1, "in"])

        expect = ["in", "in2", "out", 1, "in", "in2", "out", 2,
                  "in", "in2", "out"]
        self.assertEqual(self.spliced_wound(mod, c), expect)
        self.assertEqual(log, expect)


    def test_splice_in_a_dyn_with_exit_at_a_yield(self):
        mod = asyncio.run(self.load_module())
        log = []
        c = mod.collect(mod.managed(AwaitedExit(log)), log)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["enter", "exit"])

        expect = ["enter", "exit", "exit2", 1, "enter", "exit", "exit2", 2,
                  "enter", "exit", "exit2"]
        self.assertEqual(self.spliced_wound(mod, c), expect)
        self.assertEqual(log, expect)


    def test_splice_in_a_dyn_with_enter_at_reentry(self):
        mod = asyncio.run(self.load_module())
        log = []
        gate = []
        gen = mod.managed(AwaitedEnter(log, gate))
        c = mod.collect_gated(gen, log, gate)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["enter", "exit", 1, "enter"])

        expect = ["enter", "exit", 1, "enter", "enter2", "exit", 2,
                  "enter", "enter2", "exit"]
        self.assertEqual(self.spliced_wound(mod, c), expect)
        self.assertEqual(log, expect)


    def test_splice_in_an_after_segment_of_an_abort(self):
        """
        A shift's abort leaving a dyn-wind whose after segment suspends
        goes on from the awaiter's chain to its reset
        """

        mod = asyncio.run(self.load_module())
        log = []
        c = mod.abort_through_wait(log)
        self.assertEqual(c.send(None), "suspended")
        self.assertEqual(log, ["in", "out"])

        self.assertEqual(self.spliced_wound(mod, c), "aborted")
        self.assertEqual(log, ["in", "out", "out2"])


    def test_continuation_signal_from_a_spliced_coroutine_reaches_its_owner(
            self):
        """
        A coroutine started from Python inside the evaluation that
        captured k invokes k inside a dyn-wind, and is suspended in the
        after segment by the unwind the signal began. Awaited from that
        same evaluation, the after segment finishes, the awaiter's own
        dyn-wind is left, and k takes the value
        """

        mod = asyncio.run(self.load_module())
        log = []
        outer = mod.owner_awaits_jumper(log)
        self.assertEqual(outer.send(None), "suspended")
        self.assertEqual(log, ["in", "out", "suspended", "a-in"])

        expect = ["in", "out", "suspended", "a-in", "out2", "a-out",
                  "jumped"]
        self.assertEqual(self.run_out(outer), expect)
        self.assertEqual(log, expect)


    def test_continuation_signal_from_a_spliced_coroutine_leaves_an_awaiter(
            self):
        """
        Awaited from a coroutine that does not own k, the signal leaves
        that coroutine's extents and its context as it would from any
        call, and k's owner takes the value
        """

        mod = asyncio.run(self.load_module())
        log = []
        with self.assertRaises(StopIteration) as done:
            mod.other_awaits_jumper(log).send(None)

        expect = ["in", "out", "suspended", "a-in", "out2", "a-out",
                  "jumped"]
        self.assertEqual(done.exception.value, expect)
        self.assertEqual(log, expect)


# The end.
