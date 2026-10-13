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
"""


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
        for _ in range(5):
            agen = mod["waits"](log.append)
            self.assertEqual(await agen.__anext__(), 1)
            del agen
        self.assertEqual(live_wrappers(), before)


# The end.
