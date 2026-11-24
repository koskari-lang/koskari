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
tests.reproducers.test_started_object_await

Full-stack reproducer: a Koskari await of a Koskari coroutine, or of an
async generator step, that something else has already started -- an
asyncio Task, a Python coroutine awaiting it, a hand send -- is
refused with RuntimeError, as CPython refuses a second awaiter, and the
object is left to whoever started it, which completes it. A demand
that cannot complete synchronously closes the coroutine it started.
Where CPython has the same shape, the expected refusal is CPython's
own, taken from the equivalent Python coroutine or async generator on
the running version. Every shape runs under asyncio.wait_for, so a
regression fails instead of hanging.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import sys
import unittest

import pytest

from koskari.lib import KoskariSyncViolation, defer, demand_call
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'started_object_await.ki')

BOUND = 5


class Suspender:
    """
    A Python awaitable that suspends once with "suspended" and
    completes with "woken"
    """

    def __await__(self):
        yield "suspended"
        return "woken"


async def py_sleeper(n):
    await asyncio.sleep(n)
    return "woke"


async def py_catch_await(aw):
    try:
        return await aw
    except BaseException as e:
        return ("raised", e)


async def py_waiting_gen(log):
    try:
        yield await asyncio.sleep(0.01, "slept")
        yield await asyncio.sleep(0.01, "slept again")
    finally:
        log.append("finally")


def refusal(result):
    """
    The refusal a catching awaiter came back with

    :param result: what the awaiter returned: ("raised", exception), or
      a value
    :return: (exception type, message), or None for a value
    """

    if isinstance(result, tuple) and result and result[0] == "raised":
        return (type(result[1]), str(result[1]))
    return None


def bounded(main):
    """
    Run a coroutine function under asyncio.run, bounded by BOUND seconds

    :param main: the coroutine function
    :return: its value
    """

    return asyncio.run(asyncio.wait_for(main(), BOUND))


async def load_module():
    mod = new_module('tests.reproducers.started_object_await')
    init_module(mod, os.path.abspath(_SCRIPT))
    await load_module_async(mod)
    vars(mod)['Suspender'] = Suspender
    return mod


async def task_owned(make, catch):
    c = make(0.05)
    t = asyncio.create_task(c)
    await asyncio.sleep(0)
    r = await catch(c)
    return (refusal(r), await t)


async def gather_owned(make, catch):
    c = make(0.05)
    g = asyncio.gather(c)
    await asyncio.sleep(0)
    r = await catch(c)
    return (refusal(r), await g)


async def shield_owned(make, catch):
    c = make(0.05)
    s = asyncio.shield(c)
    await asyncio.sleep(0)
    r = await catch(c)
    return (refusal(r), await s)


async def eager_task_owned(make, catch):
    asyncio.get_running_loop().set_task_factory(asyncio.eager_task_factory)
    c = make(0.05)
    t = asyncio.create_task(c)
    r = await catch(c)
    return (refusal(r), await t)


async def python_awaiter_owned(make, catch):
    c = make(0.05)

    async def owner():
        return await c

    t = asyncio.create_task(owner())
    await asyncio.sleep(0)
    r = await catch(c)
    return (refusal(r), await t)


async def hand_sent(make, catch):
    c = make(0)
    c.send(None)
    r = await catch(c)
    try:
        c.send(None)
    except StopIteration as done:
        return (refusal(r), done.value)
    return (refusal(r), "not done")


async def hand_sent_through_await(make, catch):
    c = make(0)
    w = c.__await__()
    w.send(None)
    r = await catch(c)
    try:
        w.send(None)
    except StopIteration as done:
        return (refusal(r), done.value)
    return (refusal(r), "not done")


async def task_owned_step(gen, step, catch):
    log = []
    g = gen(log)
    st = step(g)
    t = asyncio.ensure_future(st)
    await asyncio.sleep(0)
    r = await catch(st)
    owner = await t
    await g.aclose()
    return (refusal(r), owner, log)


def step_out(awaitable):
    """
    Step an awaitable from Python to its end, each step suspending on
    "suspended"

    :return: ("value", result), or ("raise", exception type)
    """

    try:
        while True:
            assert awaitable.send(None) == "suspended"
    except StopIteration as done:
        return ("value", done.value)
    except Exception as exc:
        return ("raise", type(exc))


class TestStartedCoroutineAwait(unittest.TestCase):
    """
    A Koskari await of a started Koskari coroutine is refused as
    CPython refuses a second awaiter, and its owner completes it
    """

    def setUp(self):
        self.mod = asyncio.run(load_module())


    def compare(self, shape):
        """
        Run shape with a Koskari coroutine awaited from Koskari and with
        a Python coroutine awaited from Python, and check they agree

        :param shape: a coroutine function of (make, catch)
        :return: the Koskari outcome
        """

        mod = self.mod
        found = bounded(lambda: shape(mod.sleeper, mod.catch_await))
        expect = bounded(lambda: shape(py_sleeper, py_catch_await))
        self.assertIsNotNone(expect[0])
        self.assertIs(expect[0][0], RuntimeError)
        self.assertEqual(found, expect)
        return found


    def test_task_owned_coroutine_is_refused(self):
        self.assertEqual(
            self.compare(task_owned),
            ((RuntimeError, "coroutine is being awaited already"), "woke"))


    def test_gathered_coroutine_is_refused(self):
        self.assertEqual(
            self.compare(gather_owned),
            ((RuntimeError, "coroutine is being awaited already"), ["woke"]))


    def test_shielded_coroutine_is_refused(self):
        self.compare(shield_owned)


    def test_eager_task_owned_coroutine_is_refused(self):
        """
        asyncio.eager_task_factory exists from Python 3.12; before it,
        its absence is the assertion
        """

        if sys.version_info < (3, 12):
            self.assertFalse(hasattr(asyncio, 'eager_task_factory'))
            return
        self.compare(eager_task_owned)


    def test_python_awaiter_owned_coroutine_is_refused(self):
        self.compare(python_awaiter_owned)


    def test_hand_sent_coroutine_is_refused(self):
        self.compare(hand_sent)


    def test_coroutine_sent_through_its_await_is_refused(self):
        self.compare(hand_sent_through_await)


    def test_defer_over_a_started_coroutine_is_refused(self):
        mod = self.mod

        async def main(make, catch, wrap):
            c = make(0.05)
            t = asyncio.create_task(c)
            await asyncio.sleep(0)
            r = await catch(wrap(c))
            return (refusal(r), await t)

        found = bounded(lambda: main(
            mod.sleeper, mod.catch_await, lambda c: defer(lambda: c)))
        expect = bounded(lambda: main(
            py_sleeper, py_catch_await, lambda c: c))
        self.assertEqual(found, expect)


class TestStartedStepAwait(unittest.TestCase):
    """
    A Koskari await of a started async generator step is refused with
    the reuse error CPython gives its method, and its owner completes it
    """

    def setUp(self):
        self.mod = asyncio.run(load_module())


    def compare(self, step):
        """
        Run a Task-owned step of a Koskari generator awaited from Koskari
        and of a Python async generator awaited from Python

        :param step: a function of the generator, answering the step
        :return: the Koskari outcome
        """

        mod = self.mod
        found = bounded(lambda: task_owned_step(
            mod.waiting_gen, step, mod.catch_await))
        expect = bounded(lambda: task_owned_step(
            py_waiting_gen, step, py_catch_await))
        self.assertIsNotNone(expect[0])
        self.assertIs(expect[0][0], RuntimeError)
        self.assertEqual(found, expect)
        return found


    def test_task_owned_anext_is_refused(self):
        self.assertEqual(
            self.compare(lambda g: g.__anext__()),
            ((RuntimeError,
              "cannot reuse already awaited __anext__()/asend()"),
             "slept", ["finally"]))


    def test_task_owned_asend_is_refused(self):
        self.compare(lambda g: g.asend(None))


    def test_task_owned_aclose_is_refused(self):
        mod = self.mod

        async def main():
            log = []
            g = mod.waiting_gen(log)
            await g.__anext__()
            st = g.aclose()
            t = asyncio.ensure_future(st)
            await asyncio.sleep(0)
            r = await mod.catch_await(st)
            return (refusal(r), await t, log)

        self.assertEqual(
            bounded(main),
            ((RuntimeError, "cannot reuse already awaited aclose()/athrow()"),
             None, ["finally"]))


    def test_task_owned_athrow_is_refused(self):
        mod = self.mod

        async def main():
            g = mod.recovering_waiting_gen()
            await g.__anext__()
            st = g.athrow(ValueError("t"))
            t = asyncio.ensure_future(st)
            await asyncio.sleep(0)
            r = await mod.catch_await(st)
            owner = await t
            await g.aclose()
            return (refusal(r), owner)

        self.assertEqual(
            bounded(main),
            ((RuntimeError, "cannot reuse already awaited aclose()/athrow()"),
             "recovered"))


    def hand_started(self, g, st):
        """
        Step st once from Python, await it from Koskari, and step it to
        its end from Python

        :return: (the refusal, the step's outcome)
        """

        self.assertEqual(st.send(None), "suspended")
        r = bounded(lambda: self.mod.catch_await(st))
        return (refusal(r), step_out(st))


    def test_hand_sent_anext_is_refused(self):
        g = self.mod.suspending_gen()
        self.assertEqual(
            self.hand_started(g, g.__anext__()),
            ((RuntimeError,
              "cannot reuse already awaited __anext__()/asend()"),
             ("value", "woken")))
        asyncio.run(g.aclose())


    def test_hand_sent_asend_is_refused(self):
        g = self.mod.suspending_gen()
        self.assertEqual(
            self.hand_started(g, g.asend(None)),
            ((RuntimeError,
              "cannot reuse already awaited __anext__()/asend()"),
             ("value", "woken")))
        asyncio.run(g.aclose())


    def test_hand_sent_athrow_is_refused(self):
        g = self.mod.recovering_gen()
        self.assertEqual(step_out(g.__anext__()), ("value", 1))
        self.assertEqual(
            self.hand_started(g, g.athrow(ValueError("t"))),
            ((RuntimeError, "cannot reuse already awaited aclose()/athrow()"),
             ("value", "woken")))
        asyncio.run(g.aclose())


    def test_hand_sent_aclose_is_refused(self):
        log = []
        g = self.mod.closing_gen(log)
        self.assertEqual(step_out(g.__anext__()), ("value", 1))
        self.assertEqual(
            self.hand_started(g, g.aclose()),
            ((RuntimeError, "cannot reuse already awaited aclose()/athrow()"),
             ("value", None)))
        self.assertEqual(log, ["finally"])


class TestFailedDemandCloses(unittest.TestCase):
    """
    A demand that meets a suspension closes the coroutine it started,
    as if it had run and failed asynchronously
    """

    def setUp(self):
        self.mod = asyncio.run(load_module())


    def assert_spent(self, c):
        """
        c refuses a Koskari await and a Python await as a finished
        coroutine does, with CPython's message for one
        """

        async def py_done():
            return None

        done = py_done()
        asyncio.run(done)
        expect = refusal(bounded(lambda: py_catch_await(done)))
        self.assertEqual(expect, (
            RuntimeError, "cannot reuse already awaited coroutine"))

        self.assertEqual(
            refusal(bounded(lambda: self.mod.catch_await(c))), expect)
        self.assertEqual(
            refusal(bounded(lambda: py_catch_await(c))), expect)


    def test_coroutine_demand_closes_what_it_started(self):
        log = []
        c = self.mod.guarded_waiter(log)
        with self.assertRaises(RuntimeError) as caught:
            c.demand()
        self.assertEqual(
            str(caught.exception),
            "async suspension is not allowed in coroutine.demand()")
        self.assertEqual(log, ["finally"])
        self.assert_spent(c)


    def test_demand_root_closes_a_returned_coroutine(self):
        log = []
        c = self.mod.guarded_waiter(log)
        with self.assertRaises(KoskariSyncViolation):
            demand_call(self.mod.give, c)
        self.assertEqual(log, ["finally"])
        self.assert_spent(c)


    def test_demand_closes_an_awaitable_it_stepped(self):
        """
        An await inside a demand that suspends is refused, and the
        Python coroutine the await stepped is closed
        """

        log = []

        async def guarded():
            try:
                return await Suspender()
            finally:
                log.append("finally")

        c = guarded()
        with self.assertRaises(KoskariSyncViolation):
            demand_call(self.mod.await_object, c)
        self.assertEqual(log, ["finally"])
        self.assertIsNone(c.cr_frame)


# The end.
