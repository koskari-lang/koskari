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
tests.lib.test_demand_await_step

A demand wrapper over a Python callable steps the awaitable the
callable returns once: a completion is the call's value, a suspension
is refused with KoskariSyncViolation thrown into the awaitable, which
may catch it and return, and a coroutine already awaited elsewhere is
refused as CPython's await refuses it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import KoskariSyncViolation, demand


pytestmark = pytest.mark.phase_lib


class Suspender:
    def __await__(self):
        yield "suspended"
        return "woken"


class TestDemandAwaitStep(unittest.TestCase):

    def test_completion_is_the_value(self):
        async def settles():
            return 7

        self.assertEqual(demand(settles)(), 7)

    def test_suspension_is_refused(self):
        async def suspends():
            await Suspender()
            return 7

        with self.assertRaises(KoskariSyncViolation):
            demand(suspends)()

    def test_callee_may_catch_the_refusal(self):
        async def falls_back():
            try:
                await Suspender()
            except KoskariSyncViolation:
                return "fallback"
            return "woken"

        self.assertEqual(demand(falls_back)(), "fallback")

    def test_coroutine_awaited_elsewhere_is_refused(self):
        async def inner():
            return await Suspender()

        async def outer(c):
            return await c

        c = inner()
        first = outer(c)
        self.assertEqual(first.send(None), "suspended")

        with self.assertRaises(RuntimeError) as caught:
            demand(lambda: c)()
        self.assertEqual(
            str(caught.exception), "coroutine is being awaited already")

        with self.assertRaises(StopIteration) as done:
            first.send(None)
        self.assertEqual(done.exception.value, "woken")


# The end.
