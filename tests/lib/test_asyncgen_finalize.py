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
tests.lib.test_asyncgen_finalize

An async generator dropped while suspended is closed by its finalizer,
before any clearing, so its finally body runs against its frame as it
was: on a plain drop, and inside a reference cycle collected by gc

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import sys
import unittest

import pytest

from koskari.lib import AsyncGenerator
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
import-from asyncio sleep

define [produce box record]:
    try:
        (yield 1)
    finally:
        (record box.tag)

define [waits record]:
    try:
        (yield 1)
    finally:
        (await (sleep 0))
        (record "finally")

define [yields_again record]:
    try:
        (yield 1)
    finally:
        (record "fin-start")
        (yield 2)
        (record "fin-end")

define [break_waits record]:
    (for-each [x (waits record)] (break))
    "ok"
"""


IGNORED = (
    RuntimeError,
    "async generator ignored GeneratorExit",
    None,
    AsyncGenerator)


class Unraisables:
    """
    Collects sys.unraisablehook reports for the extent of a block, each
    as the exception type and message, the hook message, and the type
    of the object, so no report keeps the object alive
    """

    def __init__(self):
        self.seen = []

    def record(self, u):
        self.seen.append((
            type(u.exc_value),
            str(u.exc_value),
            u.err_msg,
            type(u.object)))

    def __enter__(self):
        self.prior = sys.unraisablehook
        sys.unraisablehook = self.record
        return self

    def __exit__(self, *exc):
        sys.unraisablehook = self.prior
        return False


class Box:
    tag = "finally"


def live_wrappers():
    gc.collect()
    return sum(1 for o in gc.get_objects() if isinstance(o, AsyncGenerator))


class TestAsyncGenFinalize(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module("asyncgen_finalize")
        vars(mod)["__file__"] = "<asyncgen_finalize>"
        await load_module_source_async(mod, SRC)
        return vars(mod)

    async def suspended(self, log, box):
        """
        A generator suspended at its yield inside try/finally, with box as
        its first argument; the finally reads box
        """

        mod = await self.load()
        agen = mod["produce"](box, log.append)
        self.assertEqual(await agen.__anext__(), 1)
        return agen

    async def test_dropped_suspended_generator_runs_its_finally(self):
        log = []
        with Unraisables() as reports:
            agen = await self.suspended(log, Box())
            del agen
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])

    async def test_dropped_suspended_generators_run_their_finally_each_time(self):
        """
        A generator of the same function after one was dropped reuses
        that generator's record; dropped suspended, it is closed the
        same way, finally body and all
        """

        log = []
        with Unraisables() as reports:
            for _ in range(3):
                agen = await self.suspended(log, Box())
                del agen
        self.assertEqual(log, ["finally"] * 3)
        self.assertEqual(reports.seen, [])

    async def test_dropped_inside_a_cycle_runs_its_finally(self):
        """
        The generator's frame holds box in an argument slot, and box
        holds the generator: unreachable together, the finally body
        still reads box whole, which a close run from clearing could
        not promise
        """

        log = []
        box = Box()
        with Unraisables() as reports:
            agen = await self.suspended(log, box)
            box.agen = agen
            del agen, box
            gc.collect()
        self.assertEqual(log, ["finally"])
        self.assertEqual(reports.seen, [])

    async def test_finally_that_awaits_does_not_keep_the_wrapper_alive(self):
        """
        The close cannot honour an await inside finally; the abandoned
        chain is freed with the context and the wrapper goes with it
        """

        mod = await self.load()
        log = []
        before = live_wrappers()
        with Unraisables() as reports:
            for _ in range(5):
                agen = mod["waits"](log.append)
                self.assertEqual(await agen.__anext__(), 1)
                del agen
        self.assertEqual(live_wrappers(), before)
        self.assertEqual(reports.seen, [IGNORED] * 5)

    async def test_dropped_generator_whose_finally_awaits_is_reported(self):
        """
        A finally that awaits cannot complete in the synchronous close; the
        rest of it is dropped and reported as an ignored GeneratorExit
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["waits"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
        self.assertEqual(log, [])
        self.assertEqual(reports.seen, [IGNORED])

    async def test_dropped_generator_whose_finally_yields_is_reported(self):
        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["yields_again"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
        self.assertEqual(log, ["fin-start"])
        self.assertEqual(reports.seen, [IGNORED])

    async def test_break_from_generator_whose_finally_awaits_is_reported(self):
        """
        A for-each that breaks closes its generator; a finally that awaits
        is reported the same way as on a drop
        """

        mod = await self.load()
        log = []
        with Unraisables() as reports:
            self.assertEqual(await mod["break_waits"](log.append), "ok")
        self.assertEqual(log, [])
        self.assertEqual(reports.seen, [IGNORED])

    async def test_dropped_sync_view_whose_finally_awaits_is_reported(self):
        mod = await self.load()
        log = []
        with Unraisables() as reports:
            agen = mod["waits"](log.append)
            view = iter(agen)
            self.assertEqual(next(view), 1)
            del view, agen
        self.assertEqual(log, [])
        self.assertEqual(reports.seen, [IGNORED])


# The end.
