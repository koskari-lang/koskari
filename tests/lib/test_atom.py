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
Unit tests for symbol and keyword atom types

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import copy
import unittest

import pytest

from koskari.lib import gensym, keyword, symbol


pytestmark = pytest.mark.phase_lib


class TestSymbol(unittest.IsolatedAsyncioTestCase):
    """Tests for the symbol type"""

    async def test_create(self):
        """symbol can be created with a string name"""
        s = symbol("foo")
        self.assertIsInstance(s, symbol)

    async def test_interned(self):
        """symbols with the same name are the same object"""
        s1 = symbol("foo")
        s2 = symbol("foo")
        self.assertIs(s1, s2)

    async def test_different_names(self):
        """symbols with different names are different objects"""
        s1 = symbol("foo")
        s2 = symbol("bar")
        self.assertIsNot(s1, s2)

    async def test_str(self):
        """str() returns the symbol name"""
        s = symbol("hello")
        self.assertEqual(str(s), "hello")

    async def test_repr(self):
        """repr() returns a readable representation"""
        s = symbol("hello")
        self.assertEqual(repr(s), "<symbol 'hello'>")

    async def test_len(self):
        """len() returns the length of the name"""
        s = symbol("hello")
        self.assertEqual(len(s), 5)

    async def test_len_empty(self):
        """len() works for empty symbol name"""
        s = symbol("")
        self.assertEqual(len(s), 0)

    async def test_from_non_string(self):
        """symbol can be created from non-string via str()"""
        s = symbol(42)
        self.assertEqual(str(s), "42")

    async def test_split(self):
        """split() returns a list of symbols"""
        s = symbol("foo.bar.baz")
        parts = s.split(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], symbol)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    async def test_name_from_str_subclass_is_a_plain_str(self):
        """
        An atom's name is a true str: a str subclass instance, even one
        whose __str__ returns itself, is copied, so split is str.split
        and the subclass interns to the same symbol as the plain name
        """

        class Odd(str):
            __str__ = lambda self: self

            def split(self, *args, **kwds):
                return 5

        sym = symbol(Odd("a/b"))
        self.assertIs(type(str(sym)), str)
        self.assertIs(sym, symbol("a/b"))
        parts = sym.split("/")
        self.assertEqual(parts, [symbol("a"), symbol("b")])

        kw = keyword(Odd("c/d"))
        self.assertIs(type(str(kw)), str)
        self.assertEqual(kw.split("/"), [keyword("c"), keyword("d")])

    async def test_split_interned(self):
        """split() results are interned symbols"""
        s = symbol("foo.bar")
        parts = s.split(".")
        self.assertIs(parts[0], symbol("foo"))
        self.assertIs(parts[1], symbol("bar"))

    async def test_split_maxsplit(self):
        """split() respects maxsplit argument"""
        s = symbol("a.b.c.d")
        parts = s.split(".", 2)
        self.assertEqual(len(parts), 3)
        self.assertEqual(str(parts[2]), "c.d")

    async def test_rsplit(self):
        """rsplit() returns a list of symbols"""
        s = symbol("foo.bar.baz")
        parts = s.rsplit(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], symbol)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    async def test_rsplit_maxsplit(self):
        """rsplit() respects maxsplit argument"""
        s = symbol("a.b.c.d")
        parts = s.rsplit(".", 2)
        self.assertEqual(len(parts), 3)
        self.assertEqual(str(parts[0]), "a.b")

    async def test_no_named_args(self):
        """symbol() rejects keyword arguments"""
        with self.assertRaises(TypeError):
            symbol(name="foo")

    async def test_requires_name(self):
        """symbol() requires a name argument"""
        with self.assertRaises(TypeError):
            symbol()


class TestKeyword(unittest.IsolatedAsyncioTestCase):
    """Tests for the keyword type"""

    async def test_create(self):
        """keyword can be created with a string name"""
        k = keyword("foo")
        self.assertIsInstance(k, keyword)

    async def test_interned(self):
        """keywords with the same name are the same object"""
        k1 = keyword("foo")
        k2 = keyword("foo")
        self.assertIs(k1, k2)

    async def test_different_names(self):
        """keywords with different names are different objects"""
        k1 = keyword("foo")
        k2 = keyword("bar")
        self.assertIsNot(k1, k2)

    async def test_str(self):
        """str() returns the keyword name"""
        k = keyword("hello")
        self.assertEqual(str(k), "hello")

    async def test_repr(self):
        """repr() returns a readable representation"""
        k = keyword("hello")
        self.assertEqual(repr(k), "<keyword 'hello'>")

    async def test_len(self):
        """len() returns the length of the name"""
        k = keyword("hello")
        self.assertEqual(len(k), 5)

    async def test_strip_leading_colon(self):
        """keyword strips leading colons from name"""
        k = keyword(":foo")
        self.assertEqual(str(k), "foo")

    async def test_strip_trailing_colon(self):
        """keyword strips trailing colons from name"""
        k = keyword("foo:")
        self.assertEqual(str(k), "foo")

    async def test_strip_both_colons(self):
        """keyword strips colons from both ends"""
        k = keyword(":foo:")
        self.assertEqual(str(k), "foo")

    async def test_strip_multiple_colons(self):
        """keyword strips multiple colons from ends"""
        k = keyword(":::foo:::")
        self.assertEqual(str(k), "foo")

    async def test_colon_variants_interned(self):
        """keywords with same name after stripping are interned together"""
        k1 = keyword("foo")
        k2 = keyword(":foo")
        k3 = keyword("foo:")
        k4 = keyword(":foo:")
        self.assertIs(k1, k2)
        self.assertIs(k1, k3)
        self.assertIs(k1, k4)

    async def test_internal_colons_preserved(self):
        """colons in the middle of the name are preserved"""
        k = keyword("foo:bar")
        self.assertEqual(str(k), "foo:bar")

    async def test_from_non_string(self):
        """keyword can be created from non-string via str()"""
        k = keyword(42)
        self.assertEqual(str(k), "42")

    async def test_split(self):
        """split() returns a list of keywords"""
        k = keyword("foo.bar.baz")
        parts = k.split(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], keyword)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    async def test_split_interned(self):
        """split() results are interned keywords"""
        k = keyword("foo.bar")
        parts = k.split(".")
        self.assertIs(parts[0], keyword("foo"))
        self.assertIs(parts[1], keyword("bar"))

    async def test_rsplit(self):
        """rsplit() returns a list of keywords"""
        k = keyword("foo.bar.baz")
        parts = k.rsplit(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], keyword)

    async def test_no_named_args(self):
        """keyword() rejects keyword arguments"""
        with self.assertRaises(TypeError):
            keyword(name="foo")

    async def test_requires_name(self):
        """keyword() requires a name argument"""
        with self.assertRaises(TypeError):
            keyword()


class TestGensym(unittest.IsolatedAsyncioTestCase):
    """Tests for the gensym function"""

    async def test_returns_symbol(self):
        """gensym returns a symbol"""
        g = gensym()
        self.assertIsInstance(g, symbol)

    async def test_unique(self):
        """each gensym call returns a unique symbol"""
        g1 = gensym()
        g2 = gensym()
        self.assertIsNot(g1, g2)

    async def test_many_unique(self):
        """many gensym calls all return unique symbols"""
        symbols = [gensym() for _ in range(100)]
        unique = set(id(s) for s in symbols)
        self.assertEqual(len(unique), 100)

    async def test_with_name(self):
        """gensym with name prefix includes it in the symbol name"""
        g = gensym("test")
        name = str(g)
        self.assertTrue(name.startswith("test#"))

    async def test_with_name_unique(self):
        """gensym with same name prefix still returns unique symbols"""
        g1 = gensym("foo")
        g2 = gensym("foo")
        self.assertIsNot(g1, g2)

    async def test_without_name(self):
        """gensym without name uses default prefix"""
        g = gensym()
        name = str(g)
        self.assertTrue(name.startswith("<gensym>#"))

    async def test_with_predicate_accepted(self):
        """gensym with predicate that returns True accepts the symbol"""
        called = []

        def predicate(sym):
            called.append(sym)
            return True

        g = gensym("test", predicate)
        self.assertEqual(len(called), 1)
        self.assertIs(g, called[0])

    async def test_with_predicate_rejected(self):
        """gensym with predicate that returns False retries"""
        call_count = [0]

        def predicate(sym):
            call_count[0] += 1
            # Accept on third try
            return call_count[0] >= 3

        g = gensym("test", predicate)
        self.assertEqual(call_count[0], 3)

    async def test_none_predicate(self):
        """gensym with None predicate works like no predicate"""
        g = gensym("test", None)
        self.assertIsInstance(g, symbol)

    async def test_predicate_that_raises(self):
        """gensym propagates an exception raised by the predicate"""
        def predicate(sym):
            raise KeyError("no")

        with self.assertRaises(KeyError):
            gensym("test", predicate)

    async def test_predicate_result_bool_that_raises(self):
        """gensym propagates an exception from the result's __bool__"""
        class Nasty:
            def __bool__(self):
                raise RuntimeError("nope")

        def predicate(sym):
            return Nasty()

        with self.assertRaises(RuntimeError):
            gensym("test", predicate)


class TestSymbolKeywordDistinct(unittest.IsolatedAsyncioTestCase):
    """Tests that symbol and keyword are distinct types"""

    async def test_different_types(self):
        """symbol and keyword are different types"""
        self.assertIsNot(symbol, keyword)

    async def test_same_name_different_type(self):
        """symbol and keyword with same name are different objects"""
        s = symbol("foo")
        k = keyword("foo")
        self.assertIsNot(s, k)
        self.assertNotEqual(type(s), type(k))

    async def test_not_isinstance(self):
        """symbols are not keywords and vice versa"""
        s = symbol("foo")
        k = keyword("foo")
        self.assertNotIsInstance(s, keyword)
        self.assertNotIsInstance(k, symbol)


class TestAtomHash(unittest.IsolatedAsyncioTestCase):
    """
    An interned atom hashes as its name does, so the hash follows the
    spelling and not the address
    """

    async def test_symbol_hashes_as_its_name(self):
        self.assertEqual(hash(symbol("hashed")), hash("hashed"))

    async def test_keyword_hashes_as_its_name(self):
        self.assertEqual(hash(keyword("hashed")), hash("hashed"))

    async def test_gensym_hashes_as_its_name(self):
        g = gensym("hashed")
        self.assertEqual(hash(g), hash(str(g)))

    async def test_same_name_different_type_still_distinct_keys(self):
        s = symbol("shared")
        k = keyword("shared")
        self.assertEqual(hash(s), hash(k))
        table = {s: 1, k: 2}
        self.assertEqual(table[s], 1)
        self.assertEqual(table[k], 2)


class TestAtomCopy(unittest.IsolatedAsyncioTestCase):
    """
    An interned atom is its own value: copy and deepcopy hand back the
    atom itself
    """

    def atoms(self):
        return (
            symbol("copied"),
            keyword("copied"),
            gensym("copied"),
            gensym(symbol("copied"), marks=(5,)),
        )

    async def test_copy_is_self(self):
        for atom in self.atoms():
            with self.subTest(atom=atom):
                self.assertIs(copy.copy(atom), atom)

    async def test_deepcopy_is_self(self):
        for atom in self.atoms():
            with self.subTest(atom=atom):
                self.assertIs(copy.deepcopy(atom), atom)

    async def test_deepcopy_container_keeps_atoms(self):
        s = symbol("held")
        k = keyword("held")
        held = {s: [s, k]}
        dup = copy.deepcopy(held)
        self.assertIsNot(dup[s], held[s])
        self.assertIs(dup[s][0], s)
        self.assertIs(dup[s][1], k)


# The end.
