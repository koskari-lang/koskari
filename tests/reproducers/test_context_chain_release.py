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
Releasing a context's chain runs Python code from the nodes it frees,
and that code can trigger a collection that traverses the context. The
registers are cleared before the first free, so the traverse finds no
node already freed. Each test here dropped the process by SIGSEGV when
the free ran first.
"""

import asyncio
import gc
import sys
import unittest
from unittest import mock

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def helper [fut]
  (try (await fut) ([e ValueError] None)))

(def gen_close_awaits [fut box probe items]
  (try
    (yield 1)
    (finally:
      (box.append "fin")
      (for-each [x items]
        (for-each [y items] (box.append y)))
      (helper fut))))

(def gen_two_awaits [fut fut2 box probe items]
  (try
    (begin (yield 1) (yield (await fut)))
    (finally:
      (box.append "fin")
      (for-each [x items]
        (for-each [y items] (box.append y)))
      (helper fut2))))
"""


class Allocating:
    """
    A local of the generator whose release allocates enough to run an
    automatic collection while the chain is being freed
    """

    keep = []

    def __del__(self):
        Allocating.keep.extend([i] for i in range(300000))
        Allocating.keep.clear()


def step(awaitable):
    try:
        return ("yield", awaitable.send(None))
    except StopIteration as e:
        return ("stop", e.value)


class TestContextChainRelease(unittest.TestCase):

    def setUp(self):
        self.loop = asyncio.new_event_loop()
        module = new_module("context_chain_release")
        vars(module)["__file__"] = "<context_chain_release>"
        self.loop.run_until_complete(load_module_source_async(module, SRC))
        self.v = vars(module)
        done = self.loop.create_future()
        done.set_result(None)
        self.loop.run_until_complete(self.v["helper"](done))
        gc.set_threshold(700, 10, 10)

    def tearDown(self):
        self.loop.close()
        gc.set_threshold(700, 10, 10)

    def test_teardown_fallback_frees_with_registers_cleared(self):
        """
        A suspended generator whose close awaits is dropped; the close
        cannot be honored, is reported, and the chain is freed from the
        teardown
        """

        fut = self.loop.create_future()
        box = []
        seen = []

        def record(u):
            seen.append((type(u.exc_value), str(u.exc_value)))

        agen = self.v["gen_close_awaits"](
            fut, box, Allocating(), [object(), object()])
        self.assertEqual(("stop", 1), step(agen.__anext__()))
        with mock.patch.object(sys, "unraisablehook", record):
            del agen
            gc.collect()
        self.assertEqual("fin", box[0])
        self.assertEqual(
            [(RuntimeError, "async generator ignored GeneratorExit")],
            seen)

    def test_abandoned_stepper_recycles_with_registers_cleared(self):
        """
        A stepper dropped mid-await drives GeneratorExit; the cleanup
        awaits again, is reported, and the finish recycles the wrapper's
        context
        """

        fut = self.loop.create_future()
        fut2 = self.loop.create_future()
        box = []
        seen = []

        def record(u):
            seen.append((type(u.exc_value), str(u.exc_value)))

        agen = self.v["gen_two_awaits"](
            fut, fut2, box, Allocating(), [object(), object()])
        self.assertEqual(("stop", 1), step(agen.__anext__()))
        st = agen.__anext__()
        self.assertEqual("yield", step(st)[0])
        with mock.patch.object(sys, "unraisablehook", record):
            del st
            del agen
            gc.collect()
        self.assertEqual("fin", box[0])
        self.assertEqual(
            [(RuntimeError, "async generator ignored GeneratorExit")],
            seen)


# The end.
