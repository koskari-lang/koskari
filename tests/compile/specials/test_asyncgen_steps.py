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
Koskari code stepping a Koskari async generator through __anext__,
asend, athrow and aclose runs the step flattened on the consumer's own
chain, as for-each and next do: the generator sees the consumer's
dynamic state, and the awaitable the adapter returned is spent once
the step has been taken.
"""


import asyncio
import signal
import unittest

import pytest

from koskari.lib import get_frame_depth_limit, set_frame_depth_limit
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_specials


SRC = """
(import _thread)
(import signal)

(define-parameter p "root")

(def pgen []
  (while True
    (yield p)))

(def via_anext []
  (var g (pgen))
  (var out #[])
  (parameterize [[p "one"]]
    (out.append (await (g.__anext__))))
  (parameterize [[p "two"]]
    (out.append (await (g.__anext__))))
  out)

(def via_foreach []
  (var g (pgen))
  (var out #[])
  (parameterize [[p "one"]]
    (for-each [x g] (out.append x) (break)))
  (parameterize [[p "two"]]
    (for-each [x g] (out.append x) (break)))
  out)

(def echo [log]
  (try
    (begin
      (var x (yield 1))
      (yield (+ x 100))
      (yield 3))
    ([e ValueError]
      (log.append "caught")
      (yield "recovered"))
    (finally:
      (log.append "fin"))))

(def do_asend [g]
  (await (g.__anext__))
  (await (g.asend 42)))

(def do_athrow [g]
  (await (g.__anext__))
  (await (g.athrow (ValueError "t"))))

(def do_aclose [g]
  (await (g.__anext__))
  (await (g.aclose)))

(def two [] (yield 1) (yield 2))

(def run_dry [g]
  (await (g.__anext__))
  (await (g.__anext__))
  (await (g.__anext__)))

(def after_end [g]
  (await (g.__anext__))
  (await (g.__anext__))
  (try
    (await (g.__anext__))
    ([e StopAsyncIteration] None))
  #((await (g.athrow (ValueError "late"))) (await (g.aclose))))

(def take_stepper [st]
  (await st))

(def base [] (yield 1))

(def layer [inner]
  (yield (await (inner.__anext__))))

(def build [n]
  (var g (base))
  (var i 0)
  (while (< i n)
    (set! g (layer g))
    (set! i (+ i 1)))
  g)

(def take_first [g]
  (for-each [x g] (return x)))

(def wait_gen []
  (yield (await (Suspender)))
  (yield 2))

(def wait_pgen []
  (await (Suspender))
  (yield p))

(def take_under [st]
  (parameterize [[p "inner"]]
    (await st)))

(def wait_on_gen [aw]
  (yield (await aw))
  (yield 2))

(def take_after_interrupt [st]
  (_thread.interrupt_main signal.SIGUSR1)
  (await st))
"""


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


class Interrupted(Exception):
    """
    Raised by the SIGUSR1 handler the signal test installs
    """


def raise_interrupted(signum, frame):
    raise Interrupted(signum)


class TestKoskariStepsAreFlattened(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("asyncgen_steps")
        vars(module)["__file__"] = "<asyncgen_steps>"
        vars(module)["Suspender"] = Suspender
        vars(module)["Twice"] = Twice
        await load_module_source_async(module, SRC)
        return vars(module)

    def test_signal_pending_at_the_await_of_a_step_raises_at_the_site(self):
        v = asyncio.run(self.load())
        saved = signal.signal(signal.SIGUSR1, raise_interrupted)
        try:
            g = v["two"]()
            st = g.__anext__()
            outer = v["take_after_interrupt"](st)
            with self.assertRaises(Interrupted):
                outer.send(None)
            st.close()
        finally:
            signal.signal(signal.SIGUSR1, saved)
        asyncio.run(g.aclose())

    async def test_step_parked_mid_await_is_spliced(self):
        """
        A step Python started and left parked at an await inside the
        generator splices under the awaiter; the awaiter suspends on the
        same await, its driver completes it, and the generator's yield
        is the awaiter's value
        """

        v = await self.load()
        g = v["wait_gen"]()
        st = g.__anext__()
        self.assertEqual(st.send(None), "suspended")

        outer = v["take_stepper"](st)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(RuntimeError):
            st.send(None)
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")

        self.assertEqual(2, await g.__anext__())
        await g.aclose()

    async def test_step_splice_resuspends_on_the_latest_forwarded_value(self):
        v = await self.load()
        g = v["wait_on_gen"](Twice())
        st = g.__anext__()
        self.assertEqual(st.send(None), "first")
        self.assertEqual(st.send(None), "second")

        outer = v["take_stepper"](st)
        self.assertEqual(outer.send(None), "second")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")
        self.assertEqual(2, await g.__anext__())
        await g.aclose()

    async def test_spliced_step_sees_the_awaiters_parameterize(self):
        v = await self.load()
        g = v["wait_pgen"]()
        st = g.__anext__()
        self.assertEqual(st.send(None), "suspended")

        outer = v["take_under"](st)
        self.assertEqual(outer.send(None), "suspended")
        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "inner")
        await g.aclose()

    async def test_generator_is_busy_while_its_spliced_step_is_suspended(self):
        v = await self.load()
        g = v["wait_gen"]()
        st = g.__anext__()
        self.assertEqual(st.send(None), "suspended")

        outer = v["take_stepper"](st)
        self.assertEqual(outer.send(None), "suspended")
        second = g.__anext__()
        with self.assertRaises(RuntimeError):
            second.send(None)

        with self.assertRaises(StopIteration) as done:
            outer.send(None)
        self.assertEqual(done.exception.value, "woken")
        self.assertEqual(2, await g.__anext__())
        await g.aclose()

    async def test_anext_sees_the_consumers_parameterize(self):
        v = await self.load()
        self.assertEqual(["one", "two"], await v["via_foreach"]())
        self.assertEqual(["one", "two"], await v["via_anext"]())

    async def test_asend_delivers_its_value(self):
        v = await self.load()
        log = []
        self.assertEqual(142, await v["do_asend"](v["echo"](log)))

    async def test_athrow_is_caught_inside(self):
        v = await self.load()
        log = []
        g = v["echo"](log)
        self.assertEqual("recovered", await v["do_athrow"](g))
        self.assertEqual(["caught"], log)
        await g.aclose()

    async def test_aclose_runs_finally_and_answers_none(self):
        v = await self.load()
        log = []
        self.assertIsNone(await v["do_aclose"](v["echo"](log)))
        self.assertEqual(["fin"], log)

    async def test_anext_past_the_end_raises_stop_async_iteration(self):
        v = await self.load()
        with self.assertRaises(StopAsyncIteration):
            await v["run_dry"](v["two"]())

    async def test_athrow_and_aclose_after_the_end_answer_none(self):
        v = await self.load()
        self.assertEqual((None, None), await v["after_end"](v["two"]()))

    async def test_stepper_awaited_from_koskari_is_spent(self):
        v = await self.load()
        g = v["two"]()
        st = g.__anext__()
        self.assertEqual(1, await v["take_stepper"](st))
        with self.assertRaises(RuntimeError) as caught:
            await st
        self.assertIn("cannot reuse", str(caught.exception))
        await g.aclose()

    async def test_deep_nesting_is_bounded_by_the_frame_room(self):
        """
        Each layer's await is a step on the consumer's own chain, so a
        chain of them meets the frame-room check and raises
        RecursionError instead of exhausting the C stack. The limit is
        lowered below the chain's depth for the step, so the test holds
        whatever the default limit is
        """

        v = await self.load()
        g = await v["build"](1200)
        saved = get_frame_depth_limit()
        set_frame_depth_limit(200)
        try:
            with self.assertRaises(RecursionError):
                await v["take_first"](g)
        finally:
            set_frame_depth_limit(saved)


# The end.
