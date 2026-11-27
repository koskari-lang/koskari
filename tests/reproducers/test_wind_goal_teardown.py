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
tests.reproducers.test_wind_goal_teardown

Full-stack reproducer: a generator closed or collected while the goal
that winds it into or out of a dyn-wind or dyn-with is still waiting.
The goal lives in the generator's own storage; the close that cannot
finish gives the goal up with the chain it waits on, and the storage
outlives the goal

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import contextlib
import gc
import os
import sys
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'wind_goal_teardown.ki')


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


@contextlib.contextmanager
def unraisables():
    """
    Collect what sys.unraisablehook is handed while the block runs

    :return: a list of (exception type, message) pairs
    """

    seen = []
    saved = sys.unraisablehook

    def hook(unraisable):
        exc = unraisable.exc_value
        seen.append((type(exc), str(exc)))

    sys.unraisablehook = hook
    try:
        yield seen
    finally:
        sys.unraisablehook = saved


IGNORED = (RuntimeError, "async generator ignored GeneratorExit")


class TestWindGoalTeardown(unittest.TestCase):
    """
    A generator's winding goal given up by a close that cannot finish
    """

    def setUp(self):
        mod = new_module('tests.reproducers.wind_goal_teardown')
        init_module(mod, os.path.abspath(_SCRIPT))
        asyncio.run(load_module_async(mod))
        vars(mod)['Suspender'] = Suspender
        self.mod = mod
        gc.collect()


    def first_step(self, gen):
        """
        Step gen once from Python, to its first yield

        :param gen: the generator
        :return: the value it yielded
        """

        with self.assertRaises(StopIteration) as done:
            gen.__anext__().send(None)
        return done.exception.value


    def test_dropped_at_rest_with_a_before_segment_that_awaits(self):
        """
        Dropped at a yield inside a dyn-wind, the generator's close
        winds back in, and the before segment suspends: the close
        cannot finish, and reports so
        """

        log = []
        gate = []
        gen = self.mod.on_entry(log, gate)
        self.assertEqual(self.first_step(gen), 1)
        gate.append(1)

        with unraisables() as seen:
            del gen
            gc.collect()

        self.assertEqual(log, ["in", "in2", "out", "in"])
        self.assertEqual(seen, [IGNORED])


    def test_dropped_at_rest_with_a_before_segment_that_catches(self):
        log = []
        gate = []
        gen = self.mod.on_entry_stubborn(log, gate)
        self.assertEqual(self.first_step(gen), 1)
        gate.append(1)

        with unraisables() as seen:
            del gen
            gc.collect()

        self.assertEqual(log, ["in", "in2", "out", "in"])
        self.assertEqual(seen, [IGNORED])


    def test_aclose_at_rest_with_a_before_segment_that_awaits(self):
        """
        An aclose of a generator at a yield inside a dyn-wind suspends
        in the before segment; dropped there, the close the drop owes
        cannot finish either
        """

        log = []
        gate = []
        gen = self.mod.on_entry_stubborn(log, gate)
        self.assertEqual(self.first_step(gen), 1)
        gate.append(1)

        closing = gen.aclose()
        self.assertEqual(closing.send(None), "suspended")
        self.assertEqual(log, ["in", "in2", "out", "in"])

        with unraisables() as seen:
            del closing
            del gen
            gc.collect()

        self.assertEqual(log, ["in", "in2", "out", "in", "caught"])
        self.assertEqual(seen, [IGNORED])


    def test_consumer_left_by_a_raise(self):
        """
        A Koskari consumer leaves its for-each by a raise, with the
        generator at a yield inside a dyn-wind whose before segment then
        suspends in the close
        """

        log = []
        gate = []
        c = self.mod.leave_by_raise(self.mod.on_entry(log, gate), log, gate)

        with unraisables() as seen:
            with self.assertRaisesRegex(ValueError, "stop"):
                c.send(None)
            del c
            gc.collect()

        self.assertEqual(log, ["in", "in2", "out", 1, "in"])
        self.assertEqual(seen, [IGNORED])


    def test_collected_in_an_after_segment_that_catches(self):
        """
        Stepped from Python into the after segment its yield runs, and
        dropped there: its goal waits on its own context, and the close
        the collection runs suspends again in the handler
        """

        log = []
        gen = self.mod.on_exit_stubborn(log)
        step = gen.__anext__()
        self.assertEqual(step.send(None), "suspended")
        self.assertEqual(log, ["in", "out"])

        with unraisables() as seen:
            del step
            del gen
            gc.collect()

        self.assertEqual(log, ["in", "out", "caught"])
        self.assertEqual(seen, [IGNORED])


    def test_dropped_at_rest_with_a_dyn_with_enter_that_awaits(self):
        log = []
        gate = []
        gen = self.mod.managed(AwaitedEnter(log, gate))
        self.assertEqual(self.first_step(gen), 1)
        gate.append(1)

        with unraisables() as seen:
            del gen
            gc.collect()

        self.assertEqual(log, ["enter", "exit", "enter"])
        self.assertEqual(seen, [IGNORED])


    def test_step_closed_in_an_after_segment(self):
        """
        A step suspended in the after segment its yield runs, closed:
        GeneratorExit is raised at the segment's await, the segment
        does not finish, the value is never presented, and the
        generator is done
        """

        log = []
        gen = self.mod.on_exit(log)
        step = gen.__anext__()
        self.assertEqual(step.send(None), "suspended")
        self.assertEqual(log, ["in", "out"])

        with unraisables() as seen:
            self.assertIsNone(step.close())

        self.assertEqual(log, ["in", "out"])
        self.assertEqual(seen, [])
        with self.assertRaises(StopAsyncIteration):
            gen.__anext__().send(None)
        self.assertEqual(log, ["in", "out"])


    def test_step_thrown_into_in_a_before_segment(self):
        """
        A step suspended in the before segment that re-enters its
        dyn-wind, thrown into: the exception is raised at the segment's
        await, the extent is never entered and its after segment does
        not run, and the step raises the exception unlinked, since the
        step itself carried no throw
        """

        log = []
        gate = []
        gen = self.mod.on_entry(log, gate)
        self.assertEqual(self.first_step(gen), 1)
        gate.append(1)

        step = gen.__anext__()
        self.assertEqual(step.send(None), "suspended")
        self.assertEqual(log, ["in", "in2", "out", "in"])

        with self.assertRaises(KeyError) as raised:
            step.throw(KeyError("thrown"))
        self.assertIsNone(raised.exception.__cause__)
        self.assertEqual(log, ["in", "in2", "out", "in"])
        with self.assertRaises(StopAsyncIteration):
            gen.__anext__().send(None)
        self.assertEqual(log, ["in", "in2", "out", "in"])


# The end.
