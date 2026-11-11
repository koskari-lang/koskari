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
tests.reproducers.test_yield_in_applied_continuation

A delimited continuation captured inside a generator can outlive it.
Applied where no generator is running, a yield in the resumed region
has nothing to suspend: it is an ordinary error, raised at the yield,
which a try around the application catches and a finally sees.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def gen [out]:
    (out.append (reset (+ (shift k k) (yield 5))))

def escaped []:
    var out #[]
    (list (gen out))
    (out.pop)

def caught []:
    var k (escaped)
    try:
        (k 1)
    except [e RuntimeError]:
        (str e)

def cleaned [log]:
    var k (escaped)
    try:
        (k 1)
    finally:
        (log.append "finally")
"""


class TestYieldInAppliedContinuation(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("yield_in_applied_continuation")
        vars(module)["__file__"] = "<yield_in_applied_continuation>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_yield_with_no_generator_is_caught(self):
        mod = await self.load()
        self.assertEqual("yield outside generator", await mod["caught"]())


    async def test_yield_with_no_generator_runs_finally(self):
        mod = await self.load()
        log = []
        with self.assertRaises(RuntimeError) as caught:
            await mod["cleaned"](log)
        self.assertEqual("yield outside generator", str(caught.exception))
        self.assertEqual(["finally"], log)


# The end.
