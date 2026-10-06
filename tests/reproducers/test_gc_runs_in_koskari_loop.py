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
tests.reproducers.test_gc_runs_in_koskari_loop

The cyclic collector runs during a stretch of Koskari execution that
never returns to Python's own loop. On 3.12 and later the interpreter
schedules a collection by raising a breaker bit and runs it only from
its eval loop or from PyErr_CheckSignals, so the evaluator's breaker
service has to run it; before 3.12 the allocator runs it on its own.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
import gc

define churn [count]:
    define n 0
    while (< n count):
        define l #[]
        (l.append l)
        set! n (+ n 1)
    (gc.get_count)
"""


class TestGcRunsInKoskariLoop(unittest.IsolatedAsyncioTestCase):

    async def test_cycles_are_collected_during_the_loop(self):
        module = new_module("gc_loop")
        vars(module)["__file__"] = "<gc_loop>"
        await load_module_source_async(module, SRC)
        churn = vars(module)["churn"]

        # the count is read inside the loop's own stretch, before the
        # return to Python gives the interpreter's loop a chance to
        # collect
        thresholds = gc.get_threshold()
        gc.collect()
        gc.set_threshold(100)
        try:
            count = await churn(20000)
        finally:
            gc.set_threshold(*thresholds)

        self.assertLess(count[0], 10000, count)


# The end.
