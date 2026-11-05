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
tests.reproducers.test_transform_source_freed_before_chain

A transform stepped from Python runs its koskari generator source on
the transform's own context, with the generator's node on that
context's chain. The node borrows the generator, and the chain can
outlive it: here the transform's clear frees the generator while the
context, held by the stepper, keeps the chain. The transform's
finalizer already ran while it was idle, so it does not close the
step, and the collector clears the generator and the transform before
the context. The chain's later free reads and writes nothing through
the freed generator; a free that did would write into freed memory,
seen under ASan or Valgrind.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest
import weakref

import pytest

from koskari.lib import AsyncGenerator, KosMap
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def produce [getfut log]:
    (yield 1)
    try:
        (await (getfut))
    finally:
        (log "finally")
"""


class Pending:
    """
    An awaitable that is its own iterator and never resolves. It has
    no finalizer, so a collection does not close it ahead of the
    clears, and an attribute set on it is held by whatever awaits it
    """

    def __await__(self):
        return self


    def __iter__(self):
        return self


    def __next__(self):
        return "pending"


    def send(self, value):
        return "pending"


    def throw(self, typ, val=None, tb=None):
        raise typ if val is None else val


class Resurrect:
    """
    Puts its target back into a list when finalized, so the target's
    own finalizer runs in a collection and the target survives it
    """

    def __init__(self, saved, target):
        self.saved = saved
        self.target = target
        self.me = self


    def __del__(self):
        self.saved.append(self.target)


class TestTransformSourceFreedBeforeChain(unittest.IsolatedAsyncioTestCase):
    """
    A transform's koskari generator source with its node on the
    transform's chain, released before that chain
    """

    async def asyncSetUp(self):
        module = new_module("transform_source_freed")
        vars(module)["__file__"] = "<transform_source_freed>"
        await load_module_source_async(module, SRC)
        self.ns = vars(module)
        self.log = []
        self.made = []


    def getfut(self):
        """
        A fresh awaitable for each await, kept so the test can reach it
        """

        fut = Pending()
        self.made.append(fut)
        return fut


    def step_to_await(self, transform):
        """
        Drive the transform through its first element and into the
        second step, which suspends on the generator's await
        """

        stepper = transform.__anext__()
        with self.assertRaises(StopIteration) as caught:
            stepper.send(None)
        self.assertEqual(caught.exception.value, 10)
        del stepper

        stepper = transform.__anext__()
        self.assertEqual(stepper.send(None), "pending")
        return stepper


    def churn(self):
        """
        Fresh generators after the release reuse the node storage; each
        yields its first value
        """

        gc.collect()
        for _ in range(3):
            gen = self.ns["produce"](self.getfut, self.log.append)
            stepper = gen.__anext__()
            with self.assertRaises(StopIteration) as caught:
                stepper.send(None)
            self.assertEqual(caught.exception.value, 1)
            del stepper, gen
        gc.collect()


    async def test_generator_cleared_before_the_chain(self):
        """
        the collector clears the generator and the transform ahead of
        the context the stepper holds, freeing the generator first; the
        context's chain is freed after it without touching it, and the
        collection completes
        """

        saved = []
        gen = self.ns["produce"](self.getfut, self.log.append)
        transform = KosMap(lambda x: x * 10, gen)

        # the transform's finalizer runs now, while it is idle, and
        # does not run again; the generator is held outside, so it is
        # not finalized here
        Resurrect(saved, transform)
        del transform
        gc.collect()
        transform = saved.pop()
        self.assertIsInstance(transform, KosMap)
        del gen

        stepper = self.step_to_await(transform)
        fut = self.made.pop()

        # the cycle: transform -> context -> chain -> awaited fut ->
        # stepper -> transform and context. The generator and the
        # transform survived a collection already, so the collector
        # clears them before the context, the stepper and fut.
        fut.stepper = stepper
        alive = weakref.ref(fut)
        del stepper, fut, transform

        gc.collect()
        self.assertIsNone(alive())
        self.assertFalse(any(isinstance(x, AsyncGenerator)
                             and x.__name__ == "produce"
                             for x in gc.get_objects()))
        self.churn()


# The end.
