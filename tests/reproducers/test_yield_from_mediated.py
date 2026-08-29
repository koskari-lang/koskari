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
tests.reproducers.test_yield_from_mediated

A koskari generator that yield-froms a KosMap whose own source is a
koskari generator: the map's step arms the YIELD_FROM boundary for
mediated delivery, and the deliveries must reach the provider through
the boundary. Also covers an error escaping the mapped function under
yield-from: the error propagates and the map is left reusable

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import KosMap
from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


SRC = """
define gen-plain []:
    yield 1
    yield 2

define inc [x]:
    (+ x 1)

define boom [x]:
    raise (ValueError "boom")

define relay [it]:
    yield-from it
"""


class TestYieldFromMediatedProvider(unittest.IsolatedAsyncioTestCase):
    """
    yield-from over a KosMap whose fetch is boundary-mediated
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_yield_from_over_mediated_map(self):
        mod = await self.load("yfm_basic")
        v = vars(mod)
        m = KosMap(v["inc"], v["gen-plain"]())
        result = [x async for x in v["relay"](m)]
        self.assertEqual(result, [2, 3])

    async def test_yield_from_map_error_leaves_map_reusable(self):
        mod = await self.load("yfm_boom")
        v = vars(mod)
        m = KosMap(v["boom"], [1, 2])
        with self.assertRaises(ValueError):
            [x async for x in v["relay"](m)]
        m2 = KosMap(v["inc"], v["gen-plain"]())
        result = [x async for x in v["relay"](m2)]
        self.assertEqual(result, [2, 3])

    async def test_yield_from_mediated_map_demand_drive(self):
        mod = await self.load("yfm_demand")
        v = vars(mod)
        m = KosMap(v["inc"], v["gen-plain"]())
        result = list(v["relay"](m))
        self.assertEqual(result, [2, 3])


# The end.
