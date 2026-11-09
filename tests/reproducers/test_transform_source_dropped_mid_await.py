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
tests.reproducers.test_transform_source_dropped_mid_await

A transform stepped from Python installs its koskari generator source
under the transform's own native frame. When the stepper and then the
transform are dropped mid-await, the transform's finalizer closes its
chain from the top, and every generator on it finishes with
GeneratorExit there; a finally block that awaits again cannot complete,
and the chain, the generator's node with it, is freed by the
transform's release. The generator object and the chain each let go of
the node in whichever order the free runs, and the second one frees
it: no fatal in a safety-net build, no double free, and later
generators reuse the node storage cleanly.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import sys
import unittest

import pytest

from koskari.lib import AsyncGenerator, KosFilter, KosMap, KosZip
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def plain [fut log]:
    (yield 1)
    try:
        (await fut)
    finally:
        (log "finally")

def produce [fut log]:
    (yield 1)
    try:
        (await fut)
    finally:
        (await fut)
        (log "finally")

def inner [fut log]:
    try:
        (yield 1)
        (await fut)
    finally:
        (log "inner-fin")

def outer [fut log]:
    try:
        (for-each [x (inner fut log)]
            (yield x))
    finally:
        (log "outer-fin")
"""


class Pending:
    """
    An awaitable that suspends once and never resolves
    """

    def __await__(self):
        yield "pending"


class Holder:
    pass


class TestTransformSourceDroppedMidAwait(unittest.IsolatedAsyncioTestCase):
    """
    A transform freed while its generator source is suspended inside a
    finally block that awaits
    """

    async def asyncSetUp(self):
        module = new_module("transform_source_dropped")
        vars(module)["__file__"] = "<transform_source_dropped>"
        await load_module_source_async(module, SRC)
        self.ns = vars(module)
        self.log = []
        self.seen = []
        self.addCleanup(setattr, sys, "unraisablehook", sys.unraisablehook)
        sys.unraisablehook = self.record

    def record(self, u):
        """
        Keep each report as the exception type and message and the type
        of the object reported against
        """

        self.seen.append(
            (type(u.exc_value), str(u.exc_value), type(u.object)))

    def assert_ignored_by(self, owner_type):
        """
        Exactly one report, against the object whose cleanup tried to
        suspend
        """

        expected = (
            RuntimeError,
            "async generator ignored GeneratorExit",
            owner_type)
        self.assertEqual(self.seen, [expected])

    def step_to_await(self, transform):
        """
        Drive the transform through its first element and into the
        second step, which suspends on the generator's await
        """

        stepper = transform.__anext__()
        with self.assertRaises(StopIteration) as caught:
            stepper.send(None)
        first = caught.exception.value
        stepper = transform.__anext__()
        self.assertEqual(stepper.send(None), "pending")
        return first, stepper

    def churn(self):
        """
        Fresh generators after the drop reuse the node storage; each
        yields its first value
        """

        gc.collect()
        for _ in range(3):
            gen = self.ns["produce"](Pending(), self.log.append)
            stepper = gen.__anext__()
            with self.assertRaises(StopIteration) as caught:
                stepper.send(None)
            self.assertEqual(caught.exception.value, 1)
            del stepper, gen
        gc.collect()

    async def test_map_dropped_mid_await(self):
        gen = self.ns["produce"](Pending(), self.log.append)
        transform = KosMap(lambda x: x * 10, gen)
        del gen
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, 10)
        del stepper
        del transform
        self.assertEqual(self.log, [])
        self.assert_ignored_by(AsyncGenerator)
        self.churn()

    async def test_zip_dropped_mid_await(self):
        gen = self.ns["produce"](Pending(), self.log.append)
        transform = KosZip(gen, [1, 2, 3])
        del gen
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, (1, 1))
        del stepper
        del transform
        self.assertEqual(self.log, [])
        self.assert_ignored_by(AsyncGenerator)
        self.churn()

    async def test_filter_dropped_mid_await(self):
        gen = self.ns["produce"](Pending(), self.log.append)
        transform = KosFilter(lambda x: True, gen)
        del gen
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, 1)
        del stepper
        del transform
        self.assertEqual(self.log, [])
        self.assert_ignored_by(AsyncGenerator)
        self.churn()

    async def test_map_dropped_in_a_cycle(self):
        """
        the transform, its stepper and the generator's log closure go
        together in a garbage collection, where the finalizer order is
        the collector's; the generator's close still runs, whichever
        finalizer comes first, and is reported against the generator
        """

        holder = Holder()
        gen = self.ns["produce"](
            Pending(), lambda v, holder=holder: self.log.append(v))
        transform = KosMap(lambda x: x * 10, gen)
        holder.transform = transform
        del gen
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, 10)
        holder.stepper = stepper
        del stepper, transform, holder
        gc.collect()
        self.assertEqual(self.log, [])
        self.assert_ignored_by(AsyncGenerator)
        self.churn()

    async def test_nested_generators_close_with_the_transform(self):
        """
        A transform over an outer generator iterating an inner one,
        dropped while the inner awaits: the transform's chain holds
        both, and its drop closes them in one unwind from the top, the
        inner first, while the outer is still held
        """

        outer = self.ns["outer"](Pending(), self.log.append)
        transform = KosMap(lambda x: x * 10, outer)
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, 10)
        del stepper
        del transform
        self.assertEqual(self.log, ["inner-fin", "outer-fin"])
        self.assertEqual(self.seen, [])
        with self.assertRaises(StopAsyncIteration):
            await outer.__anext__()
        del outer
        self.assertEqual(self.log, ["inner-fin", "outer-fin"])
        self.assertEqual(self.seen, [])
        self.churn()

    async def test_plain_finally_runs_on_drop(self):
        """
        a finally block that does not await completes when the
        transform's drop closes its chain, and the node comes off the
        chain with the generator
        """

        gen = self.ns["plain"](Pending(), self.log.append)
        transform = KosMap(lambda x: x * 10, gen)
        del gen
        first, stepper = self.step_to_await(transform)
        self.assertEqual(first, 10)
        del stepper
        del transform
        self.assertEqual(self.log, ["finally"])
        self.assertEqual(self.seen, [])
        self.churn()


# The end.
