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
tests.reproducers.test_foreach_pyaiter_await

Defect reproducer, RED until fixed: koskari for-each over a python
async iterator must treat StopAsyncIteration surfacing from a step
suspended on its await as end-of-iteration. ITER_NEXT's in-flight
receiver machinery (the FOREACH step phase and iter-boundary
capability) is enabled only for flattened koskari generators; a python
async iterator that suspends and then exhausts propagates
StopAsyncIteration out of the loop instead of branching to the loop
exit. Steps that exhaust synchronously (eagerly, without suspending)
classify correctly, which is how the gap stayed hidden.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module
from tests import SlowAIter


pytestmark = pytest.mark.phase_reproducers


SRC = """
def collect [ait]:
    var out (#list)
    for-each [x ait]:
        (out.append x)
    out
"""


async def eager_source():
    yield 1
    yield 2


class TestForeachPyAiterAwait(unittest.IsolatedAsyncioTestCase):
    """
    for-each over a python async iterator: awaiting steps deliver their
    values and mid-await exhaustion ends the loop
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_foreach_eager_pyaiter(self):
        """
        control: a python async generator that never suspends between
        yields already collects correctly
        """

        mod = await self.load("fpa_eager")
        collect = vars(mod)["collect"]
        self.assertEqual(await collect(eager_source()), [1, 2])

    async def test_foreach_awaiting_pyaiter(self):
        """
        every step suspends; both values arrive and the mid-await
        exhaustion ends the loop
        """

        mod = await self.load("fpa_awaiting")
        collect = vars(mod)["collect"]
        self.assertEqual(await collect(SlowAIter([1, 2])), [1, 2])

    async def test_foreach_awaiting_empty_pyaiter(self):
        """
        the very first step suspends and exhausts: the loop body never
        runs and the empty list returns
        """

        mod = await self.load("fpa_empty")
        collect = vars(mod)["collect"]
        self.assertEqual(await collect(SlowAIter([])), [])


# The end.
