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
Tests for the str bodies of the arithmetic electors: an ADD site on
two exact strs elects a concatenation on every family, a stack site
of three or more strs elects a join, a value the caller still holds
is never rewritten, a str subclass keeps its own __add__ and __radd__,
a non-str operand raises where it did, and every result matches
Python's + over the same operands.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import FormalsSpec, NamespaceSpec, Operand
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, opcount

import pytest


pytestmark = pytest.mark.phase_lib


ASCII = "abc"
LATIN = "caf\xe9"
UCS2 = "中文"
UCS4 = "\U0001f600!"

STRS = ["", ASCII, LATIN, UCS2, UCS4, "x" * 300]


class Shout(str):

    def __add__(self, other):
        return Shout(str.__add__(self, other).upper())

    def __radd__(self, other):
        return Shout(str.__add__(other, self).upper())


class SlotBox:
    __slots__ = ('a',)

    def __init__(self, a):
        self.a = a


class DictBox:
    def __init__(self, a):
        self.a = a


BOXES = (SlotBox, DictBox)


def make_operand_site(count=2, cache=True):
    """
    A function of count positionals adding them at one OBJECT_OPERAND
    site
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr.append(['OBJECT_ADD', count])
    instr.append(['RETURN', 1])
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def make_inplace_site(count=2, cache=True):
    """
    A function of count positionals adding them in place at one
    OBJECT_INPLACE site
    """

    syms = tuple(f'v{i}' for i in range(count))
    instr = [['FAST', i] for i in range(count)]
    instr.append(['OBJECT_INPLACE_ADD', count])
    instr.append(['RETURN', 1])
    code = build_code(instr, local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def make_nested_site(cache=True):
    """
    A function of (a, b, c) computing (a + b) + c through two
    OBJECT_OPERAND sites, so the outer site's left is a temporary
    """

    syms = ('a', 'b', 'c')
    code = build_code([
        ['FAST', 0],
        ['FAST', 1],
        ['OBJECT_ADD', 2],
        ['FAST', 2],
        ['OBJECT_ADD', 2],
        ['RETURN', 1],
    ], local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def make_fast_site(cache=True):
    """
    A function of (a, b) applying a += b through one FAST_INPLACE site
    and returning a
    """

    code = build_code([
        ['FAST', 1],
        ['FAST_INPLACE', (opcount(Operand['ADD'], 1), 0, 0, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a', 'b'), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a', 'b'), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a', 'b')))
    return fn, code


def make_const_site(const='x', cache=True):
    """
    A function of (a) applying a += const through one
    FAST_INPLACE_CONST site and returning a
    """

    code = build_code([
        ['FAST_INPLACE_CONST', (int(Operand['ADD']), 0, const, 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=('a',), name='site', cache=cache)
    nspec = NamespaceSpec(None, ('a',), ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(('a',)))
    return fn, code


def make_attr_site(cache=True):
    """
    A function of (box, v) applying box.a += v through one
    FAST_ATTR_INPLACE site and returning the box
    """

    syms = ('box', 'v')
    code = build_code([
        ['FAST', 1],
        ['FAST_ATTR_INPLACE', (opcount(Operand['ADD'], 1), 0, 'a', 0)],
        ['FAST', 0],
        ['RETURN', 1],
    ], local_syms=syms, name='site', cache=cache)
    nspec = NamespaceSpec(None, syms, ())
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(syms))
    return fn, code


def same(result, expected):
    return result == expected and type(result) is type(expected)


class PairMixin:
    """
    The checks every binary family runs; the subclass supplies the site
    builder and how a pair is applied
    """

    async def test_every_pair_through_an_elected_site(self):
        site, _ = self.make()
        for a in STRS:
            for b in STRS:
                result = await self.apply(site, a, b)
                self.assertTrue(same(result, a + b), (a, b, result))


    async def test_repeated_site_keeps_the_input_intact(self):
        site, _ = self.make()
        seed = "seed"
        held = seed
        for i in range(40):
            result = await self.apply(site, seed, str(i))
            self.assertEqual(result, "seed" + str(i))
        self.assertEqual(held, "seed")
        self.assertIs(held, seed)


    async def test_hashed_input_intact(self):
        site, _ = self.make()
        seed = "hashed"
        hash(seed)
        for i in range(20):
            self.assertEqual(await self.apply(site, seed, "!"), "hashed!")
        self.assertEqual(seed, "hashed")


    async def test_subclass_keeps_its_own_add(self):
        site, _ = self.make()
        self.assertEqual(await self.apply(site, "a", "b"), "ab")
        result = await self.apply(site, Shout("a"), "b")
        self.assertTrue(same(result, Shout("AB")), result)
        result = await self.apply(site, "a", Shout("b"))
        self.assertTrue(same(result, Shout("AB")), result)
        self.assertEqual(await self.apply(site, "c", "d"), "cd")


    async def test_non_str_operand_raises(self):
        site, _ = self.make()
        self.assertEqual(await self.apply(site, "a", "b"), "ab")
        with self.assertRaises(TypeError):
            await self.apply(site, "a", 1)
        with self.assertRaises(TypeError):
            await self.apply(site, 1, "a")
        self.assertEqual(await self.apply(site, "c", "d"), "cd")


    async def test_polymorphic_site_serves_every_type(self):
        site, _ = self.make()
        for i in range(30):
            self.assertEqual(await self.apply(site, "n", str(i)), "n" + str(i))
            self.assertEqual(await self.apply(site, i, 1), i + 1)
            self.assertEqual(await self.apply(site, 0.5, float(i)), i + 0.5)


    async def test_demoted_site_re_elects_str(self):
        site, _ = self.make()
        for i in range(10):
            self.assertEqual(await self.apply(site, "a", "b"), "ab")
            self.assertEqual(await self.apply(site, 1, 2), 3)
        for i in range(600):
            self.assertEqual(await self.apply(site, "x", str(i)), "x" + str(i))
        self.assertEqual(await self.apply(site, 1, 2), 3)


    async def test_ungated_site_matches(self):
        site, _ = self.make(cache=False)
        for a in STRS:
            for b in STRS:
                self.assertTrue(same(await self.apply(site, a, b), a + b))


class TestObjectOperandStr(PairMixin, CevalTest):

    def make(self, cache=True):
        return make_operand_site(cache=cache)

    async def apply(self, site, a, b):
        return await site(a, b)


    async def test_temporary_left_at_a_nested_site(self):
        site, _ = make_nested_site()
        for a in STRS:
            for b in STRS:
                for c in (ASCII, UCS4, ""):
                    result = await site(a, b, c)
                    self.assertTrue(same(result, a + b + c), (a, b, c))
        with self.assertRaises(TypeError):
            await site("a", "b", 1)
        self.assertEqual(await site("a", "b", "c"), "abc")


class TestObjectInplaceStr(PairMixin, CevalTest):

    def make(self, cache=True):
        return make_inplace_site(cache=cache)

    async def apply(self, site, a, b):
        return await site(a, b)


class JoinMixin:
    """
    The checks a stack family of three or more operands runs
    """

    async def test_join_of_every_kind(self):
        site, _ = self.make(count=4)
        for i in range(3):
            for a in STRS:
                for b in (ASCII, LATIN, UCS2, UCS4):
                    for c in ("", UCS4, LATIN):
                        result = await site(a, b, c, "end")
                        self.assertTrue(same(result, a + b + c + "end"), (a, b, c))


    async def test_join_of_empty_and_single_pieces(self):
        site, _ = self.make(count=3)
        self.assertEqual(await site("", "", ""), "")
        piece = "only"
        for _ in range(5):
            result = await site("", piece, "")
            self.assertEqual(result, "only")
            self.assertIs(type(result), str)
        self.assertEqual(piece, "only")


    async def test_join_with_a_non_str_raises(self):
        site, _ = self.make(count=3)
        self.assertEqual(await site("a", "b", "c"), "abc")
        with self.assertRaises(TypeError):
            await site("a", 1, "c")
        with self.assertRaises(TypeError):
            await site(1, "b", "c")
        self.assertEqual(await site("a", "b", "c"), "abc")


    async def test_join_with_a_subclass_folds(self):
        site, _ = self.make(count=3)
        self.assertEqual(await site("a", "b", "c"), "abc")
        result = await site("a", Shout("b"), "c")
        self.assertTrue(same(result, Shout("ABC")), result)
        result = await site(Shout("a"), "b", "c")
        self.assertTrue(same(result, Shout("ABC")), result)
        self.assertEqual(await site("a", "b", "c"), "abc")


    async def test_join_site_serves_numbers_too(self):
        site, _ = self.make(count=3)
        for i in range(30):
            self.assertEqual(await site("a", str(i), "c"), f"a{i}c")
            self.assertEqual(await site(i, 2, 3), i + 5)
            self.assertEqual(await site([i], [2], [3]), [i, 2, 3])


    async def test_ungated_join_matches(self):
        site, _ = self.make(count=5, cache=False)
        for a in STRS:
            self.assertEqual(await site(a, LATIN, UCS2, UCS4, a), a + LATIN + UCS2 + UCS4 + a)


class TestObjectOperandJoin(JoinMixin, CevalTest):

    def make(self, count=3, cache=True):
        return make_operand_site(count=count, cache=cache)


class TestObjectInplaceJoin(JoinMixin, CevalTest):

    def make(self, count=3, cache=True):
        return make_inplace_site(count=count, cache=cache)


class TestFastInplaceStr(PairMixin, CevalTest):

    def make(self, cache=True):
        return make_fast_site(cache=cache)

    async def apply(self, site, a, b):
        return await site(a, b)


class TestFastInplaceConstStr(CevalTest):

    async def test_every_pair_through_elected_sites(self):
        for const in STRS:
            site, _ = make_const_site(const)
            for a in STRS * 2:
                self.assertTrue(same(await site(a), a + const), (a, const))


    async def test_repeated_site_keeps_the_input_intact(self):
        site, _ = make_const_site("!")
        seed = "seed"
        hash(seed)
        for _ in range(40):
            self.assertEqual(await site(seed), "seed!")
        self.assertEqual(seed, "seed")


    async def test_subclass_and_non_str_at_an_elected_site(self):
        site, _ = make_const_site("b")
        self.assertEqual(await site("a"), "ab")
        self.assertTrue(same(await site(Shout("a")), Shout("AB")))
        with self.assertRaises(TypeError):
            await site(1)
        self.assertEqual(await site("c"), "cb")


    async def test_int_site_then_str_constant_site(self):
        int_site, _ = make_const_site(3)
        str_site, _ = make_const_site("3")
        for i in range(30):
            self.assertEqual(await int_site(i), i + 3)
            self.assertEqual(await str_site(str(i)), f"{i}3")
        with self.assertRaises(TypeError):
            await int_site("x")
        with self.assertRaises(TypeError):
            await str_site(1)


    async def test_ungated_site_matches(self):
        site, _ = make_const_site(UCS4, cache=False)
        for a in STRS:
            self.assertTrue(same(await site(a), a + UCS4))


class TestFastAttrInplaceStr(CevalTest):

    async def test_every_pair_through_an_elected_site(self):
        for cls in BOXES:
            site, _ = make_attr_site()
            for a in STRS:
                for b in STRS:
                    box = cls(a)
                    await site(box, b)
                    self.assertTrue(same(box.a, a + b), (cls, a, b))


    async def test_repeated_updates_on_one_receiver(self):
        for cls in BOXES:
            site, _ = make_attr_site()
            box = cls("")
            for i in range(300):
                await site(box, str(i))
            self.assertEqual(box.a, "".join(str(i) for i in range(300)))


    async def test_shared_value_is_untouched(self):
        for cls in BOXES:
            site, _ = make_attr_site()
            seed = "seed"
            box = cls(seed)
            await site(box, "1")
            await site(box, "2")
            self.assertEqual(seed, "seed")
            self.assertEqual(box.a, "seed12")


    async def test_subclass_and_non_str_at_an_elected_site(self):
        for cls in BOXES:
            site, _ = make_attr_site()
            box = cls("a")
            await site(box, "b")
            self.assertEqual(box.a, "ab")
            box.a = Shout("a")
            await site(box, "b")
            self.assertTrue(same(box.a, Shout("AB")))
            box.a = "a"
            with self.assertRaises(TypeError):
                await site(box, 1)
            self.assertEqual(box.a, "a")
            await site(box, "c")
            self.assertEqual(box.a, "ac")


    async def test_receiver_class_changes_under_the_site(self):
        site, _ = make_attr_site()
        boxes = [SlotBox("s"), DictBox("d")]
        for i in range(40):
            await site(boxes[i % 2], "x")
        self.assertEqual(boxes[0].a, "s" + "x" * 20)
        self.assertEqual(boxes[1].a, "d" + "x" * 20)


    async def test_unset_attribute_raises_after_the_election(self):
        site, _ = make_attr_site()
        box = SlotBox("a")
        await site(box, "b")
        await site(box, "c")
        empty = SlotBox.__new__(SlotBox)
        with self.assertRaises(AttributeError):
            await site(empty, "x")
        await site(box, "d")
        self.assertEqual(box.a, "abcd")


    async def test_mixed_types_at_an_elected_site(self):
        for cls in BOXES:
            site, _ = make_attr_site()
            box = cls("a")
            await site(box, "b")
            box.a = 1
            await site(box, 2)
            self.assertEqual(box.a, 3)
            box.a = "z"
            await site(box, "y")
            self.assertEqual(box.a, "zy")


    async def test_ungated_site_matches(self):
        for cls in BOXES:
            site, _ = make_attr_site(cache=False)
            box = cls(LATIN)
            for _ in range(10):
                await site(box, UCS2)
            self.assertEqual(box.a, LATIN + UCS2 * 10)


# The end.
