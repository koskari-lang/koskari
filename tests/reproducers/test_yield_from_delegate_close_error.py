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
A yield-from delegate whose close raises is reported, not dropped

A generator delegating to a Python generator closes the delegate when
it is torn down. An error the delegate raises from that close has no
frame to propagate into, so it is handed to sys.unraisablehook the way
a finalizer error is, both when the delegating generator is abandoned
and when it is closed explicitly.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import gc
import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def outer [] (yield-from (inner)))
"""


def inner():
    try:
        yield 1
        yield 2
    finally:
        raise ValueError("delegate close failed")


class TestYieldFromDelegateCloseError(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("yield_from_delegate_close_error")
        vars(module)["__file__"] = "<yield_from_delegate_close_error>"
        vars(module)["inner"] = inner
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    def capture(self):
        """
        Install a recording unraisable hook after clearing garbage that
        has nothing to do with the test
        """

        gc.collect()
        seen = []
        prior = sys.unraisablehook
        sys.unraisablehook = seen.append
        self.addCleanup(setattr, sys, "unraisablehook", prior)
        return seen

    def assert_reported(self, seen):
        self.assertEqual(len(seen), 1)
        report = seen[0]
        self.assertIsInstance(report.exc_value, ValueError)
        self.assertIn("delegate close failed", str(report.exc_value))
        self.assertEqual(report.object.__name__, "inner")

    async def test_abandoned_delegation_reports_close_error(self):
        mod = await self.load()
        gen = mod["outer"]()
        self.assertEqual(await gen.__anext__(), 1)
        seen = self.capture()
        del gen
        gc.collect()
        # at rest, the dropped generator closes in the aclose the running
        # loop's finalizer hook schedules
        for _ in range(5):
            await asyncio.sleep(0)
        self.assert_reported(seen)

    async def test_explicit_close_reports_close_error(self):
        mod = await self.load()
        gen = mod["outer"]()
        self.assertEqual(await gen.__anext__(), 1)
        seen = self.capture()
        await gen.aclose()
        self.assert_reported(seen)


# The end.
