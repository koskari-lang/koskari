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
tests.reproducers.test_wind_across_yield

A dyn-wind and a dyn-with around a yield. The generator's yield leaves
the extent, so its after segment or exit runs before the consumer has
the value, innermost first; the next step enters it again, outermost
first, before the body continues. Each segment runs with the chain as
it stands outside its own extent. A try, a finally and a plain with run
nothing at a yield. A coroutine suspended in an await is not a yield:
nothing runs until it is closed.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest

import pytest

from koskari.lib import KoskariSyncViolation, demand_call
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(import-from asyncio sleep)
(import sys)


(def winder [log]
  (dyn-wind
    (before: (log.append "in"))
    (body: (yield 1) (yield 2))
    (after: (log.append "out")))
  (yield 3))


(def collect [gen log]
  (for-each [x gen]
    (log.append x))
  log)


(def step [it log]
  (try
    (log.append (next it))
    ([e ValueError] (log.append (+ "raised:" (str e))))))


(def stepped [log]
  (var it (winder log))
  (step it log)
  (step it log)
  (step it log)
  log)


(def after_raises [log]
  (dyn-wind
    (before: (log.append "in"))
    (body:
      (try
        (yield 1)
        ([e ValueError] (log.append "inner-caught")))
      (log.append "resumed"))
    (after:
      (log.append "out")
      (raise (ValueError "after")))))


(def after_caught [log]
  (try
    (dyn-wind
      (before: (log.append "in"))
      (body:
        (try
          (yield 1)
          ([e ValueError] (log.append "inner-caught")))
        (log.append "resumed"))
      (after:
        (log.append "out")
        (raise (ValueError "after"))))
    ([e ValueError]
      (log.append "outer-caught")
      (yield "recovered"))))


(def stepped_after_raises [log]
  (var it (after_raises log))
  (step it log)
  log)


(def nested [log fail]
  (dyn-wind
    (before: (log.append "a-in"))
    (body:
      (dyn-wind
        (before:
          (log.append "b-in")
          (if (fail.count "b") (raise (ValueError "b")) None))
        (body:
          (dyn-wind
            (before:
              (log.append "c-in")
              (if (fail.count "c") (raise (ValueError "c")) None))
            (body:
              (try
                (yield 1)
                ([e ValueError] (log.append "body-caught")))
              (yield 2))
            (after: (log.append "c-out"))))
        (after: (log.append "b-out"))))
    (after: (log.append "a-out"))))


(def stepped_nested [log fail which]
  (var it (nested log fail))
  (step it log)
  (fail.append which)
  (step it log)
  log)


(def closed [log]
  (var it (winder log))
  (step it log)
  (it.aclose)
  log)


(def catcher [log]
  (dyn-wind
    (before: (log.append "in"))
    (body:
      (try
        (yield 1)
        ([e ValueError]
          (log.append "caught")
          (yield 2))))
    (after: (log.append "out"))))


(def flagged [log fail]
  (dyn-wind
    (before: (log.append "in"))
    (body: (yield 1) (yield 2))
    (after:
      (log.append "out")
      (if (fail.count "after") (raise (ValueError "after")) None))))


(def waiter [log]
  (dyn-wind
    (before: (log.append "in"))
    (body: (await (Suspender)))
    (after: (log.append "out"))))


(def positions [log]
  (dyn-wind
    (before: (log.append #("outer-in" (parameterq p "unset"))))
    (body:
      (parameterize [[p "gen"]]
        (dyn-wind
          (before: (log.append #("inner-in" (parameterq p "unset"))))
          (body: (yield 1) (yield 2))
          (after: (log.append #("inner-out" (parameterq p "unset")))))))
    (after: (log.append #("outer-out" (parameterq p "unset"))))))


(def positions_stepped [log]
  (var it (positions log))
  (parameterize [[p "c1"]]
    (next it)
    (log.append #("consumer" (parameterq p "unset"))))
  (parameterize [[p "c2"]]
    (next it)
    (log.append #("consumer" (parameterq p "unset"))))
  log)


(def delegating [log]
  (log.append "start")
  (yield-from (winder log))
  (yield 4))


(def wrapping [log]
  (dyn-wind
    (before: (log.append "wrap-in"))
    (body: (yield-from (winder log)))
    (after: (log.append "wrap-out")))
  (yield 4))


(def sleepy [log]
  (dyn-wind
    (before: (sleep 0) (log.append "in"))
    (body: (yield 1) (yield 2))
    (after: (log.append "out"))))


(def managed [log]
  (dyn-with [c (Ctx log)]
    (yield 1)
    (yield 2))
  (yield 3))


(def flaky [log fail]
  (try
    (dyn-with [c (Flaky log fail)]
      (yield 1)
      (yield 2))
    ([e ValueError]
      (log.append (+ "caught:" (str e)))
      (yield "recovered"))))


(def local_only [log]
  (with [c (Ctx log)]
    (try
      (begin (yield 1) (yield 2))
      (finally: (log.append "finally"))))
  (yield 3))


(def gated [log gate]
  (dyn-wind
    (before:
      (log.append "in")
      (if (gate.count "await") (await (Suspender)) None))
    (body: (yield 1) (yield 2))
    (after: (log.append "out"))))


(def collect_gated [log gate]
  (for-each [x (gated log gate)]
    (log.append x)
    (gate.append "await"))
  log)


(def deepening [log]
  (dyn-wind
    (before: (log.append "a-in"))
    (body:
      (yield 1)
      (dyn-wind
        (before: (log.append "b-in"))
        (body: (yield 2))
        (after: (log.append "b-out"))))
    (after: (log.append "a-out"))))


(def managed_slow [log]
  (dyn-with [c (SlowCtx log)]
    (yield 1)
    (yield 2))
  (yield 3))


(def slow_after [log]
  (dyn-wind
    (before: (log.append "in"))
    (body: (yield 1) (yield 2))
    (after: (log.append "out") (await (Suspender)) (log.append "out2"))))


(def self_stepping [log box]
  (dyn-wind
    (before:
      (log.append "in")
      (try (next (item box 0))
        ([e RuntimeError] (log.append "in-refused"))))
    (body: (yield 1) (yield 2))
    (after:
      (log.append "out")
      (try (next (item box 0))
        ([e RuntimeError] (log.append "out-refused"))))))


(def between_param [log fail]
  (dyn-wind
    (before:
      (log.append #("outer-in" (parameterq p "unset")))
      (if (fail.count "outer-in") (raise (ValueError "outer-in")) None))
    (body:
      (parameterize [[p "gen"]]
        (dyn-wind
          (before:
            (log.append #("inner-in" (parameterq p "unset")))
            (if (fail.count "inner-in") (raise (ValueError "inner-in")) None))
          (body: (yield 1) (yield 2))
          (after:
            (log.append #("inner-out" (parameterq p "unset")))
            (if (fail.count "inner-out")
              (raise (ValueError "inner-out")) None)))))
    (after:
      (log.append #("outer-out" (parameterq p "unset")))
      (if (fail.count "outer-out") (raise (ValueError "outer-out")) None))))


(def between_try [log fail]
  (dyn-wind
    (before:
      (log.append #("outer-in" (repr (item (sys.exc_info) 1))))
      (if (fail.count "outer-in") (raise (ValueError "outer-in")) None))
    (body:
      (try
        (raise (KeyError "held"))
        ([e KeyError]
          (dyn-wind
            (before:
              (log.append #("inner-in" (repr (item (sys.exc_info) 1))))
              (if (fail.count "inner-in")
                (raise (ValueError "inner-in")) None))
            (body: (yield 1) (yield 2))
            (after:
              (log.append #("inner-out" (repr (item (sys.exc_info) 1))))
              (if (fail.count "inner-out")
                (raise (ValueError "inner-out")) None))))))
    (after:
      (log.append #("outer-out" (repr (item (sys.exc_info) 1))))
      (if (fail.count "outer-out") (raise (ValueError "outer-out")) None))))


(def caught_between [log fail]
  (dyn-wind
    (before: (log.append #("outer-in" (parameterq p "unset"))))
    (body:
      (try
        (parameterize [[p "gen"]]
          (dyn-wind
            (before:
              (log.append #("inner-in" (parameterq p "unset")))
              (if (fail.count "inner-in")
                (raise (ValueError "inner-in")) None))
            (body: (yield 1) (yield 2))
            (after:
              (log.append #("inner-out" (parameterq p "unset")))
              (if (fail.count "inner-out")
                (raise (ValueError "inner-out")) None))))
        ([e ValueError]
          (log.append #("gen-caught" (str e) (parameterq p "unset")))
          (yield "recovered"))))
    (after: (log.append #("outer-out" (parameterq p "unset"))))))


(def flaky_between [log fail]
  (dyn-wind
    (before: (log.append #("outer-in" (parameterq p "unset"))))
    (body:
      (parameterize [[p "gen"]]
        (dyn-with [c (Flaky log fail)]
          (yield 1)
          (yield 2))))
    (after: (log.append #("outer-out" (parameterq p "unset"))))))


(def two_steps [it log fail which]
  (parameterize [[p "c1"]]
    (log.append (next it)))
  (fail.append which)
  (parameterize [[p "c2"]]
    (try
      (log.append (next it))
      ([e ValueError]
        (log.append
          #("caught" (str e) (parameterq p "unset") (repr e.__context__))))))
  (log.append #("after" (parameterq p "unset") (repr (item (sys.exc_info) 1))))
  log)
"""


WOUND = ["in", "out", 1, "in", "out", 2, "in", "out", 3]

HELD = "KeyError('held')"

FIRST_PARAM = [
    ("outer-in", "c1"), ("inner-in", "gen"),
    ("inner-out", "gen"), ("outer-out", "c1"), 1]

FIRST_TRY = [
    ("outer-in", "None"), ("inner-in", HELD),
    ("inner-out", HELD), ("outer-out", "None"), 1]

LEFT = [("after", "unset", "None")]


class Ctx:
    """
    A context manager that logs its enter and its exit
    """

    def __init__(self, log):
        self.log = log


    def __enter__(self):
        self.log.append("enter")
        return self


    def __exit__(self, exc_type, exc, tb):
        self.log.append("exit")
        return False


class Flaky:
    """
    A context manager whose enter or exit raises once its name is in
    fail
    """

    def __init__(self, log, fail):
        self.log = log
        self.fail = fail


    def __enter__(self):
        self.log.append("enter")
        if "enter" in self.fail:
            raise ValueError("enter")
        return self


    def __exit__(self, exc_type, exc, tb):
        self.log.append("exit")
        if "exit" in self.fail:
            raise ValueError("exit")
        return False


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


class SlowCtx:
    """
    A context manager whose enter and exit each log, then hand back an
    awaitable that suspends once
    """

    def __init__(self, log):
        self.log = log


    def __enter__(self):
        self.log.append("enter")
        return Settling(self.log, "entered")


    def __exit__(self, exc_type, exc, tb):
        self.log.append("exit")
        return Settling(self.log, "exited")


class Unraisables:
    """
    Collects sys.unraisablehook reports for the extent of a block
    """

    def __init__(self):
        self.seen = []


    def __enter__(self):
        self.prior = sys.unraisablehook
        sys.unraisablehook = lambda u: self.seen.append(u.exc_value)
        return self


    def __exit__(self, *exc):
        sys.unraisablehook = self.prior
        return False


async def load():
    """
    The module of generators under test, with its Python helpers
    """

    module = new_module("wind_across_yield")
    vars(module)["__file__"] = "<wind_across_yield>"
    vars(module).update(
        Ctx=Ctx, Flaky=Flaky, Suspender=Suspender, SlowCtx=SlowCtx)
    await load_module_source_async(module, SRC)
    return module


async def outcome(awaitable):
    """
    Collapse an await into a comparable tuple
    """

    try:
        return ("value", await awaitable)
    except BaseException as exc:
        return ("raise", type(exc).__name__, str(exc))


async def drain(gen, log):
    """
    Step gen from Python to its end, logging each value after the
    segments its yield ran
    """

    async for value in gen:
        log.append(value)
    return log


async def consume_gated(gen, gate):
    """
    Step gen from Python to its end, setting the gate after each value,
    and return the values
    """

    values = []
    async for value in gen:
        values.append(value)
        gate.append("await")
    return values


class TestWindAcrossYield(unittest.IsolatedAsyncioTestCase):
    """
    A yield leaves the dynamic extents around it and the next step
    enters them again
    """

    async def asyncSetUp(self):
        self.mod = await load()


    async def test_for_each_runs_the_segments_at_every_yield(self):
        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.winder(log), log), WOUND)


    async def test_next_runs_the_segments_at_every_yield(self):
        self.assertEqual(await self.mod.stepped([]), WOUND)


    async def test_python_step_runs_the_segments_at_every_yield(self):
        log = []
        self.assertEqual(await drain(self.mod.winder(log), log), WOUND)


    async def test_after_raising_is_caught_outside_its_extent(self):
        """
        An after segment raising at a yield raises from its dyn-wind:
        a try in the body does not see it, one around the dyn-wind
        does, the value is never presented, and the segment does not
        run a second time
        """

        expect = ["in", "out", "outer-caught", "recovered"]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.after_caught(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.after_caught(log), log), expect)


    async def test_after_raising_uncaught_reaches_the_consumer(self):
        self.assertEqual(
            await self.mod.stepped_after_raises([]),
            ["in", "out", "raised:after"])

        log = []
        gen = self.mod.after_raises(log)
        self.assertEqual(
            await outcome(gen.__anext__()), ("raise", "ValueError", "after"))
        self.assertEqual(log, ["in", "out"])
        self.assertEqual(
            await outcome(gen.__anext__()),
            ("raise", "StopAsyncIteration", ""))
        self.assertEqual(log, ["in", "out"])


    async def test_before_raising_with_two_extents_outside(self):
        """
        A before segment raising at re-entry never enters its body: the
        body's try does not see the exception and its own after segment
        does not run. The two extents entered before it unwind, and the
        exception reaches the step
        """

        first = ["a-in", "b-in", "c-in", "c-out", "b-out", "a-out", 1]
        failed = ["a-in", "b-in", "c-in", "b-out", "a-out"]

        self.assertEqual(
            await self.mod.stepped_nested([], [], "c"),
            first + failed + ["raised:c"])

        log = []
        fail = []
        gen = self.mod.nested(log, fail)
        self.assertEqual(await gen.__anext__(), 1)
        fail.append("c")
        del log[:]
        self.assertEqual(
            await outcome(gen.__anext__()), ("raise", "ValueError", "c"))
        self.assertEqual(log, failed)
        self.assertEqual(
            await outcome(gen.__anext__()),
            ("raise", "StopAsyncIteration", ""))
        self.assertEqual(log, failed)


    async def test_before_raising_with_one_extent_outside(self):
        first = ["a-in", "b-in", "c-in", "c-out", "b-out", "a-out", 1]
        failed = ["a-in", "b-in", "a-out"]

        self.assertEqual(
            await self.mod.stepped_nested([], [], "b"),
            first + failed + ["raised:b"])

        log = []
        fail = []
        gen = self.mod.nested(log, fail)
        self.assertEqual(await gen.__anext__(), 1)
        fail.append("b")
        del log[:]
        self.assertEqual(
            await outcome(gen.__anext__()), ("raise", "ValueError", "b"))
        self.assertEqual(log, failed)


    async def test_close_at_a_yield_runs_each_segment_once(self):
        expect = ["in", "out", 1, "in", "out"]

        self.assertEqual(await self.mod.closed([]), expect)

        log = []
        gen = self.mod.winder(log)
        log.append(await gen.__anext__())
        await gen.aclose()
        self.assertEqual(log, expect)


    async def test_throw_at_a_yield_enters_before_it_raises(self):
        log = []
        gen = self.mod.catcher(log)
        self.assertEqual(await gen.__anext__(), 1)
        self.assertEqual(await gen.athrow(ValueError("thrown")), 2)
        self.assertEqual(log, ["in", "out", "in", "caught", "out"])
        self.assertEqual(
            await outcome(gen.__anext__()),
            ("raise", "StopAsyncIteration", ""))
        self.assertEqual(
            log, ["in", "out", "in", "caught", "out", "in", "out"])


    async def test_throw_uncaught_runs_each_segment_once(self):
        log = []
        gen = self.mod.winder(log)
        self.assertEqual(await gen.__anext__(), 1)
        self.assertEqual(
            await outcome(gen.athrow(KeyError("thrown"))),
            ("raise", "KeyError", "'thrown'"))
        self.assertEqual(log, ["in", "out", "in", "out"])


    async def test_coroutine_closed_in_an_await_runs_only_the_after(self):
        """
        An await is not a yield: the extent stays entered while the
        coroutine is suspended, and closing it runs the after segment
        with no re-entry
        """

        log = []
        coro = self.mod.waiter(log)
        self.assertEqual(coro.send(None), "suspended")
        self.assertEqual(log, ["in"])
        coro.close()
        self.assertEqual(log, ["in", "out"])


    async def test_coroutine_dropped_in_an_await_runs_only_the_after(self):
        log = []
        coro = self.mod.waiter(log)
        self.assertEqual(coro.send(None), "suspended")
        with Unraisables() as reports:
            del coro
            gc.collect()
        self.assertEqual(log, ["in", "out"])
        self.assertEqual(reports.seen, [])


    async def test_each_segment_sees_the_parameter_of_its_position(self):
        """
        The outer dyn-wind is outside the generator's parameterize, so
        its segments read the consumer's cell of that step; the inner
        one is inside it and reads the generator's
        """

        self.assertEqual(await self.mod.positions_stepped([]), [
            ("outer-in", "c1"), ("inner-in", "gen"),
            ("inner-out", "gen"), ("outer-out", "c1"),
            ("consumer", "c1"),
            ("outer-in", "c2"), ("inner-in", "gen"),
            ("inner-out", "gen"), ("outer-out", "c2"),
            ("consumer", "c2"),
        ])


    async def finished(self, gen, log):
        """
        Assert gen is done and that stepping it runs no segment
        """

        del log[:]
        self.assertEqual(
            await outcome(gen.__anext__()),
            ("raise", "StopAsyncIteration", ""))
        self.assertEqual(log, [])


    async def test_inner_after_raising_with_a_parameterize_between(self):
        """
        The parameterize between the two dyn-winds is still in effect
        for the inner after segment that raises; the raise unwinds it,
        so the outer after segment and the step's handler read the
        binding of the step
        """

        log = []
        fail = []
        gen = self.mod.between_param(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "inner-out"),
            FIRST_PARAM + [
                ("outer-in", "c2"), ("inner-in", "gen"),
                ("inner-out", "gen"), ("outer-out", "c2"),
                ("caught", "inner-out", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_outer_after_raising_with_a_parameterize_between(self):
        """
        The parameterize between the two dyn-winds is out when the outer
        after segment raises at a yield: that segment and the step's
        handler read the binding of the step, and the generator is done
        """

        log = []
        fail = []
        gen = self.mod.between_param(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "outer-out"),
            FIRST_PARAM + [
                ("outer-in", "c2"), ("inner-in", "gen"),
                ("inner-out", "gen"), ("outer-out", "c2"),
                ("caught", "outer-out", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_inner_before_raising_with_a_parameterize_between(self):
        """
        The parameterize is back in effect for the inner before segment
        that raises at re-entry; the raise unwinds it, the inner after
        segment does not run, and the outer one reads the binding of
        the step
        """

        log = []
        fail = []
        gen = self.mod.between_param(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "inner-in"),
            FIRST_PARAM + [
                ("outer-in", "c2"), ("inner-in", "gen"),
                ("outer-out", "c2"),
                ("caught", "inner-in", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_outer_before_raising_with_a_parameterize_between(self):
        """
        The outer before segment that raises at re-entry reads the
        binding of the step, the parameterize inside it is never put
        back, no after segment runs, and the generator is done
        """

        log = []
        fail = []
        gen = self.mod.between_param(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "outer-in"),
            FIRST_PARAM + [
                ("outer-in", "c2"),
                ("caught", "outer-in", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_inner_after_raising_with_a_handling_try_between(self):
        """
        The try between the two dyn-winds is still handling its
        exception for the inner after segment that raises, which is the
        new exception's context; the raise ends the handler, and the
        outer after segment and the step see no handled exception
        """

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "inner-out"),
            FIRST_TRY + [
                ("outer-in", "None"), ("inner-in", HELD),
                ("inner-out", HELD), ("outer-out", "None"),
                ("caught", "inner-out", "c2", HELD),
            ] + LEFT)
        await self.finished(gen, log)

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(await gen.__anext__(), 1)
        fail.append("inner-out")
        del log[:]
        with self.assertRaises(ValueError) as raised:
            await gen.__anext__()
        self.assertEqual(str(raised.exception), "inner-out")
        self.assertEqual(repr(raised.exception.__context__), HELD)
        self.assertEqual(log, [
            ("outer-in", "None"), ("inner-in", HELD),
            ("inner-out", HELD), ("outer-out", "None")])
        self.assertIsNone(sys.exc_info()[1])
        await self.finished(gen, log)


    async def test_outer_after_raising_with_a_handling_try_between(self):
        """
        The handling try between the two dyn-winds is out when the outer
        after segment raises at a yield: that segment sees no handled
        exception, the new exception has no context, and the step sees
        no handled exception afterwards
        """

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "outer-out"),
            FIRST_TRY + [
                ("outer-in", "None"), ("inner-in", HELD),
                ("inner-out", HELD), ("outer-out", "None"),
                ("caught", "outer-out", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_inner_before_raising_with_a_handling_try_between(self):
        """
        The try is handling its exception again for the inner before
        segment that raises at re-entry, which is the new exception's
        context; the raise ends the handler before the outer after
        segment runs
        """

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "inner-in"),
            FIRST_TRY + [
                ("outer-in", "None"), ("inner-in", HELD),
                ("outer-out", "None"),
                ("caught", "inner-in", "c2", HELD),
            ] + LEFT)
        await self.finished(gen, log)

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(await gen.__anext__(), 1)
        fail.append("inner-in")
        del log[:]
        with self.assertRaises(ValueError) as raised:
            await gen.__anext__()
        self.assertEqual(str(raised.exception), "inner-in")
        self.assertEqual(repr(raised.exception.__context__), HELD)
        self.assertEqual(log, [
            ("outer-in", "None"), ("inner-in", HELD), ("outer-out", "None")])
        self.assertIsNone(sys.exc_info()[1])
        await self.finished(gen, log)


    async def test_outer_before_raising_with_a_handling_try_between(self):
        """
        The outer before segment that raises at re-entry sees no handled
        exception, the try inside it never handles its exception again,
        the new exception has no context, and the generator is done
        """

        log = []
        fail = []
        gen = self.mod.between_try(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "outer-in"),
            FIRST_TRY + [
                ("outer-in", "None"),
                ("caught", "outer-in", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_raise_between_winders_caught_in_the_generator(self):
        """
        A try between the two dyn-winds, outside the parameterize,
        catches a raise from the inner segments with the parameterize
        unwound; the generator yields again from the handler, leaving
        the outer dyn-wind, and finishes at the next step
        """

        for which, ran in (
                ("inner-out", [("inner-in", "gen"), ("inner-out", "gen")]),
                ("inner-in", [("inner-in", "gen")])):
            log = []
            fail = []
            gen = self.mod.caught_between(log, fail)
            self.assertEqual(
                await self.mod.two_steps(gen, log, fail, which),
                FIRST_PARAM + [("outer-in", "c2")] + ran + [
                    ("gen-caught", which, "c2"), ("outer-out", "c2"),
                    "recovered",
                ] + LEFT)
            del log[:]
            self.assertEqual(
                await outcome(gen.__anext__()),
                ("raise", "StopAsyncIteration", ""))
            self.assertEqual(
                log, [("outer-in", "unset"), ("outer-out", "unset")])


    async def test_dyn_with_raising_with_a_parameterize_between(self):
        """
        An exit at a yield and an enter at re-entry that raise inside a
        parameterize inside a dyn-wind: the raise unwinds the
        parameterize, and the outer after segment reads the binding of
        the step
        """

        first = [("outer-in", "c1"), "enter", "exit", ("outer-out", "c1"), 1]

        log = []
        fail = []
        gen = self.mod.flaky_between(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "exit"),
            first + [
                ("outer-in", "c2"), "enter", "exit", ("outer-out", "c2"),
                ("caught", "exit", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)

        log = []
        fail = []
        gen = self.mod.flaky_between(log, fail)
        self.assertEqual(
            await self.mod.two_steps(gen, log, fail, "enter"),
            first + [
                ("outer-in", "c2"), "enter", ("outer-out", "c2"),
                ("caught", "enter", "c2", "None"),
            ] + LEFT)
        await self.finished(gen, log)


    async def test_yield_from_winds_the_inner_generator(self):
        expect = ["start"] + WOUND + [4]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.delegating(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.delegating(log), log), expect)


    async def test_yield_from_inside_a_dyn_wind_winds_both(self):
        """
        The inner generator leaves its extent at its yield and the
        outer one at its emit; the next step enters the outer, then the
        inner
        """

        step = ["wrap-in", "in", "out", "wrap-out"]
        expect = (["wrap-in", "in", "out", "wrap-out", 1]
                  + step + [2]
                  + step + [3]
                  + ["wrap-in", "wrap-out", 4])

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.wrapping(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.wrapping(log), log), expect)


    async def test_python_step_awaits_in_a_before_segment(self):
        log = []
        self.assertEqual(
            await drain(self.mod.sleepy(log), log),
            ["in", "out", 1, "in", "out", 2, "in", "out"])


    async def test_dyn_with_exits_and_enters_at_every_yield(self):
        expect = ["enter", "exit", 1, "enter", "exit", 2, "enter", "exit", 3]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.managed(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.managed(log), log), expect)


    async def test_dyn_with_exit_raising_at_a_yield(self):
        """
        An exit that raises at a yield raises from its dyn-with, as an
        after segment does: the value is never presented and the exit
        does not run again
        """

        expect = ["enter", "exit", "caught:exit", "recovered"]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.flaky(log, ["exit"]), log),
            expect)

        log = []
        self.assertEqual(
            await drain(self.mod.flaky(log, ["exit"]), log), expect)


    async def test_dyn_with_enter_raising_at_re_entry(self):
        """
        An enter that raises at re-entry never enters its body, and its
        exit does not run for it
        """

        log = []
        fail = []
        gen = self.mod.flaky(log, fail)
        self.assertEqual(await gen.__anext__(), 1)
        fail.append("enter")
        self.assertEqual(await gen.__anext__(), "recovered")
        self.assertEqual(log, ["enter", "exit", "enter", "caught:enter"])
        self.assertEqual(
            await outcome(gen.__anext__()),
            ("raise", "StopAsyncIteration", ""))
        self.assertEqual(log, ["enter", "exit", "enter", "caught:enter"])


    async def test_with_and_finally_run_nothing_at_a_yield(self):
        expect = ["enter", 1, 2, "finally", "exit", 3]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.local_only(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.local_only(log), log), expect)


    async def test_a_later_yield_under_more_winders(self):
        """
        The second yield is under two dyn-winds, the first under one:
        each yield leaves every extent around it and each step enters
        them again, outermost first
        """

        expect = ["a-in", "a-out", 1,
                  "a-in", "b-in", "b-out", "a-out", 2,
                  "a-in", "b-in", "b-out", "a-out"]

        log = []
        self.assertEqual(
            await self.mod.collect(self.mod.deepening(log), log), expect)

        log = []
        self.assertEqual(await drain(self.mod.deepening(log), log), expect)


    async def test_segment_stepping_its_own_generator_is_refused(self):
        """
        A segment that steps the generator it winds is refused as a
        re-entrant step is: at the first entry, at each yield's leave
        and at each step's re-entry. The generator goes on undisturbed
        """

        seg = ["in", "in-refused", "out", "out-refused"]
        expect = seg + [1] + seg + [2] + seg

        log = []
        box = []
        gen = self.mod.self_stepping(log, box)
        box.append(gen)
        self.assertEqual(await self.mod.collect(gen, log), expect)

        log = []
        box = []
        gen = self.mod.self_stepping(log, box)
        box.append(gen)
        self.assertEqual(await drain(gen, log), expect)


class TestWindAcrossYieldFinalizer(unittest.TestCase):
    """
    A generator dropped at a yield inside a dyn-wind is closed where it
    is dropped: the before segment runs, GeneratorExit is raised at the
    yield, and the after segment runs. No event loop is running, so no
    hook defers the close
    """

    def setUp(self):
        self.mod = asyncio.run(load())


    def test_finalizer_at_a_yield_runs_each_segment_once(self):
        log = []
        it = iter(self.mod.winder(log))
        self.assertEqual(next(it), 1)
        with Unraisables() as reports:
            del it
            gc.collect()
        self.assertEqual(log, ["in", "out", "in", "out"])
        self.assertEqual(reports.seen, [])


    def test_finalizer_reports_a_raising_segment(self):
        log = []
        fail = []
        it = iter(self.mod.flagged(log, fail))
        self.assertEqual(next(it), 1)
        fail.append("after")
        with Unraisables() as reports:
            del it
            gc.collect()
        self.assertEqual(log, ["in", "out", "in", "out"])
        self.assertEqual(
            [(type(e).__name__, str(e)) for e in reports.seen],
            [("ValueError", "after")])


class TestWindAcrossYieldDemand(unittest.TestCase):
    """
    The re-entry of a winder runs under the demand of the step that
    enters it again: a before segment that suspends at re-entry is
    refused under demand, and suspends under an async consumer. The
    before segment suspends only once the gate is set, after the first
    value
    """

    def setUp(self):
        self.mod = asyncio.run(load())


    def test_re_entry_under_demand_in_a_for_each(self):
        """
        A Koskari for-each run under demand_call steps the generator on
        a running context
        """

        log = []
        with self.assertRaises(KoskariSyncViolation) as raised:
            demand_call(self.mod.collect_gated, log, [])
        self.assertEqual(
            str(raised.exception),
            "async suspension is not allowed in demand_sync")
        self.assertEqual(log, ["in", "out", 1, "in"])


    def test_re_entry_under_a_python_step(self):
        """
        next() from Python is a demand step on the generator's own
        context, which is not running at the install
        """

        log = []
        gate = []
        it = iter(self.mod.gated(log, gate))
        self.assertEqual(next(it), 1)
        gate.append("await")
        with self.assertRaises(KoskariSyncViolation) as raised:
            next(it)
        self.assertEqual(
            str(raised.exception),
            "async suspension is not allowed in demand_sync")
        self.assertEqual(log, ["in", "out", "in"])


    def test_re_entry_under_an_async_consumer_suspends(self):
        """
        With no demand the same re-entry suspends at each step after
        the first, and the generator completes
        """

        log = []
        gate = []
        coro = consume_gated(self.mod.gated(log, gate), gate)
        suspended = []
        with self.assertRaises(StopIteration) as done:
            while True:
                suspended.append(coro.send(None))
        self.assertEqual(suspended, ["suspended", "suspended"])
        self.assertEqual(done.exception.value, [1, 2])
        self.assertEqual(log, ["in", "out", "in", "out", "in", "out"])


    def test_dyn_with_awaits_in_its_exit_and_its_enter(self):
        """
        An exit that hands back an awaitable suspends the yield's leave
        and an enter that does suspends the step's re-entry; each
        completes before the value is presented or the body goes on
        """

        log = []
        coro = drain(self.mod.managed_slow(log), log)
        suspended = []
        with self.assertRaises(StopIteration) as done:
            while True:
                suspended.append(coro.send(None))
        self.assertEqual(suspended, ["suspended"] * 6)
        self.assertIs(done.exception.value, log)
        self.assertEqual(log, [
            "enter", "entered", "exit", "exited", 1,
            "enter", "entered", "exit", "exited", 2,
            "enter", "entered", "exit", "exited", 3])


    def test_wind_out_under_a_python_step(self):
        """
        next() from Python is a demand step: the after segment a yield
        runs is refused at its await, the value is never presented, and
        the raise leaves through the generator, which is done
        """

        log = []
        it = iter(self.mod.slow_after(log))
        with self.assertRaises(KoskariSyncViolation) as raised:
            next(it)
        self.assertEqual(
            str(raised.exception),
            "async suspension is not allowed in demand_sync")
        self.assertEqual(log, ["in", "out"])
        with self.assertRaises(StopIteration):
            next(it)
        self.assertEqual(log, ["in", "out"])


# The end.
