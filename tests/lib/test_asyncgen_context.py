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
tests.lib.test_asyncgen_context

A Koskari generator consumed from Koskari, from Python, and by both in
turn: for-each and yield-from splice it, __anext__, asend, athrow and
aclose drive it, and the two may alternate on one generator. A
generator runs under the dynamic state of whoever pulls from it: the
consumer's parameters when spliced, the ambient state at the drive
when driven from Python. Dropping an unstarted generator runs nothing;
dropping one suspended after a splice runs its finally.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define-parameter p 0)

(define [produce n] (for-each [i (range n)] (yield i)))

(define [fold g]
  (define total 0)
  (for-each [x g] (incr! total x))
  total)

(define [relay g] (yield-from g))

(define [first_of g] (for-each [x g] (return x)))

(define [guarded box]
  (try
    (begin (yield 1) (yield 2))
    (finally: (box.append "f"))))

(define [read_p] (yield p) (yield p))

(define [make_under v] (parameterize [[p v]] (read_p)))

(define [fold_under g v] (parameterize [[p v]] (fold g)))
"""


async def collect(agen):
    return [x async for x in agen]


class TestAsyncGenContext(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("asyncgen_context")
        vars(module)["__file__"] = "<asyncgen_context>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_koskari_for_each_folds_the_generator(self):
        mod = await self.load()
        self.assertEqual(await mod["fold"](mod["produce"](5)), 10)

    async def test_yield_from_relays_the_generator(self):
        mod = await self.load()
        self.assertEqual(
            await collect(mod["relay"](mod["produce"](3))), [0, 1, 2])

    async def test_python_drives_with_anext_asend_and_async_for(self):
        mod = await self.load()
        g = mod["produce"](4)
        self.assertEqual(await g.__anext__(), 0)
        self.assertEqual(await g.asend(None), 1)
        self.assertEqual(await collect(g), [2, 3])

    async def test_python_first_then_koskari(self):
        mod = await self.load()
        g = mod["produce"](5)
        self.assertEqual(await g.__anext__(), 0)
        self.assertEqual(await mod["fold"](g), 10)

    async def test_koskari_first_then_python(self):
        mod = await self.load()
        g = mod["produce"](5)
        self.assertEqual(await mod["first_of"](g), 0)
        self.assertEqual(await collect(g), [1, 2, 3, 4])

    async def test_alternating_drivers(self):
        mod = await self.load()
        g = mod["produce"](6)
        self.assertEqual(await g.__anext__(), 0)
        self.assertEqual(await mod["first_of"](g), 1)
        self.assertEqual(await g.__anext__(), 2)
        self.assertEqual(await mod["first_of"](g), 3)
        self.assertEqual(await collect(g), [4, 5])

    async def test_aclose_runs_finally(self):
        mod = await self.load()
        box = []
        g = mod["guarded"](box)
        self.assertEqual(await g.__anext__(), 1)
        self.assertIsNone(await g.aclose())
        self.assertEqual(box, ["f"])

    async def test_athrow_runs_finally_and_raises(self):
        mod = await self.load()
        box = []
        g = mod["guarded"](box)
        self.assertEqual(await g.__anext__(), 1)
        with self.assertRaises(ValueError):
            await g.athrow(ValueError("in"))
        self.assertEqual(box, ["f"])

    async def test_unstarted_generator_dropped_runs_nothing(self):
        mod = await self.load()
        box = []
        g = mod["guarded"](box)
        del g
        gc.collect()
        self.assertEqual(box, [])

    async def test_generator_suspended_by_a_splice_dropped_runs_finally(self):
        mod = await self.load()
        box = []
        g = mod["guarded"](box)
        self.assertEqual(await mod["first_of"](g), 1)
        del g
        gc.collect()
        # at rest, the dropped generator closes in the aclose the running
        # loop's finalizer hook schedules
        for _ in range(5):
            await asyncio.sleep(0)
        self.assertEqual(box, ["f"])

    async def test_splice_reads_the_consumer_parameters(self):
        mod = await self.load()
        g = await mod["make_under"](7)
        self.assertEqual(await mod["fold_under"](g, 3), 6)

    async def test_python_drive_reads_the_parameters_at_the_drive(self):
        mod = await self.load()
        g = await mod["make_under"](7)
        self.assertEqual(await collect(g), [0, 0])


# The end.
