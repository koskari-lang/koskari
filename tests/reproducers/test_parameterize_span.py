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
tests.reproducers.test_parameterize_span

A Python parameterize block and a koskari parameterize extent on one
stack: each language's reads see the innermost of the two

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import unittest

import pytest

from koskari.lib import ParamRef, demand_call, parameter
from koskari.module import load_module_source_async, new_module
from koskari.types import parameterize


pytestmark = pytest.mark.phase_reproducers


SRC = """
(define call_within [probe]
  (parameterize [[span_width 5]]
    (probe)))

(define read_width []
  (parameterq span_width None))

(define read_width_within [probe]
  (parameterize [[span_width 5]]
    (probe)
    (parameterq span_width None)))

(define call_async [f]
  (f))
"""


class TestParameterizeSpan(unittest.IsolatedAsyncioTestCase):


    async def load(self):
        module = new_module("tests.reproducers.parameterize_span")
        vars(module)["__file__"] = "<parameterize_span>"
        await load_module_source_async(module, SRC, layout=False)
        return module


    async def test_koskari_call_inside_block_sees_the_binding(self):
        mod = await self.load()
        with parameterize(span_width=7):
            self.assertEqual(demand_call(mod.read_width), 7)
        self.assertIsNone(demand_call(mod.read_width))


    async def test_koskari_extent_inside_block_shadows_then_restores(self):
        mod = await self.load()
        with parameterize(span_width=1):
            probe = lambda: parameter("span_width")
            self.assertEqual(demand_call(mod.call_within, probe), 5)
            self.assertEqual(parameter("span_width"), 1)
            self.assertEqual(demand_call(mod.read_width), 1)


    async def test_callee_block_shadows_the_callers_extent(self):
        """
        A block opened by Python code that koskari called out to is
        inner to the calling extent: inside it both languages read the
        block's value; after it the extent's value answers again
        """

        mod = await self.load()
        seen = {}

        def probe():
            with parameterize(span_width=9):
                seen["python inside"] = parameter("span_width")
                seen["koskari inside"] = demand_call(mod.read_width)
            seen["python after"] = parameter("span_width")
            seen["koskari after"] = demand_call(mod.read_width)

        self.assertEqual(demand_call(mod.read_width_within, probe), 5)
        self.assertEqual(seen, {
            "python inside": 9,
            "koskari inside": 9,
            "python after": 5,
            "koskari after": 5,
        })
        self.assertIsNone(parameter("span_width", None))


    async def test_store_from_python_inside_extent_writes_the_extent(self):
        mod = await self.load()
        ref = ParamRef("span_width")

        def probe():
            ref.store(6)
            return parameter("span_width")

        self.assertEqual(demand_call(mod.call_within, probe), 6)
        self.assertTrue(ref.empty)


    async def test_block_opened_under_a_koskari_context_survives_an_await(self):
        """
        A Python coroutine awaited by koskari opens a parameterize block
        and awaits inside it: the binding holds across the await, for
        Python and for koskari calls made after it
        """

        mod = await self.load()

        async def body():
            with parameterize(span_width=7):
                before = parameter("span_width", None)
                await asyncio.sleep(0)
                after = parameter("span_width", None)
                inner = await vars(mod)["read_width"]()
            return before, after, inner

        self.assertEqual(await vars(mod)["call_async"](body), (7, 7, 7))
        self.assertIsNone(parameter("span_width", None))


# The end.
