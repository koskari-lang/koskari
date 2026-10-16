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
tests.reproducers.test_with_as_cell

The [EXPR as: NAME] cell of with and dyn-with binds NAME to the value
the manager's __enter__ returns, whether EXPR is a call or a bare
symbol: the cell's earlier shapes, (PATTERN EXPR) and (NAME EXPR),
take its first two elements and yield to the as: shape when the cell
does not end there.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


HEAD = """
import-from contextlib nullcontext
"""

SOURCES = {
    "with_call_as": """
define with_call_as [v]:
    (with [(nullcontext v) as: got] got)
""",
    "with_symbol_as": """
define with_symbol_as [v]:
    define cm (nullcontext v)
    (with [cm as: got] got)
""",
    "dyn_with_call_as": """
define dyn_with_call_as [v]:
    (dyn-with [(nullcontext v) as: got] got)
""",
    "with_named": """
define with_named [v]:
    (with [got (nullcontext v)] got)
""",
}


class TestWithAsCell(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(f"with_as_cell_{name}")
        vars(module)["__file__"] = f"<with_as_cell_{name}>"
        await load_module_source_async(module, HEAD + SOURCES[name])
        return vars(module)[name]


    async def test_with_call_as_name(self):
        fn = await self.load("with_call_as")
        self.assertEqual(7, await fn(7))


    async def test_with_symbol_as_name(self):
        fn = await self.load("with_symbol_as")
        self.assertEqual(8, await fn(8))


    async def test_dyn_with_call_as_name(self):
        fn = await self.load("dyn_with_call_as")
        self.assertEqual(9, await fn(9))


    async def test_with_named_cell(self):
        fn = await self.load("with_named")
        self.assertEqual(10, await fn(10))


# The end.
