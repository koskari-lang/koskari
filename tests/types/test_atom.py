"""
Unit tests for symbol and keyword atom types

author: Christopher O'Brien <obriencj@gmail.com>
license: GPL v.3
"""


import unittest

from koskari.lib import symbol, keyword, gensym


class TestSymbol(unittest.TestCase):
    """Tests for the symbol type"""

    def test_create(self):
        """symbol can be created with a string name"""
        s = symbol("foo")
        self.assertIsInstance(s, symbol)

    def test_interned(self):
        """symbols with the same name are the same object"""
        s1 = symbol("foo")
        s2 = symbol("foo")
        self.assertIs(s1, s2)

    def test_different_names(self):
        """symbols with different names are different objects"""
        s1 = symbol("foo")
        s2 = symbol("bar")
        self.assertIsNot(s1, s2)

    def test_str(self):
        """str() returns the symbol name"""
        s = symbol("hello")
        self.assertEqual(str(s), "hello")

    def test_repr(self):
        """repr() returns a readable representation"""
        s = symbol("hello")
        self.assertEqual(repr(s), "<symbol 'hello'>")

    def test_len(self):
        """len() returns the length of the name"""
        s = symbol("hello")
        self.assertEqual(len(s), 5)

    def test_len_empty(self):
        """len() works for empty symbol name"""
        s = symbol("")
        self.assertEqual(len(s), 0)

    def test_from_non_string(self):
        """symbol can be created from non-string via str()"""
        s = symbol(42)
        self.assertEqual(str(s), "42")

    def test_split(self):
        """split() returns a list of symbols"""
        s = symbol("foo.bar.baz")
        parts = s.split(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], symbol)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    def test_split_interned(self):
        """split() results are interned symbols"""
        s = symbol("foo.bar")
        parts = s.split(".")
        self.assertIs(parts[0], symbol("foo"))
        self.assertIs(parts[1], symbol("bar"))

    def test_split_maxsplit(self):
        """split() respects maxsplit argument"""
        s = symbol("a.b.c.d")
        parts = s.split(".", 2)
        self.assertEqual(len(parts), 3)
        self.assertEqual(str(parts[2]), "c.d")

    def test_rsplit(self):
        """rsplit() returns a list of symbols"""
        s = symbol("foo.bar.baz")
        parts = s.rsplit(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], symbol)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    def test_rsplit_maxsplit(self):
        """rsplit() respects maxsplit argument"""
        s = symbol("a.b.c.d")
        parts = s.rsplit(".", 2)
        self.assertEqual(len(parts), 3)
        self.assertEqual(str(parts[0]), "a.b")

    def test_no_named_args(self):
        """symbol() rejects keyword arguments"""
        with self.assertRaises(TypeError):
            symbol(name="foo")

    def test_requires_name(self):
        """symbol() requires a name argument"""
        with self.assertRaises(TypeError):
            symbol()


class TestKeyword(unittest.TestCase):
    """Tests for the keyword type"""

    def test_create(self):
        """keyword can be created with a string name"""
        k = keyword("foo")
        self.assertIsInstance(k, keyword)

    def test_interned(self):
        """keywords with the same name are the same object"""
        k1 = keyword("foo")
        k2 = keyword("foo")
        self.assertIs(k1, k2)

    def test_different_names(self):
        """keywords with different names are different objects"""
        k1 = keyword("foo")
        k2 = keyword("bar")
        self.assertIsNot(k1, k2)

    def test_str(self):
        """str() returns the keyword name"""
        k = keyword("hello")
        self.assertEqual(str(k), "hello")

    def test_repr(self):
        """repr() returns a readable representation"""
        k = keyword("hello")
        self.assertEqual(repr(k), "<keyword 'hello'>")

    def test_len(self):
        """len() returns the length of the name"""
        k = keyword("hello")
        self.assertEqual(len(k), 5)

    def test_strip_leading_colon(self):
        """keyword strips leading colons from name"""
        k = keyword(":foo")
        self.assertEqual(str(k), "foo")

    def test_strip_trailing_colon(self):
        """keyword strips trailing colons from name"""
        k = keyword("foo:")
        self.assertEqual(str(k), "foo")

    def test_strip_both_colons(self):
        """keyword strips colons from both ends"""
        k = keyword(":foo:")
        self.assertEqual(str(k), "foo")

    def test_strip_multiple_colons(self):
        """keyword strips multiple colons from ends"""
        k = keyword(":::foo:::")
        self.assertEqual(str(k), "foo")

    def test_colon_variants_interned(self):
        """keywords with same name after stripping are interned together"""
        k1 = keyword("foo")
        k2 = keyword(":foo")
        k3 = keyword("foo:")
        k4 = keyword(":foo:")
        self.assertIs(k1, k2)
        self.assertIs(k1, k3)
        self.assertIs(k1, k4)

    def test_internal_colons_preserved(self):
        """colons in the middle of the name are preserved"""
        k = keyword("foo:bar")
        self.assertEqual(str(k), "foo:bar")

    def test_from_non_string(self):
        """keyword can be created from non-string via str()"""
        k = keyword(42)
        self.assertEqual(str(k), "42")

    def test_split(self):
        """split() returns a list of keywords"""
        k = keyword("foo.bar.baz")
        parts = k.split(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], keyword)
        self.assertEqual(str(parts[0]), "foo")
        self.assertEqual(str(parts[1]), "bar")
        self.assertEqual(str(parts[2]), "baz")

    def test_split_interned(self):
        """split() results are interned keywords"""
        k = keyword("foo.bar")
        parts = k.split(".")
        self.assertIs(parts[0], keyword("foo"))
        self.assertIs(parts[1], keyword("bar"))

    def test_rsplit(self):
        """rsplit() returns a list of keywords"""
        k = keyword("foo.bar.baz")
        parts = k.rsplit(".")
        self.assertEqual(len(parts), 3)
        self.assertIsInstance(parts[0], keyword)

    def test_no_named_args(self):
        """keyword() rejects keyword arguments"""
        with self.assertRaises(TypeError):
            keyword(name="foo")

    def test_requires_name(self):
        """keyword() requires a name argument"""
        with self.assertRaises(TypeError):
            keyword()


class TestGensym(unittest.TestCase):
    """Tests for the gensym function"""

    def test_returns_symbol(self):
        """gensym returns a symbol"""
        g = gensym()
        self.assertIsInstance(g, symbol)

    def test_unique(self):
        """each gensym call returns a unique symbol"""
        g1 = gensym()
        g2 = gensym()
        self.assertIsNot(g1, g2)

    def test_many_unique(self):
        """many gensym calls all return unique symbols"""
        symbols = [gensym() for _ in range(100)]
        unique = set(id(s) for s in symbols)
        self.assertEqual(len(unique), 100)

    def test_with_name(self):
        """gensym with name prefix includes it in the symbol name"""
        g = gensym("test")
        name = str(g)
        self.assertTrue(name.startswith("test#"))

    def test_with_name_unique(self):
        """gensym with same name prefix still returns unique symbols"""
        g1 = gensym("foo")
        g2 = gensym("foo")
        self.assertIsNot(g1, g2)

    def test_without_name(self):
        """gensym without name uses default prefix"""
        g = gensym()
        name = str(g)
        self.assertTrue(name.startswith("<gensym>#"))

    def test_with_predicate_accepted(self):
        """gensym with predicate that returns True accepts the symbol"""
        called = []

        def predicate(sym):
            called.append(sym)
            return True

        g = gensym("test", predicate)
        self.assertEqual(len(called), 1)
        self.assertIs(g, called[0])

    def test_with_predicate_rejected(self):
        """gensym with predicate that returns False retries"""
        call_count = [0]

        def predicate(sym):
            call_count[0] += 1
            # Accept on third try
            return call_count[0] >= 3

        g = gensym("test", predicate)
        self.assertEqual(call_count[0], 3)

    def test_none_predicate(self):
        """gensym with None predicate works like no predicate"""
        g = gensym("test", None)
        self.assertIsInstance(g, symbol)


class TestSymbolKeywordDistinct(unittest.TestCase):
    """Tests that symbol and keyword are distinct types"""

    def test_different_types(self):
        """symbol and keyword are different types"""
        self.assertIsNot(symbol, keyword)

    def test_same_name_different_type(self):
        """symbol and keyword with same name are different objects"""
        s = symbol("foo")
        k = keyword("foo")
        self.assertIsNot(s, k)
        self.assertNotEqual(type(s), type(k))

    def test_not_isinstance(self):
        """symbols are not keywords and vice versa"""
        s = symbol("foo")
        k = keyword("foo")
        self.assertNotIsInstance(s, keyword)
        self.assertNotIsInstance(k, symbol)


# The end.
