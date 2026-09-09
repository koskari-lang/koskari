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
tests.reproducers.test_generator_call_construction

Calling a generator function never begins evaluating its code: every
call entry -- a direct koskari call, a tail-position call, an
argumentful call through the shared call core, the python surface,
and the demand surface -- completes immediately with a new
AsyncGenerator instance, with the body untouched until the first step

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import demand
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define effects (list)

define gen-fn [x]:
    (effects.append "ran")
    yield x
    yield (+ x 1)

define call-direct []:
    (gen-fn 10)

define call-tail [x]:
    (gen-fn x)
"""


class TestGeneratorCallConstruction(unittest.IsolatedAsyncioTestCase):
    """
    A generator function call constructs; only stepping evaluates
    """

    async def asyncSetUp(self):
        module = new_module("genconstruct")
        vars(module)["__file__"] = "<genconstruct>"
        await load_module_source_async(module, SRC)
        self.v = vars(module)

    def assertUnevaluated(self, gen):
        self.assertEqual(type(gen).__name__, "AsyncGenerator")
        self.assertEqual(list(self.v["effects"]), [])

    async def drain(self, gen):
        return [x async for x in gen]

    async def test_koskari_direct_call(self):
        gen = await self.v["call-direct"]()
        self.assertUnevaluated(gen)
        self.assertEqual(await self.drain(gen), [10, 11])
        self.assertEqual(list(self.v["effects"]), ["ran"])

    async def test_koskari_tail_call(self):
        gen = await self.v["call-tail"](5)
        self.assertUnevaluated(gen)
        self.assertEqual(await self.drain(gen), [5, 6])

    async def test_python_surface_call(self):
        gen = self.v["gen-fn"](3)
        self.assertUnevaluated(gen)
        self.assertEqual(await self.drain(gen), [3, 4])

    async def test_demand_surface_call(self):
        dgen = demand(self.v["gen-fn"])(7)
        self.assertEqual(list(self.v["effects"]), [])
        self.assertEqual(list(dgen), [7, 8])
        self.assertEqual(list(self.v["effects"]), ["ran"])


# The end.
