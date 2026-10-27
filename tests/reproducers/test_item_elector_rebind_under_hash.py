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
tests.reproducers.test_item_elector_rebind_under_hash

The dict bodies of the fast item electors read the container and the
key from fast slots and then hash the key, which runs user code. A
slot holding a plain ref that a closure writes can be rebound from
inside that code, and when the slot held the dict's last reference the
dict is freed under the call. Each test loops a fused item row twice
over a captured dict: the first execution elects the dict body, the
key's hash then arms, and the second execution rebinds the dict from
inside the hash. The row must complete on the dict it fetched and the
function return with the slot rebound; a body holding the dict
borrowed writes into freed memory, which the ASan build reports

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
def store-twice [key hooks]:
    let [[d (dict)] [k key] [n 0]]:
        def rebind []:
            set! d None
        (hooks.append (demand rebind))
        while (< n 2):
            (set-item d k n)
            (hooks.append True)
            setq n (+ n 1)
        d

def load-twice [key hooks]:
    let [[d (dict)] [k key] [n 0] [seen #[]]]:
        def rebind []:
            set! d None
        (hooks.append (demand rebind))
        (d.__setitem__ key 7)
        while (< n 2):
            (seen.append d[k])
            (hooks.append True)
            setq n (+ n 1)
        #(d seen)

def inplace-twice [key hooks]:
    let [[d (dict)] [k key] [n 0]]:
        def rebind []:
            set! d None
        (hooks.append (demand rebind))
        (d.__setitem__ key 1)
        while (< n 2):
            (inplace-item + d k 10)
            (hooks.append True)
            setq n (+ n 1)
        d
"""


class RebindingKey:
    """
    A key whose hash rebinds the captured slot once armed: hooks holds
    the rebind closure first and a True for every armed execution.
    After the rebind the hash allocates dicts and keeps them, so the
    freed dict's storage is reused by one of them and a body still
    holding the freed dict writes into, or reads from, a dict it never
    fetched
    """

    def __init__(self, hooks):
        self.hooks = hooks
        self.reused = []

    def __hash__(self):
        if len(self.hooks) > 1:
            self.hooks[0]()
            self.reused.extend({} for _ in range(16))
        return 1

    def __eq__(self, other):
        return self is other


class TestItemElectorRebindUnderHash(unittest.IsolatedAsyncioTestCase):
    """
    Fast item elector dict bodies with the container rebound under the
    key's hash
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_store_body(self):
        mod = await self.load("ierh_store")
        hooks = []
        key = RebindingKey(hooks)
        d = await vars(mod)["store-twice"](key, hooks)
        self.assertIsNone(d)
        self.assertTrue(all(len(r) == 0 for r in key.reused),
                        "the store landed in a dict the row never fetched")

    async def test_load_body(self):
        mod = await self.load("ierh_load")
        hooks = []
        key = RebindingKey(hooks)
        d, seen = await vars(mod)["load-twice"](key, hooks)
        self.assertIsNone(d)
        self.assertEqual(seen, [7, 7])

    async def test_inplace_body(self):
        mod = await self.load("ierh_inplace")
        hooks = []
        key = RebindingKey(hooks)
        d = await vars(mod)["inplace-twice"](key, hooks)
        self.assertIsNone(d)
        self.assertTrue(all(len(r) == 0 for r in key.reused),
                        "the store landed in a dict the row never fetched")


# The end.
