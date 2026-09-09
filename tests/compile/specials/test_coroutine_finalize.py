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
A Koskari coroutine dropped while suspended is closed before its
context is freed: GeneratorExit unwinds its finally blocks, with exits,
and wind after thunks on the demand path, and whatever comes back out
of the close is swallowed rather than escaping into the code that
dropped the last reference.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import gc
import sys

from . import SpecialsTestCase

import pytest


pytestmark = pytest.mark.phase_specials


class Park:
    """
    An awaitable that yields once and completes on the next send
    """

    def __await__(self):
        yield self
        return "parked"


class Recorder:
    """
    A context manager that records its exit
    """

    def __init__(self, log):
        self.log = log

    def __enter__(self):
        self.log.append("enter")
        return self

    def __exit__(self, *exc):
        self.log.append(("exit", exc[0].__name__ if exc[0] else None))
        return False


class Unraisables:
    """
    Collects sys.unraisablehook reports for the extent of a block
    """

    def __init__(self):
        self.seen = []

    def __enter__(self):
        self.prior = sys.unraisablehook
        sys.unraisablehook = lambda u: self.seen.append(u)
        return self

    def __exit__(self, *exc):
        sys.unraisablehook = self.prior
        return False


class TestCoroutineFinalize(SpecialsTestCase):

    def park_in(self, src_str, **defns):
        """
        compile src_str to a function of no arguments, start its
        coroutine, and park it at the first await; returns the coroutine
        """

        expr, mod = self.compile_expr(src_str, park=Park, **defns)
        fn = self.run_sync(expr)
        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        return coro

    def run_sync(self, expr):
        from koskari.lib import demand_call
        return demand_call(expr)

    async def test_finally_runs_when_dropped(self):
        log = []
        src = """
        (lambda []
          (try
            (call park)
            (finally: (call record "finally"))))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, record=log.append)
            self.assertEqual(log, [])
            del coro
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])

    async def test_with_exit_runs_with_generator_exit(self):
        log = []
        src = """
        (lambda []
          (with [(cm)]
            (call park)))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, cm=lambda: Recorder(log))
            self.assertEqual(log, ["enter"])
            del coro
        self.assertEqual(log, ["enter", ("exit", "GeneratorExit")])
        self.assertEqual(reports.seen, [])

    async def test_wind_after_thunk_runs_when_dropped(self):
        log = []
        src = """
        (lambda []
          (dyn-wind
            (call record "before")
            (call park)
            (call record "after")))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, record=log.append)
            self.assertEqual(log, ["before"])
            del coro
        self.assertEqual(log, ["before", "after"])
        self.assertEqual(reports.seen, [])

    async def test_nested_cleanups_run_innermost_first(self):
        log = []
        src = """
        (lambda []
          (try
            (with [(cm)]
              (try
                (call park)
                (finally: (call record "inner"))))
            (finally: (call record "outer"))))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, record=log.append, cm=lambda: Recorder(log))
            del coro
        self.assertEqual(
            log, ["enter", "inner", ("exit", "GeneratorExit"), "outer"])
        self.assertEqual(reports.seen, [])

    async def test_dropped_inside_a_cycle_is_still_closed(self):
        log = []
        src = """
        (lambda []
          (try
            (call park)
            (finally: (call record "finally"))))
        """
        coro = self.park_in(src, record=log.append)

        class Box:
            pass

        box = Box()
        box.coro = coro
        box.self = box
        del coro, box
        gc.collect()
        self.assertEqual(log, ["finally"])

    async def test_cleanup_that_suspends_is_refused_and_reported(self):
        """
        a finally that awaits cannot suspend from a finalizer: it is
        refused in-eval, the enclosing finally still runs, the refusal
        is reported as unraisable, and nothing escapes
        """

        log = []
        src = """
        (lambda []
          (try
            (try
              (call park)
              (finally:
                (call record "inner-start")
                (call park)
                (call record "inner-end")))
            (finally: (call record "outer"))))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, record=log.append)
            del coro
        self.assertEqual(log, ["inner-start", "outer"])
        self.assertEqual(len(reports.seen), 1)
        self.assertEqual(
            type(reports.seen[0].exc_value).__name__, "KoskariSyncViolation")

    async def test_exception_from_cleanup_is_reported_not_raised(self):
        log = []

        def boom():
            raise ValueError("cleanup failed")

        src = """
        (lambda []
          (try
            (call park)
            (finally: (call record "finally") (call boom))))
        """
        with Unraisables() as reports:
            coro = self.park_in(src, record=log.append, boom=boom)
            del coro
        self.assertEqual(log, ["finally"])
        self.assertEqual(len(reports.seen), 1)
        self.assertIsInstance(reports.seen[0].exc_value, ValueError)

    async def test_inner_python_awaitable_is_closed(self):
        log = []

        async def inner():
            try:
                await Park()
            finally:
                log.append("inner-closed")

        src = """
        (lambda []
          (call inner))
        """
        expr, mod = self.compile_expr(src, inner=inner)
        fn = self.run_sync(expr)
        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        with Unraisables() as reports:
            del coro
        self.assertEqual(log, ["inner-closed"])
        self.assertEqual(reports.seen, [])

    async def test_never_started_and_finished_coroutines_do_nothing(self):
        log = []
        src = """
        (lambda []
          (try
            42
            (finally: (call record "finally"))))
        """
        expr, mod = self.compile_expr(src, record=log.append)
        fn = self.run_sync(expr)

        with Unraisables() as reports:
            never = fn()
            del never
        self.assertEqual(log, [])

        done = fn()
        with self.assertRaises(StopIteration) as caught:
            done.send(None)
        self.assertEqual(caught.exception.value, 42)
        self.assertEqual(log, ["finally"])
        with Unraisables() as reports2:
            del done
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen + reports2.seen, [])


# The end.
