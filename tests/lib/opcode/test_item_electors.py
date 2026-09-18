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
Tests for the item electors: ITEM, ITEM_CONST, ITEM_STORE,
ITEM_CONST_STORE and their FAST twins elect a body for the container
kind the site first sees (exact list or tuple under an exact int key,
exact dict under any key), decline to the generic body for an index
outside the sequence or a key the dict lacks, count type-guard misses
toward a demotion, re-elect after the generic body's own count wraps,
and return the same results as the object protocol throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code


pytestmark = pytest.mark.phase_lib


def make_site(instr, syms, cache=True):
    """
    A function over the fast locals syms running instr through one site
    """

    code = build_code(instr, local_syms=syms, name="site", cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(syms)), code


LOAD_SITES = {
    'ITEM': (
        [["FAST", 0], ["FAST", 1], ["ITEM", 0], ["RETURN", 1]],
        ("obj", "key")),
    'FAST_ITEM_FAST': (
        [["FAST_ITEM_FAST", (0, 1, 0, 0)], ["RETURN", 1]],
        ("obj", "key")),
}

STORE_SITES = {
    'ITEM_STORE': (
        [["FAST", 0], ["FAST", 1], ["FAST", 2], ["ITEM_STORE", 0],
         ["FAST", 0], ["RETURN", 1]],
        ("obj", "key", "val")),
    'FAST_ITEM_FAST_STORE': (
        [["FAST", 2], ["FAST_ITEM_FAST_STORE", (0, 1, 0, 0)],
         ["FAST", 0], ["RETURN", 1]],
        ("obj", "key", "val")),
}


def const_load_site(key, opname, cache=True):
    if opname == 'ITEM_CONST':
        instr = [["FAST", 0], ["ITEM_CONST", key], ["RETURN", 1]]
    else:
        instr = [["FAST_ITEM_CONST", (0, key, 0, 0)], ["RETURN", 1]]
    return make_site(instr, ("obj",), cache=cache)


def const_store_site(key, opname, cache=True):
    if opname == 'ITEM_CONST_STORE':
        instr = [["FAST", 0], ["FAST", 1], ["ITEM_CONST_STORE", key],
                 ["FAST", 0], ["RETURN", 1]]
    else:
        instr = [["FAST", 1], ["FAST_ITEM_CONST_STORE", (0, key, 0, 0)],
                 ["FAST", 0], ["RETURN", 1]]
    return make_site(instr, ("obj", "val"), cache=cache)


class ListSub(list):
    def __getitem__(self, key):
        return "sub"

    def __setitem__(self, key, val):
        list.__setitem__(self, key, "sub")


class DictSub(dict):
    def __getitem__(self, key):
        return "sub"

    def __missing__(self, key):
        return "missing"


class TestItemCacheSlots(CevalTest):

    def test_hot_rows_request_one_slot(self):
        for instr, syms in list(LOAD_SITES.values()) + list(STORE_SITES.values()):
            _, code = make_site(instr, syms)
            self.assertEqual(code.cache_slots, 1, instr)
            _, code = make_site(instr, syms, cache=False)
            self.assertEqual(code.cache_slots, 0, instr)
        for opname in ('ITEM_CONST', 'FAST_ITEM_CONST'):
            self.assertEqual(const_load_site(0, opname)[1].cache_slots, 1)
        for opname in ('ITEM_CONST_STORE', 'FAST_ITEM_CONST_STORE'):
            self.assertEqual(const_store_site(0, opname)[1].cache_slots, 1)


    def test_var_rows_request_no_slot(self):
        for op in ('VAR_ITEM_VAR', 'VAR_ITEM_VAR_STORE'):
            code = build_code([[op, ("obj", "key", 0, 0)], ["RETURN", 1]])
            self.assertEqual(code.cache_slots, 0, op)
        for op in ('VAR_ITEM_CONST', 'VAR_ITEM_CONST_STORE'):
            code = build_code([[op, ("obj", 0, 0, 0)], ["RETURN", 1]])
            self.assertEqual(code.cache_slots, 0, op)


class TestItemLoadElectors(CevalTest):

    async def check_load_site(self, site):
        lst = list(range(10))
        tup = tuple(range(10))
        d = {i: -i for i in range(10)}
        d["s"] = "str"

        # list body elected on the first execution, then served
        for i in range(-10, 10):
            self.assertEqual(await site(lst, i), lst[i])
        # an index outside the list declines to the generic body and
        # raises the protocol's own error; the site keeps its body
        with self.assertRaises(IndexError):
            await site(lst, 10)
        with self.assertRaises(IndexError):
            await site(lst, -11)
        self.assertEqual(await site(lst, 3), 3)
        # a bool key is not an exact int: a guard miss, served generically
        self.assertEqual(await site(lst, True), 1)
        # an index wider than the fast read declines, still correct
        with self.assertRaises(IndexError):
            await site(lst, 2 ** 70)
        # other kinds miss the guard and are served generically
        self.assertEqual(await site(tup, 4), 4)
        self.assertEqual(await site(d, "s"), "str")
        self.assertEqual(await site("abc", 1), "b")
        self.assertEqual(await site(ListSub([1]), 0), "sub")
        self.assertEqual(await site(DictSub(), "x"), "sub")
        with self.assertRaises(TypeError):
            await site(42, 0)
        with self.assertRaises(KeyError):
            await site(d, "missing")
        with self.assertRaises(TypeError):
            await site(d, [])


    async def test_list_site_serves_every_shape(self):
        for opname, (instr, syms) in LOAD_SITES.items():
            site, _ = make_site(instr, syms)
            await self.check_load_site(site)


    async def test_ungated_site_serves_every_shape(self):
        for opname, (instr, syms) in LOAD_SITES.items():
            site, _ = make_site(instr, syms, cache=False)
            await self.check_load_site(site)


    async def test_tuple_and_dict_sites(self):
        for opname, (instr, syms) in LOAD_SITES.items():
            site, _ = make_site(instr, syms)
            tup = (1, 2, 3)
            for _ in range(3):
                self.assertEqual(await site(tup, -1), 3)
            with self.assertRaises(IndexError):
                await site(tup, 3)
            self.assertEqual(await site([9], 0), 9)

            site, _ = make_site(instr, syms)
            d = {"a": 1, 2: "b", (1, 2): "t"}
            for _ in range(3):
                self.assertEqual(await site(d, "a"), 1)
                self.assertEqual(await site(d, 2), "b")
                self.assertEqual(await site(d, (1, 2)), "t")
            with self.assertRaises(KeyError):
                await site(d, "z")
            with self.assertRaises(TypeError):
                await site(d, {})
            self.assertEqual(await site(DictSub(), "q"), "sub")
            self.assertEqual(await site([9], 0), 9)


    async def test_demoted_site_re_elects_and_serves_every_kind(self):
        for opname, (instr, syms) in LOAD_SITES.items():
            site, _ = make_site(instr, syms)
            lst, tup, d = [1, 2], (3, 4), {"k": 5}
            for _ in range(10):
                self.assertEqual(await site(lst, 0), 1)
                self.assertEqual(await site(tup, 1), 4)
                self.assertEqual(await site(d, "k"), 5)
            for i in range(600):
                self.assertEqual(await site(d, "k"), 5)
            self.assertEqual(await site(lst, 1), 2)
            self.assertEqual(await site(tup, 0), 3)


    async def test_unbound_fast_slot_at_an_elected_site_raises(self):
        site, _ = make_site([
            ["FAST_ITEM_FAST", (0, 1, 0, 0)],
            ["FAST_CLEAR", 1],
            ["FAST_ITEM_FAST", (0, 1, 0, 0)],
            ["RETURN", 1],
        ], ("obj", "key"))
        with self.assertRaises(NameError):
            await site([1], 0)


class TestConstItemLoadElectors(CevalTest):

    async def test_int_key_sites(self):
        for opname in ('ITEM_CONST', 'FAST_ITEM_CONST'):
            site, _ = const_load_site(1, opname)
            for _ in range(3):
                self.assertEqual(await site([7, 8, 9]), 8)
            self.assertEqual(await site((7, 8, 9)), 8)
            self.assertEqual(await site({1: "d"}), "d")
            self.assertEqual(await site("abc"), "b")
            with self.assertRaises(IndexError):
                await site([7])
            self.assertEqual(await site(ListSub([0, 0])), "sub")

            site, _ = const_load_site(-1, opname)
            self.assertEqual(await site([7, 8, 9]), 9)
            with self.assertRaises(IndexError):
                await site([])

            site, _ = const_load_site(2 ** 70, opname)
            with self.assertRaises(IndexError):
                await site([1])
            with self.assertRaises(IndexError):
                await site([1])


    async def test_str_key_sites(self):
        for opname in ('ITEM_CONST', 'FAST_ITEM_CONST'):
            site, _ = const_load_site("k", opname)
            for _ in range(3):
                self.assertEqual(await site({"k": 1}), 1)
            with self.assertRaises(KeyError):
                await site({})
            self.assertEqual(await site(DictSub()), "sub")
            with self.assertRaises(TypeError):
                await site([1])


class TestItemStoreElectors(CevalTest):

    async def check_store_site(self, site):
        lst = [0] * 5
        for i in range(-5, 5):
            self.assertIs(await site(lst, i, i), lst)
            self.assertEqual(lst[i], i)
        with self.assertRaises(IndexError):
            await site(lst, 5, 0)
        with self.assertRaises(IndexError):
            await site(lst, -6, 0)
        with self.assertRaises(IndexError):
            await site(lst, 2 ** 70, 0)
        self.assertEqual((await site(lst, 0, "z"))[0], "z")
        # the stored value is the one reference the list now holds
        obj = object()
        await site(lst, 1, obj)
        self.assertIs(lst[1], obj)
        # other kinds miss the guard and are served generically
        d = {}
        self.assertEqual(await site(d, "k", 1), {"k": 1})
        with self.assertRaises(TypeError):
            await site((1, 2), 0, 0)
        with self.assertRaises(TypeError):
            await site(d, [], 0)
        sub = ListSub([0])
        await site(sub, 0, 1)
        self.assertEqual(sub[0], "sub")
        self.assertEqual(list.__getitem__(sub, 0), "sub")
        self.assertEqual((await site(lst, 2, 42))[2], 42)


    async def test_list_site_serves_every_shape(self):
        for opname, (instr, syms) in STORE_SITES.items():
            site, _ = make_site(instr, syms)
            await self.check_store_site(site)


    async def test_ungated_site_serves_every_shape(self):
        for opname, (instr, syms) in STORE_SITES.items():
            site, _ = make_site(instr, syms, cache=False)
            await self.check_store_site(site)


    async def test_dict_site(self):
        for opname, (instr, syms) in STORE_SITES.items():
            site, _ = make_site(instr, syms)
            d = {}
            for i in range(5):
                await site(d, i, -i)
                await site(d, str(i), i)
            self.assertEqual(len(d), 10)
            self.assertEqual(d[3], -3)
            self.assertEqual(d["3"], 3)
            with self.assertRaises(TypeError):
                await site(d, {}, 0)
            lst = [0]
            self.assertEqual(await site(lst, 0, 1), [1])


    async def test_demoted_site_re_elects_and_serves_every_kind(self):
        for opname, (instr, syms) in STORE_SITES.items():
            site, _ = make_site(instr, syms)
            lst, d = [0], {}
            for _ in range(10):
                self.assertEqual(await site(lst, 0, 1), [1])
                self.assertEqual(await site(d, "k", 2), {"k": 2})
            for i in range(600):
                await site(d, "k", i)
            self.assertEqual(d["k"], 599)
            self.assertEqual(await site(lst, 0, 3), [3])


class TestConstItemStoreElectors(CevalTest):

    async def test_int_key_sites(self):
        for opname in ('ITEM_CONST_STORE', 'FAST_ITEM_CONST_STORE'):
            site, _ = const_store_site(1, opname)
            lst = [0, 0]
            for i in range(3):
                self.assertEqual(await site(lst, i), [0, i])
            self.assertEqual(await site({}, 1), {1: 1})
            with self.assertRaises(IndexError):
                await site([0], 1)
            with self.assertRaises(TypeError):
                await site((0, 0), 1)
            self.assertEqual(await site(lst, 9), [0, 9])


    async def test_str_key_sites(self):
        for opname in ('ITEM_CONST_STORE', 'FAST_ITEM_CONST_STORE'):
            site, _ = const_store_site("k", opname)
            d = {}
            for i in range(3):
                self.assertEqual(await site(d, i), {"k": i})
            with self.assertRaises(TypeError):
                await site([0], 1)
            self.assertEqual(await site({}, 1), {"k": 1})


def const_value_store_site(key, value, opname, cache=True):
    if opname == 'FAST_ITEM_FAST_STORE_CONST':
        instr = [["FAST_ITEM_FAST_STORE_CONST", (0, 1, value, 0)],
                 ["FAST", 0], ["RETURN", 1]]
        syms = ("obj", "key")
    else:
        instr = [["FAST_ITEM_CONST_STORE_CONST", (0, key, value, 0)],
                 ["FAST", 0], ["RETURN", 1]]
        syms = ("obj",)
    return make_site(instr, syms, cache=cache)


class TestItemStoreConstElectors(CevalTest):
    """
    The stack-free stores take a new reference to the pool constant
    for the cell and drop nothing
    """

    def test_rows_request_one_slot(self):
        for opname in ('FAST_ITEM_FAST_STORE_CONST', 'FAST_ITEM_CONST_STORE_CONST'):
            self.assertEqual(const_value_store_site(0, 1, opname)[1].cache_slots, 1)
            self.assertEqual(
                const_value_store_site(0, 1, opname, cache=False)[1].cache_slots, 0)
        for op in ('VAR_ITEM_VAR_STORE_CONST', 'VAR_ITEM_CONST_STORE_CONST'):
            code = build_code([[op, ("obj", "key", 1, 0)], ["RETURN", 1]])
            self.assertEqual(code.cache_slots, 0, op)


    async def test_fast_item_store_const_site(self):
        for cache in (True, False):
            site, _ = const_value_store_site(None, "v", 'FAST_ITEM_FAST_STORE_CONST',
                                             cache=cache)
            lst = [0] * 3
            for i in range(-3, 3):
                self.assertIs(await site(lst, i), lst)
                self.assertEqual(lst[i], "v")
            with self.assertRaises(IndexError):
                await site(lst, 3)
            with self.assertRaises(IndexError):
                await site(lst, 2 ** 70)
            d = {}
            self.assertEqual(await site(d, "k"), {"k": "v"})
            self.assertEqual(await site(d, 1), {"k": "v", 1: "v"})
            with self.assertRaises(TypeError):
                await site((0,), 0)
            with self.assertRaises(TypeError):
                await site(d, [])
            sub = ListSub([0])
            await site(sub, 0)
            self.assertEqual(list.__getitem__(sub, 0), "sub")
            self.assertEqual(await site([0], 0), ["v"])


    async def test_fast_const_item_store_const_site(self):
        for cache in (True, False):
            site, _ = const_value_store_site(1, "v", 'FAST_ITEM_CONST_STORE_CONST',
                                             cache=cache)
            lst = [0, 0]
            for _ in range(3):
                self.assertEqual(await site(lst), [0, "v"])
            self.assertEqual(await site({}), {1: "v"})
            with self.assertRaises(IndexError):
                await site([0])
            with self.assertRaises(TypeError):
                await site((0, 0))
            self.assertEqual(await site([1, 2]), [1, "v"])


    async def test_constant_keeps_its_references(self):
        # the cell takes a new reference each time; the pool constant
        # survives every overwrite and the site's demotion and re-election
        site, _ = const_value_store_site(0, "v", 'FAST_ITEM_CONST_STORE_CONST')
        lst = [None]
        for _ in range(10):
            await site(lst)
            await site({})
        for _ in range(600):
            await site({})
        self.assertEqual(await site([None]), ["v"])
        self.assertEqual(lst, ["v"])


# The end.
