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
Tests for every row of the item family, generated from the grid the
family is named by: the object from the stack, a name, or a fast slot;
the key from the stack, the pool, a name, or a fast slot; a store's
value from the same four; an in-place row's operand from the stack or
the pool. Each row runs over a list, a dict, a tuple where the verb
allows one, and an object served by the protocol, raises NameError for
an empty slot and the protocol's own error for a bad access, and
answers the same after election as before it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import (
    FormalsSpec, ItemRef, NamespaceSpec, OpCode, Operand, OpStyle)
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount


pytestmark = pytest.mark.phase_lib


SYMS = ("obj", "key", "val")
SLOT = {"obj": 0, "key": 1, "val": 2}
MARK = {"const": "CONST", "var": "VAR", "fast": "FAST"}


def row_name(obj, key, verb, val=None, operand=None):
    parts = []
    if obj != "stack":
        parts.append(MARK[obj])
    parts.append("ITEM")
    if key != "stack":
        parts.append(MARK[key])
    if verb != "ITEM":
        parts.append(verb)
    if verb == "STORE" and val != "stack":
        parts.append(MARK[val])
    if verb == "INPLACE" and operand == "const":
        parts.append("CONST")
    return "_".join(parts)


def rows():
    """
    Every row as (name, obj, key, verb, val, operand)
    """

    out = []
    for obj in ("stack", "var", "fast"):
        for key in ("stack", "const", "var", "fast"):
            if {obj, key} >= {"var", "fast"}:
                continue
            for verb in ("ITEM", "CLEAR", "TAKE", "REF"):
                out.append((row_name(obj, key, verb), obj, key, verb, None, None))
            for val in ("stack", "const", "var", "fast"):
                if {obj, key, val} >= {"var", "fast"}:
                    continue
                out.append((row_name(obj, key, "STORE", val=val),
                            obj, key, "STORE", val, None))
            for operand in ("stack", "const"):
                out.append((row_name(obj, key, "INPLACE", operand=operand),
                            obj, key, "INPLACE", None, operand))
    return out


ROWS = rows()


# the item family as the opcode table spells it, one name per row of
# the grid; a row added to or taken from the family changes this list
ITEM_OPCODES = (
    "FAST_ITEM",
    "FAST_ITEM_CLEAR",
    "FAST_ITEM_CONST",
    "FAST_ITEM_CONST_CLEAR",
    "FAST_ITEM_CONST_INPLACE",
    "FAST_ITEM_CONST_INPLACE_CONST",
    "FAST_ITEM_CONST_REF",
    "FAST_ITEM_CONST_STORE",
    "FAST_ITEM_CONST_STORE_CONST",
    "FAST_ITEM_CONST_STORE_FAST",
    "FAST_ITEM_CONST_TAKE",
    "FAST_ITEM_FAST",
    "FAST_ITEM_FAST_CLEAR",
    "FAST_ITEM_FAST_INPLACE",
    "FAST_ITEM_FAST_INPLACE_CONST",
    "FAST_ITEM_FAST_REF",
    "FAST_ITEM_FAST_STORE",
    "FAST_ITEM_FAST_STORE_CONST",
    "FAST_ITEM_FAST_STORE_FAST",
    "FAST_ITEM_FAST_TAKE",
    "FAST_ITEM_INPLACE",
    "FAST_ITEM_INPLACE_CONST",
    "FAST_ITEM_REF",
    "FAST_ITEM_STORE",
    "FAST_ITEM_STORE_CONST",
    "FAST_ITEM_STORE_FAST",
    "FAST_ITEM_TAKE",
    "ITEM",
    "ITEM_CLEAR",
    "ITEM_CONST",
    "ITEM_CONST_CLEAR",
    "ITEM_CONST_INPLACE",
    "ITEM_CONST_INPLACE_CONST",
    "ITEM_CONST_REF",
    "ITEM_CONST_STORE",
    "ITEM_CONST_STORE_CONST",
    "ITEM_CONST_STORE_FAST",
    "ITEM_CONST_STORE_VAR",
    "ITEM_CONST_TAKE",
    "ITEM_FAST",
    "ITEM_FAST_CLEAR",
    "ITEM_FAST_INPLACE",
    "ITEM_FAST_INPLACE_CONST",
    "ITEM_FAST_REF",
    "ITEM_FAST_STORE",
    "ITEM_FAST_STORE_CONST",
    "ITEM_FAST_STORE_FAST",
    "ITEM_FAST_TAKE",
    "ITEM_INPLACE",
    "ITEM_INPLACE_CONST",
    "ITEM_REF",
    "ITEM_STORE",
    "ITEM_STORE_CONST",
    "ITEM_STORE_FAST",
    "ITEM_STORE_VAR",
    "ITEM_TAKE",
    "ITEM_VAR",
    "ITEM_VAR_CLEAR",
    "ITEM_VAR_INPLACE",
    "ITEM_VAR_INPLACE_CONST",
    "ITEM_VAR_REF",
    "ITEM_VAR_STORE",
    "ITEM_VAR_STORE_CONST",
    "ITEM_VAR_STORE_VAR",
    "ITEM_VAR_TAKE",
    "VAR_ITEM",
    "VAR_ITEM_CLEAR",
    "VAR_ITEM_CONST",
    "VAR_ITEM_CONST_CLEAR",
    "VAR_ITEM_CONST_INPLACE",
    "VAR_ITEM_CONST_INPLACE_CONST",
    "VAR_ITEM_CONST_REF",
    "VAR_ITEM_CONST_STORE",
    "VAR_ITEM_CONST_STORE_CONST",
    "VAR_ITEM_CONST_STORE_VAR",
    "VAR_ITEM_CONST_TAKE",
    "VAR_ITEM_INPLACE",
    "VAR_ITEM_INPLACE_CONST",
    "VAR_ITEM_REF",
    "VAR_ITEM_STORE",
    "VAR_ITEM_STORE_CONST",
    "VAR_ITEM_STORE_VAR",
    "VAR_ITEM_TAKE",
    "VAR_ITEM_VAR",
    "VAR_ITEM_VAR_CLEAR",
    "VAR_ITEM_VAR_INPLACE",
    "VAR_ITEM_VAR_INPLACE_CONST",
    "VAR_ITEM_VAR_REF",
    "VAR_ITEM_VAR_STORE",
    "VAR_ITEM_VAR_STORE_CONST",
    "VAR_ITEM_VAR_STORE_VAR",
    "VAR_ITEM_VAR_TAKE",
)


def row_arg(op, obj, key, val, operand, key_const, val_const, count):
    """
    The arg of a row: its named fields in name order, an in-place
    row's operator field first
    """

    fields = []
    if operand == "stack":
        fields.append(opcount(Operand.ADD, count))
    elif operand == "const":
        fields.append(int(Operand.ADD))
    for role, origin in (("obj", obj), ("key", key), ("val", val)):
        if origin == "const":
            fields.append(key_const if role == "key" else val_const)
        elif origin == "var":
            fields.append(role)
        elif origin == "fast":
            fields.append(SLOT[role])
    if operand == "const":
        fields.append(val_const)
    if op.style == OpStyle.EXTRA:
        return tuple(fields) + (0,) * (4 - len(fields))
    return fields[0] if fields else 0


KEY_CONST = 1
VAL_CONST = 7
KEY_LOCAL = 2
VAL_LOCAL = 9


def site(name, obj, key, verb, val, operand, key_const=KEY_CONST,
         val_const=VAL_CONST, count=1, cache=True):
    """
    A function over the fast locals obj, key, val running one row.
    Loads, TAKE, and REF return the row's result; the others return
    the object
    """

    op = OpCode[name]
    instr = []
    if obj == "stack":
        instr.append(["FAST", 0])
    if key == "stack":
        instr.append(["FAST", 1])
    if verb == "STORE" and val == "stack":
        instr.append(["FAST", 2])
    if verb == "INPLACE" and operand == "stack":
        instr.extend([["FAST", 2]] * count)
    instr.append([name, row_arg(op, obj, key, val, operand, key_const,
                                val_const, count)])
    if verb in ("ITEM", "TAKE", "REF"):
        instr.append(["RETURN", 1])
    else:
        instr.append(["FAST", 0])
        instr.append(["RETURN", 1])
    code = build_code(instr, local_syms=SYMS, name=name, cache=cache)
    nspec = NamespaceSpec(None, SYMS, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(SYMS))

    def run(obj, key, val=None):
        return fn(obj, key, val)
    return run, code


class Served:
    """
    A container served by the protocol, remembering every access
    """

    def __init__(self):
        self.log = []

    def __getitem__(self, key):
        self.log.append(("get", key))
        return 20

    def __setitem__(self, key, val):
        self.log.append(("set", key, val))

    def __delitem__(self, key):
        self.log.append(("del", key))


class TestItemMatrix(CevalTest):

    def test_the_grid_is_the_opcode_table(self):
        names = {name for name, *_ in ROWS}
        self.assertEqual(names, set(ITEM_OPCODES))
        self.assertEqual(len(ITEM_OPCODES), 92)
        for name in ITEM_OPCODES:
            self.assertTrue(hasattr(OpCode, name), name)


    def test_slots_by_tier(self):
        for name, obj, key, verb, val, operand in ROWS:
            fn, code = site(name, obj, key, verb, val, operand)
            var_row = "VAR" in name.split("_")
            elects = verb in ("ITEM", "STORE", "INPLACE") and not var_row
            self.assertEqual(code.cache_slots, 1 if elects else 0, name)


    def key_for(self, key):
        # the key the site reads: the pool constant on a const row,
        # else the fast local, which differ so the origin is checked
        return KEY_CONST if key == "const" else KEY_LOCAL

    def val_for(self, val):
        return VAL_CONST if val == "const" else VAL_LOCAL

    async def check_load(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k = self.key_for(key)
        lst = [10, 20, 30]
        for _ in range(3):
            self.assertEqual(await fn(lst, KEY_LOCAL), lst[k], name)
            self.assertEqual(await fn(tuple(lst), KEY_LOCAL), lst[k], name)
            self.assertEqual(await fn({k: 20}, KEY_LOCAL), 20, name)
            served = Served()
            self.assertEqual(await fn(served, KEY_LOCAL), 20, name)
            self.assertEqual(served.log, [("get", k)], name)
        with self.assertRaises(IndexError, msg=name):
            await fn([10], KEY_LOCAL)
        with self.assertRaises(KeyError, msg=name):
            await fn({}, KEY_LOCAL)
        with self.assertRaises(TypeError, msg=name):
            await fn(42, KEY_LOCAL)
        if key != "const":
            with self.assertRaises(TypeError, msg=name):
                await fn({}, {}, None)
        fn, _ = site(name, obj, key, verb, val, operand, key_const="k")
        self.assertEqual(await fn({"k": 20}, "k"), 20, name)


    async def check_store(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k, v = self.key_for(key), self.val_for(val)
        for _ in range(3):
            expect = [10, 20, 30]
            expect[k] = v
            self.assertEqual(
                await fn([10, 20, 30], KEY_LOCAL, VAL_LOCAL), expect, name)
            self.assertEqual(await fn({}, KEY_LOCAL, VAL_LOCAL), {k: v}, name)
            served = Served()
            await fn(served, KEY_LOCAL, VAL_LOCAL)
            self.assertEqual(served.log, [("set", k, v)], name)
        with self.assertRaises(TypeError, msg=name):
            await fn((10, 20, 30), KEY_LOCAL, VAL_LOCAL)
        with self.assertRaises(IndexError, msg=name):
            await fn([10], KEY_LOCAL, VAL_LOCAL)
        with self.assertRaises(TypeError, msg=name):
            await fn(42, KEY_LOCAL, VAL_LOCAL)
        fn, _ = site(name, obj, key, verb, val, operand, key_const="k")
        self.assertEqual(
            await fn({}, "k", VAL_LOCAL), {"k": v}, name)
        marker = object()
        fn, _ = site(name, obj, key, verb, val, operand, key_const=0,
                     val_const=marker)
        holder = await fn([None], 0, marker)
        self.assertIs(holder[0], marker, name)


    async def check_inplace(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k = self.key_for(key)
        r = VAL_CONST if operand == "const" else VAL_LOCAL
        for _ in range(3):
            expect = [10, 20, 30]
            expect[k] += r
            self.assertEqual(
                await fn([10, 20, 30], KEY_LOCAL, VAL_LOCAL), expect, name)
            self.assertEqual(
                await fn({k: 20}, KEY_LOCAL, VAL_LOCAL), {k: 20 + r}, name)
            floats = [0.5, 1.5, 2.5]
            expect = list(floats)
            expect[k] += r
            self.assertEqual(
                await fn(floats, KEY_LOCAL, VAL_LOCAL), expect, name)
        served = Served()
        await fn(served, KEY_LOCAL, VAL_LOCAL)
        self.assertEqual(served.log, [("get", k), ("set", k, 20 + r)], name)
        with self.assertRaises(TypeError, msg=name):
            await fn((10, 20, 30), KEY_LOCAL, VAL_LOCAL)
        with self.assertRaises(IndexError, msg=name):
            await fn([10], KEY_LOCAL, VAL_LOCAL)
        with self.assertRaises(KeyError, msg=name):
            await fn({}, KEY_LOCAL, VAL_LOCAL)
        fn, _ = site(name, obj, key, verb, val, operand, key_const=0,
                     val_const="b")
        self.assertEqual(await fn(["a"], 0, "b"), ["ab"], name)
        self.assertEqual(await fn({0: "a"}, 0, "b"), {0: "ab"}, name)
        inner = [1]
        fn, _ = site(name, obj, key, verb, val, operand, key_const=0,
                     val_const=[2])
        self.assertIs((await fn([inner], 0, [2]))[0], inner, name)
        self.assertEqual(inner, [1, 2], name)
        if operand == "stack":
            fn, _ = site(name, obj, key, verb, val, operand, count=2)
            expect = [10, 20, 30]
            expect[k] += 2 * VAL_LOCAL
            self.assertEqual(
                await fn([10, 20, 30], KEY_LOCAL, VAL_LOCAL), expect, name)
            self.assertEqual(
                await fn({k: 20}, KEY_LOCAL, VAL_LOCAL),
                {k: 20 + 2 * VAL_LOCAL}, name)


    async def check_clear(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k = self.key_for(key)
        for _ in range(3):
            expect = [10, 20, 30]
            del expect[k]
            self.assertEqual(await fn([10, 20, 30], KEY_LOCAL), expect, name)
            self.assertEqual(await fn({k: 20, 5: 3}, KEY_LOCAL), {5: 3}, name)
            served = Served()
            await fn(served, KEY_LOCAL)
            self.assertEqual(served.log, [("del", k)], name)
        with self.assertRaises(TypeError, msg=name):
            await fn((10, 20, 30), KEY_LOCAL)
        with self.assertRaises(KeyError, msg=name):
            await fn({}, KEY_LOCAL)


    async def check_take(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k = self.key_for(key)
        for _ in range(3):
            lst = [10, 20, 30]
            expect = list(lst)
            taken = expect.pop(k)
            self.assertEqual(await fn(lst, KEY_LOCAL), taken, name)
            self.assertEqual(lst, expect, name)
            d = {k: 20}
            self.assertEqual(await fn(d, KEY_LOCAL), 20, name)
            self.assertEqual(d, {}, name)
            served = Served()
            self.assertEqual(await fn(served, KEY_LOCAL), 20, name)
            self.assertEqual(served.log, [("get", k), ("del", k)], name)
        with self.assertRaises(TypeError, msg=name):
            await fn((10, 20, 30), KEY_LOCAL)
        with self.assertRaises(KeyError, msg=name):
            await fn({}, KEY_LOCAL)


    async def check_ref(self, name, obj, key, verb, val, operand):
        fn, _ = site(name, obj, key, verb, val, operand)
        k = self.key_for(key)
        for _ in range(3):
            lst = [10, 20, 30]
            ref = await fn(lst, KEY_LOCAL)
            self.assertIsInstance(ref, ItemRef, name)
            self.assertEqual(ref.deref(), lst[k], name)
            ref.store(7)
            self.assertEqual(lst[k], 7, name)
            d = {k: 20}
            ref = await fn(d, KEY_LOCAL)
            self.assertEqual(ref.deref(), 20, name)
            ref.store(7)
            self.assertEqual(d, {k: 7}, name)
            ref = await fn((10, 20, 30), KEY_LOCAL)
            self.assertEqual(ref.deref(), (10, 20, 30)[k], name)
            with self.assertRaises(TypeError, msg=name):
                ref.store(7)
            served = Served()
            ref = await fn(served, KEY_LOCAL)
            self.assertEqual(ref.deref(), 20, name)
            ref.store(7)
            self.assertEqual(served.log, [("get", k), ("set", k, 7)], name)


    CHECKS = {
        "ITEM": check_load, "STORE": check_store, "INPLACE": check_inplace,
        "CLEAR": check_clear, "TAKE": check_take, "REF": check_ref,
    }


    async def test_every_row_over_every_container(self):
        for name, obj, key, verb, val, operand in ROWS:
            await self.CHECKS[verb](self, name, obj, key, verb, val, operand)


    async def test_every_ungated_row(self):
        for name, obj, key, verb, val, operand in ROWS:
            if verb not in ("ITEM", "STORE", "INPLACE"):
                continue
            fn, _ = site(name, obj, key, verb, val, operand, cache=False)
            if verb == "ITEM":
                self.assertEqual(await fn([10, 20, 30], 1), 20, name)
                self.assertEqual(await fn({1: 20}, 1), 20, name)
            elif verb == "STORE":
                self.assertEqual(await fn([10, 20], 1, 7), [10, 7], name)
                self.assertEqual(await fn({}, 1, 7), {1: 7}, name)
            else:
                self.assertEqual(await fn([10, 20], 1, 7), [10, 27], name)
                self.assertEqual(await fn({1: 20}, 1, 7), {1: 27}, name)


    async def test_an_empty_slot_raises_name_error(self):
        for name, obj, key, verb, val, operand in ROWS:
            slots = [SLOT[role] for role, origin in (
                ("obj", obj), ("key", key), ("val", val)) if origin in ("var", "fast")]
            for slot in slots:
                fn = _cleared(name, obj, key, verb, val, operand, slot)
                with self.assertRaises(NameError, msg=(name, slot)):
                    await fn([10, 20, 30], 1, 7)


    async def test_in_place_at_count_zero_reduces_nothing(self):
        # the compiler never emits a count of zero; a hand-assembled
        # row with one stores the cell back unchanged
        for name in ("FAST_ITEM_CONST_INPLACE", "FAST_ITEM_FAST_INPLACE",
                     "VAR_ITEM_CONST_INPLACE", "VAR_ITEM_VAR_INPLACE",
                     "ITEM_CONST_INPLACE", "ITEM_INPLACE"):
            row = next(r for r in ROWS if r[0] == name)
            fn, _ = site(*row, count=0)
            self.assertEqual(await fn([10, 20], 1, 5), [10, 20], name)
            self.assertEqual(await fn({1: 20}, 1, 5), {1: 20}, name)


    async def test_elected_site_serves_a_kind_change(self):
        # a site that saw a list answers a long run of dicts and then
        # a list again the same as before; the slot states behind that
        # (demotion, re-election) are asserted in tests/native
        for name in ("FAST_ITEM_FAST", "ITEM_FAST", "FAST_ITEM",
                     "FAST_ITEM_FAST_STORE_FAST", "ITEM_FAST_STORE_CONST",
                     "FAST_ITEM_STORE", "FAST_ITEM_FAST_INPLACE_CONST",
                     "ITEM_FAST_INPLACE"):
            row = next(r for r in ROWS if r[0] == name)
            fn, _ = site(*row)
            for _ in range(10):
                out = await fn([10, 20, 30], 1, 7)
                dout = await fn({1: 20}, 1, 7)
            for _ in range(300):
                self.assertEqual(await fn({1: 20}, 1, 7), dout, name)
            self.assertEqual(await fn([10, 20, 30], 1, 7), out, name)


def _cleared(name, obj, key, verb, val, operand, slot):
    """
    The site with FAST_CLEAR of slot ahead of its row
    """

    op = OpCode[name]
    instr = [["FAST_CLEAR", slot]]
    if obj == "stack":
        instr.append(["FAST", 0])
    if key == "stack":
        instr.append(["FAST", 1])
    if verb == "STORE" and val == "stack":
        instr.append(["FAST", 2])
    if verb == "INPLACE" and operand == "stack":
        instr.append(["FAST", 2])
    instr.append([name, row_arg(op, obj, key, val, operand, 1, 7, 1)])
    instr.append(["RETURN", 1])
    code = build_code(instr, local_syms=SYMS, name=name)
    nspec = NamespaceSpec(None, SYMS, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(SYMS))

    def run(obj, key, val=None):
        return fn(obj, key, val)
    return run


# The end.
