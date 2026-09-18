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
Tests for the in-place item electors: FAST_ITEM_INPLACE,
FAST_CONST_ITEM_INPLACE, FAST_ITEM_INPLACE_CONST and
FAST_CONST_ITEM_INPLACE_CONST sites elect their operator's int or
float body for the item and operand they first see, find the cell by
an exact list or dict check inside the body, decline to the generic
body for an index outside the list, a key the dict lacks, or an
arithmetic decline, count type-guard misses toward a demotion, and
return the same results as the object protocol throughout.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

import pytest

from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount


pytestmark = pytest.mark.phase_lib


INT_OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('MODULO', operator.mod),
    ('FLOOR_DIVIDE', operator.floordiv), ('BIT_AND', operator.and_),
    ('BIT_OR', operator.or_), ('BIT_XOR', operator.xor),
    ('BIT_LEFT', operator.lshift), ('BIT_RIGHT', operator.rshift),
    ('TRUE_DIVIDE', operator.truediv), ('POWER', operator.pow),
]

INT_PAIRS = [
    (7, 3), (-7, 3), (300, 5), (2 ** 40, 3), (2 ** 62, 2), (2 ** 100, 3),
    (0, 5), (7, 0),
]

FLOAT_OPERATORS = [
    ('ADD', operator.add), ('SUBTRACT', operator.sub),
    ('MULTIPLY', operator.mul), ('TRUE_DIVIDE', operator.truediv),
    ('MODULO', operator.mod), ('FLOOR_DIVIDE', operator.floordiv),
]

FLOAT_PAIRS = [
    (1.5, 2.0), (-2.25, 0.5), (1e300, 3.0), (0.0, -1.0), (7.5, -0.0),
]


def make_site(instr, syms, cache=True):
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(syms)), code


def stack_site(opname, cache=True, count=1):
    """
    (obj, key, v...) applying obj[key] OP= v... through FAST_ITEM_INPLACE,
    returning obj
    """

    syms = ('obj', 'key') + tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i + 2] for i in range(count)]
    instr += [
        ['FAST_ITEM_INPLACE', (opcount(Operand[opname], count), 0, 1, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ]
    return make_site(instr, syms, cache=cache)


def object_stack_site(opname, cache=True, count=1):
    """
    (obj, key, v...) applying obj[key] OP= v... through ITEM_INPLACE,
    the object and key on the stack below the values
    """

    syms = ('obj', 'key') + tuple(f'v{i}' for i in range(count))
    loads = [['FAST', i] for i in range(2 + count)]
    return make_site(
        loads + [['ITEM_INPLACE', opcount(Operand[opname], count)],
                 ['CONST', None], ['RETURN', 1]],
        syms, cache=cache)


def object_stack_const_key_site(opname, key, cache=True, count=1):
    """
    (obj, v...) applying obj[key] OP= v... through CONST_ITEM_INPLACE,
    the object on the stack below the values and the key a constant
    """

    syms = ('obj',) + tuple(f'v{i}' for i in range(count))
    loads = [['FAST', i] for i in range(1 + count)]
    return make_site(
        loads + [['CONST_ITEM_INPLACE', (opcount(Operand[opname], count), key, 0, 0)],
                 ['CONST', None], ['RETURN', 1]],
        syms, cache=cache)


def const_key_stack_site(opname, key, cache=True):
    instr = [
        ['FAST', 1],
        ['FAST_CONST_ITEM_INPLACE', (opcount(Operand[opname], 1), 0, key, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ]
    return make_site(instr, ('obj', 'v'), cache=cache)


def const_rhs_site(opname, rhs, cache=True):
    instr = [
        ['FAST_ITEM_INPLACE_CONST', (int(Operand[opname]), 0, 1, rhs)],
        ['FAST', 0],
        ['RETURN', 1],
    ]
    return make_site(instr, ('obj', 'key'), cache=cache)


def const_both_site(opname, key, rhs, cache=True):
    instr = [
        ['FAST_CONST_ITEM_INPLACE_CONST', (int(Operand[opname]), 0, key, rhs)],
        ['FAST', 0],
        ['RETURN', 1],
    ]
    return make_site(instr, ('obj',), cache=cache)


def same(result, expected):
    return repr(result) == repr(expected) and type(result) is type(expected)


class ListSub(list):
    def __getitem__(self, key):
        return 100

    def __setitem__(self, key, val):
        list.__setitem__(self, key, ("sub", val))


class TestCacheSlots(CevalTest):

    def test_slotted_rows_request_one_slot(self):
        for maker in (lambda c: stack_site('ADD', cache=c),
                      lambda c: const_key_stack_site('ADD', 0, cache=c),
                      lambda c: const_rhs_site('ADD', 1, cache=c),
                      lambda c: const_both_site('ADD', 0, 1, cache=c),
                      lambda c: object_stack_site('ADD', cache=c),
                      lambda c: object_stack_const_key_site('ADD', 0, cache=c)):
            self.assertEqual(maker(True)[1].cache_slots, 1)
            self.assertEqual(maker(False)[1].cache_slots, 0)


    def test_var_rows_request_no_slot(self):
        for instr in (
                [['VAR_ITEM_INPLACE', (opcount(Operand.ADD, 1), 'a', 'k', 0)]],
                [['VAR_CONST_ITEM_INPLACE', (opcount(Operand.ADD, 1), 'a', 0, 0)]],
                [['VAR_ITEM_INPLACE_CONST', (int(Operand.ADD), 'a', 'k', 1)]],
                [['VAR_CONST_ITEM_INPLACE_CONST', (int(Operand.ADD), 'a', 0, 1)]]):
            code = build_code(instr + [['RETURN', 1]])
            self.assertEqual(code.cache_slots, 0, instr)


class TestFastItemInplaceElector(CevalTest):

    async def test_every_int_operator_on_list_and_dict_cells(self):
        for opname, pyop in INT_OPERATORS:
            site, _ = stack_site(opname)
            for a, b in INT_PAIRS * 2:
                for container, key in (([a], 0), ({"k": a}, "k")):
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(container, key, b)
                        continue
                    if opname == 'BIT_LEFT' and b > 64:
                        continue
                    await site(container, key, b)
                    self.assertTrue(same(container[key], expected),
                                    (opname, a, b, container[key]))


    async def test_every_float_operator(self):
        for opname, pyop in FLOAT_OPERATORS:
            site, _ = stack_site(opname)
            for a, b in FLOAT_PAIRS * 2:
                lst = [a]
                try:
                    expected = pyop(a, b)
                except Exception as exc:
                    with self.assertRaises(type(exc)):
                        await site(lst, 0, b)
                    continue
                await site(lst, 0, b)
                self.assertTrue(same(lst[0], expected), (opname, a, b, lst[0]))


    async def test_elected_site_declines_and_misses(self):
        for cache in (True, False):
            site, _ = stack_site('ADD', cache=cache)
            lst = [1, 2, 3]
            for i in range(-3, 3):
                await site(lst, i, 10)
            self.assertEqual(lst, [21, 22, 23])
            with self.assertRaises(IndexError):
                await site(lst, 3, 1)
            with self.assertRaises(IndexError):
                await site(lst, 2 ** 70, 1)
            with self.assertRaises(KeyError):
                await site({}, "k", 1)
            with self.assertRaises(TypeError):
                await site({}, [], 1)
            # other kinds miss the guard and take the protocol
            d = {"k": "a"}
            await site(d, "k", "b")
            self.assertEqual(d["k"], "ab")
            inner = [1]
            d = {"k": inner}
            await site(d, "k", [2])
            self.assertIs(d["k"], inner)
            self.assertEqual(inner, [1, 2])
            sub = ListSub([0])
            await site(sub, 0, 1)
            self.assertEqual(list.__getitem__(sub, 0), ("sub", 101))
            with self.assertRaises(TypeError):
                await site((1,), 0, 1)
            with self.assertRaises(TypeError):
                await site(lst, 0, "s")
            # bool operands are not exact ints
            await site(lst, 0, True)
            self.assertEqual(lst[0], 22)
            self.assertIs(type(lst[0]), int)
            # and the int body still serves
            await site(lst, 1, 1)
            self.assertEqual(lst[1], 23)


    async def test_demoted_site_re_elects_and_serves_every_kind(self):
        site, _ = stack_site('ADD')
        lst, fl = [0], [0.0]
        for _ in range(10):
            await site(lst, 0, 1)
            await site(fl, 0, 0.5)
        for i in range(600):
            await site(fl, 0, 0.5)
        await site(lst, 0, 1)
        self.assertEqual(lst, [11])
        self.assertEqual(fl, [305.0])


    async def test_two_value_site_is_generic_and_correct(self):
        site, _ = stack_site('ADD', count=2)
        lst = [1]
        for _ in range(5):
            await site(lst, 0, 10, 100)
        self.assertEqual(lst, [551])


    async def test_error_at_an_elected_site_propagates(self):
        site, _ = stack_site('MODULO')
        lst = [7]
        await site(lst, 0, 3)
        self.assertEqual(lst, [1])
        with self.assertRaises(ZeroDivisionError):
            await site(lst, 0, 0)
        await site([8], 0, 3)


class TestItemInplaceElector(CevalTest):
    """
    The stack shapes: object and key below the value, or object below
    the value and a constant key
    """

    async def test_every_int_operator_on_list_and_dict_cells(self):
        for opname, pyop in INT_OPERATORS:
            site, _ = object_stack_site(opname)
            for a, b in INT_PAIRS * 2:
                for container, key in (([a], 0), ({"k": a}, "k")):
                    try:
                        expected = pyop(a, b)
                    except Exception as exc:
                        with self.assertRaises(type(exc)):
                            await site(container, key, b)
                        continue
                    if opname == 'BIT_LEFT' and b > 64:
                        continue
                    await site(container, key, b)
                    self.assertTrue(same(container[key], expected),
                                    (opname, a, b, container[key]))


    async def test_every_float_operator_with_constant_key(self):
        for opname, pyop in FLOAT_OPERATORS:
            site, _ = object_stack_const_key_site(opname, 0)
            for a, b in FLOAT_PAIRS * 2:
                lst = [a]
                try:
                    expected = pyop(a, b)
                except Exception as exc:
                    with self.assertRaises(type(exc)):
                        await site(lst, b)
                    continue
                await site(lst, b)
                self.assertTrue(same(lst[0], expected), (opname, a, b, lst[0]))


    async def test_elected_site_declines_and_misses(self):
        for cache in (True, False):
            site, _ = object_stack_site('ADD', cache=cache)
            lst = [1, 2, 3]
            for i in range(-3, 3):
                await site(lst, i, 10)
            self.assertEqual(lst, [21, 22, 23])
            with self.assertRaises(IndexError):
                await site(lst, 3, 1)
            with self.assertRaises(KeyError):
                await site({}, "k", 1)
            d = {"k": "a"}
            await site(d, "k", "b")
            self.assertEqual(d["k"], "ab")
            sub = ListSub([0])
            await site(sub, 0, 1)
            self.assertEqual(list.__getitem__(sub, 0), ("sub", 101))
            with self.assertRaises(TypeError):
                await site((1,), 0, 1)
            await site(lst, 1, 1)
            self.assertEqual(lst[1], 23)
            # the stack is empty after every shape
            self.assertIsNone(await site(lst, 0, 0))


    async def test_demoted_site_re_elects_and_serves_every_kind(self):
        site, _ = object_stack_site('ADD')
        lst, fl = [0], [0.0]
        for _ in range(10):
            await site(lst, 0, 1)
            await site(fl, 0, 0.5)
        for _ in range(600):
            await site(fl, 0, 0.5)
        await site(lst, 0, 1)
        self.assertEqual(lst, [11])
        self.assertEqual(fl, [305.0])
        site, _ = object_stack_const_key_site('ADD', 0)
        lst, fl = [0], [0.0]
        for _ in range(10):
            await site(lst, 1)
            await site(fl, 0.5)
        for _ in range(600):
            await site(fl, 0.5)
        await site(lst, 1)
        self.assertEqual(lst, [11])
        self.assertEqual(fl, [305.0])


    async def test_two_value_site_is_generic_and_correct(self):
        site, _ = object_stack_site('ADD', count=2)
        lst = [1]
        for _ in range(5):
            await site(lst, 0, 10, 100)
        self.assertEqual(lst, [551])
        site, _ = object_stack_const_key_site('SUBTRACT', 0, count=2)
        lst = [1000]
        for _ in range(3):
            await site(lst, 10, 100)
        self.assertEqual(lst, [670])


    async def test_error_at_an_elected_site_propagates(self):
        site, _ = object_stack_site('MODULO')
        lst = [7]
        await site(lst, 0, 3)
        self.assertEqual(lst, [1])
        with self.assertRaises(ZeroDivisionError):
            await site(lst, 0, 0)
        await site([8], 0, 3)


class StrSub(str):
    def __add__(self, other):
        return ("sub", other)


class TestItemInplaceStrElector(CevalTest):
    """
    The str body of every item in-place family: + over an exact-str
    cell and operand, on a list and a dict cell
    """

    def sites(self):
        return (
            (lambda: stack_site('ADD'), lambda s, c, k, v: s(c, k, v)),
            (lambda: const_key_stack_site('ADD', 0), lambda s, c, k, v: s(c, v)),
            (lambda: object_stack_site('ADD'), lambda s, c, k, v: s(c, k, v)),
            (lambda: object_stack_const_key_site('ADD', 0),
             lambda s, c, k, v: s(c, v)),
        )

    async def test_str_cells_concatenate_on_list_and_dict(self):
        for make, call in self.sites():
            site, _ = make()
            lst = ["a"]
            for piece in ("b", "c", "d"):
                await call(site, lst, 0, piece)
            self.assertEqual(lst, ["abcd"])
            d = {0: "x"}
            await call(site, d, 0, "y")
            await call(site, d, 0, "z")
            self.assertEqual(d, {0: "xyz"})
            # the cell holds a new object each time
            before = lst[0]
            await call(site, lst, 0, "")
            self.assertEqual(lst[0], before)

    async def test_constant_operand_families(self):
        site, _ = const_rhs_site('ADD', "!")
        lst = ["hi"]
        for _ in range(3):
            await site(lst, 0)
        self.assertEqual(lst, ["hi!!!"])
        site, _ = const_both_site('ADD', 0, "?")
        lst = ["a"]
        for _ in range(3):
            await site(lst)
        self.assertEqual(lst, ["a???"])
        d = {0: ""}
        await site(d)
        self.assertEqual(d, {0: "?"})

    async def test_subclass_and_other_operands_miss(self):
        site, _ = stack_site('ADD')
        lst = ["a"]
        await site(lst, 0, "b")
        sub = [StrSub("s")]
        await site(sub, 0, "t")
        self.assertEqual(sub, [("sub", "t")])
        with self.assertRaises(TypeError):
            await site(lst, 0, 1)
        with self.assertRaises(TypeError):
            await site([1], 0, "b")
        lst2 = ["x"]
        await site(lst2, 0, StrSub("y"))
        self.assertEqual(lst2, ["xy"])
        self.assertIs(type(lst2[0]), str)
        await site(lst, 0, "c")
        self.assertEqual(lst, ["abc"])

    async def test_declines_take_the_generic_body(self):
        site, _ = stack_site('ADD')
        lst = ["a", "b"]
        await site(lst, 1, "c")
        with self.assertRaises(IndexError):
            await site(lst, 5, "c")
        with self.assertRaises(KeyError):
            await site({}, "k", "c")
        await site(lst, -1, "d")
        self.assertEqual(lst, ["a", "bcd"])


class TestFastConstItemInplaceElector(CevalTest):

    async def test_int_and_float_cells(self):
        site, _ = const_key_stack_site('MULTIPLY', 1)
        lst = [1, 2, 3]
        for _ in range(5):
            await site(lst, 3)
        self.assertEqual(lst, [1, 486, 3])
        d = {1: 1.5}
        await site(d, 2.0)
        self.assertEqual(d, {1: 3.0})
        with self.assertRaises(IndexError):
            await site([0], 2)
        with self.assertRaises(KeyError):
            await site({}, 2)
        await site(lst, 2)
        self.assertEqual(lst[1], 972)

        site, _ = const_key_stack_site('ADD', "k")
        d = {"k": 1}
        for _ in range(5):
            await site(d, 2)
        self.assertEqual(d, {"k": 11})
        with self.assertRaises(TypeError):
            await site([1], 2)


class TestFastItemInplaceConstElector(CevalTest):

    async def test_int_rhs(self):
        site, _ = const_rhs_site('SUBTRACT', 1)
        lst = [10, 20]
        for _ in range(5):
            await site(lst, 0)
            await site(lst, -1)
        self.assertEqual(lst, [5, 15])
        d = {"k": 2 ** 100}
        await site(d, "k")
        self.assertEqual(d, {"k": 2 ** 100 - 1})
        d = {"k": 1.5}
        await site(d, "k")
        self.assertEqual(d, {"k": 0.5})
        with self.assertRaises(IndexError):
            await site(lst, 5)
        with self.assertRaises(TypeError):
            await site(["s"], 0)
        await site(lst, 0)
        self.assertEqual(lst[0], 4)


    async def test_wide_constant_keeps_the_site_generic(self):
        site, _ = const_rhs_site('ADD', 2 ** 70)
        lst = [1]
        for _ in range(5):
            await site(lst, 0)
        self.assertEqual(lst, [1 + 5 * 2 ** 70])


    async def test_float_rhs(self):
        site, _ = const_rhs_site('MULTIPLY', 0.5)
        lst = [8.0]
        for _ in range(3):
            await site(lst, 0)
        self.assertEqual(lst, [1.0])
        lst = [8]
        await site(lst, 0)
        self.assertEqual(lst, [4.0])


class TestFastConstItemInplaceConstElector(CevalTest):

    async def test_stack_free_site(self):
        site, _ = const_both_site('ADD', "n", 1)
        d = {"n": 0}
        for _ in range(100):
            await site(d)
        self.assertEqual(d, {"n": 100})
        with self.assertRaises(KeyError):
            await site({})
        with self.assertRaises(TypeError):
            await site([0])

        site, _ = const_both_site('BIT_LEFT', 0, 1)
        lst = [1]
        for _ in range(70):
            await site(lst)
        self.assertEqual(lst, [2 ** 70])


    async def test_demoted_site_re_elects(self):
        site, _ = const_both_site('ADD', 0, 1)
        lst, fl = [0], [0.0]
        for _ in range(10):
            await site(lst)
            await site(fl)
        for _ in range(600):
            await site(fl)
        await site(lst)
        self.assertEqual(lst, [11])
        self.assertEqual(fl, [610.0])


# The end.
