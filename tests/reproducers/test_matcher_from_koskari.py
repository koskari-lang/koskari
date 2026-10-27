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
tests.reproducers.test_matcher_from_koskari

A matcher is the Function its pattern builds: Koskari calls it as any
function, getting the captures on a match and None on a miss, and an
awaitable a Python predicate returns is awaited in place in the
calling evaluator

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.matcher import CAPTURE, MATCH, PREDICATE, TYPE, make_matcher
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def probe [m subject]:
  (m subject)

var hit (probe matcher #(7))
var miss (probe matcher #("s"))
"""


class TestMatcherFromKoskari(unittest.IsolatedAsyncioTestCase):

    async def load(self, name, matcher):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        vars(module)["matcher"] = matcher
        await load_module_source_async(module, SRC)
        return module


    async def test_call_returns_captures_or_none(self):
        mod = await self.load(
            "mfk_call", make_matcher(MATCH(CAPTURE("x", TYPE("int")))))
        self.assertEqual(vars(mod)["hit"], {"x": 7})
        self.assertIsNone(vars(mod)["miss"])


    async def test_async_predicate_is_awaited_in_place(self):
        seen = []

        async def p(value):
            await asyncio.sleep(0)
            seen.append(value)
            return value == 7

        mod = await self.load(
            "mfk_async",
            make_matcher(MATCH(CAPTURE("x", PREDICATE("p"))), {"p": p}))
        self.assertEqual(vars(mod)["hit"], {"x": 7})
        self.assertIsNone(vars(mod)["miss"])
        self.assertEqual(seen, [7, "s"])


# The end.
