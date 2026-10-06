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
tests.lib.test_context_token

Continuation liveness across a coroutine's suspensions: a continuation
captured by a coroutine is dead while the coroutine sits suspended on a
Python awaitable, and applies from inside the coroutine once it has
been resumed, across the exit and re-entry of the evaluator on the
same context.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import types
import unittest

import pytest

import koskari.lib as lib
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
(define run (lambda [box aw]
  (call/cc (lambda [k]
    (set! (item box 0) k)
    (await aw)
    (k "resumed")
    "not reached"))))

(define plain (lambda [a b] (+ a b)))
"""


@types.coroutine
def one_step():
    yield "suspended"
    return "woken"


class Suspender:
    def __await__(self):
        return one_step()


class TestContextToken(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("context_token")
        vars(module)["__file__"] = "<context_token>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_continuation_dead_while_suspended_then_applies_on_resume(self):
        mod = await self.load()
        box = [None]
        coro = mod["run"](box, Suspender())

        self.assertEqual(coro.send(None), "suspended")
        self.assertIsNotNone(box[0])

        with self.assertRaises(lib.KoskariContinuationDead):
            box[0](1)

        with self.assertRaises(StopIteration) as stop:
            coro.send(None)
        self.assertEqual(stop.exception.value, "resumed")

        with self.assertRaises(lib.KoskariContinuationDead):
            box[0](2)

    async def test_plain_coroutine_completes(self):
        mod = await self.load()
        self.assertEqual(await mod["plain"](2, 3), 5)


# The end.
