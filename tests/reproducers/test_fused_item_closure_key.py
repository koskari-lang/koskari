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
tests.reproducers.test_fused_item_closure_key

The fused item store and in-place rows resolve their key by name when
the key is a symbol. A key captured from an enclosing function must be
requested from that scope like the object is, or the row resolves it
through the parent chain, which skips call frames. Covers the symbol
key on set-item, inplace-item, the bracket suffix store, and the load,
and the same shapes with a computed key that contains the captured
name, which take the stack path

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def store-sym [k]:
    lambda [d]:
        (set-item d k 1)
        d

def store-suffix-sym [k]:
    lambda [d]:
        (set! d[k] 2)
        d

def inplace-sym [k]:
    lambda [d]:
        (inplace-item + d k 10)
        d

def load-sym [k]:
    lambda [d]:
        d[k]

def store-expr [x]:
    lambda [d]:
        (set-item d (+ x 5) 1)
        d

def store-suffix-expr [x]:
    lambda [d]:
        (set! d[(+ x 5)] 2)
        d

def inplace-expr [x]:
    lambda [d]:
        (inplace-item + d (+ x 5) 10)
        d

def load-expr [x]:
    lambda [d]:
        d[(+ x 5)]
"""


class TestFusedItemClosureKey(unittest.IsolatedAsyncioTestCase):
    """
    Fused item rows with a key captured from the enclosing function
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_set_item_symbol_key(self):
        mod = await self.load("ficr_store_sym")
        v = vars(mod)
        d = await (await v["store-sym"]("k"))({})
        self.assertEqual(d, {"k": 1})

    async def test_suffix_store_symbol_key(self):
        mod = await self.load("ficr_suffix_sym")
        v = vars(mod)
        d = await (await v["store-suffix-sym"]("k"))({})
        self.assertEqual(d, {"k": 2})

    async def test_inplace_item_symbol_key(self):
        mod = await self.load("ficr_inplace_sym")
        v = vars(mod)
        d = await (await v["inplace-sym"]("k"))({"k": 5})
        self.assertEqual(d, {"k": 15})

    async def test_load_symbol_key(self):
        mod = await self.load("ficr_load_sym")
        v = vars(mod)
        self.assertEqual(await (await v["load-sym"]("k"))({"k": 7}), 7)

    async def test_set_item_computed_key(self):
        mod = await self.load("ficr_store_expr")
        v = vars(mod)
        d = await (await v["store-expr"](1))({})
        self.assertEqual(d, {6: 1})

    async def test_suffix_store_computed_key(self):
        mod = await self.load("ficr_suffix_expr")
        v = vars(mod)
        d = await (await v["store-suffix-expr"](1))({})
        self.assertEqual(d, {6: 2})

    async def test_inplace_item_computed_key(self):
        mod = await self.load("ficr_inplace_expr")
        v = vars(mod)
        d = await (await v["inplace-expr"](1))({6: 5})
        self.assertEqual(d, {6: 15})

    async def test_load_computed_key(self):
        mod = await self.load("ficr_load_expr")
        v = vars(mod)
        self.assertEqual(await (await v["load-expr"](1))({6: 7}), 7)


# The end.
