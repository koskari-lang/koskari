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
Unit tests for kos_repr and __kos_repr__

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    KosPartial, cons, keyword, kos_repr, kos_repr_register,
    kos_repr_registry_clear, kos_repr_registry_snapshot, kos_repr_unregister,
    nil, pair, setcdr, symbol)


pytestmark = pytest.mark.phase_lib


class TestKosRepr(unittest.TestCase):
    """
    Tests for kos_repr and native __kos_repr__
    """

    def test_kos_repr_import(self):
        """
        kos_repr is callable on koskari.lib
        """

        self.assertTrue(callable(kos_repr))


    def test_fallback_int(self):
        """
        objects without __kos_repr__ use repr
        """

        self.assertEqual(kos_repr(42), repr(42))


    def test_plain_str_double_quotes(self):
        """
        str uses double-quote wrapping, not Python single-quote repr
        """

        self.assertEqual(kos_repr("hello"), '"hello"')
        self.assertNotEqual(kos_repr("hello"), repr("hello"))


    def test_str_escapes_internal_double_quote(self):
        """
        embedded double quotes are backslash-escaped
        """

        self.assertEqual(kos_repr('say "hi"'), '"say \\"hi\\""')


    def test_list_contains_str_kos_form(self):
        """
        pair spine uses KosUtils_Repr for str elements
        """

        p = cons("a", nil)
        self.assertEqual(kos_repr(p), '("a")')


    def test_nil(self):
        """
        nil prints as the empty-list form ()
        """

        self.assertEqual(kos_repr(nil), "()")


    def test_symbol(self):
        """
        symbol kos form is bare name
        """

        s = symbol("alpha")
        self.assertEqual(kos_repr(s), "alpha")


    def test_type_uses_normal_repr(self):
        """
        a type object reprs normally, not via its instances' __kos_repr__
        """

        # pair/symbol instances carry __kos_repr__; the type object must not
        # try to dispatch through it.
        self.assertEqual(kos_repr(pair), repr(pair))
        self.assertEqual(kos_repr(symbol), repr(symbol))
        self.assertEqual(kos_repr(type(nil)), repr(type(nil)))


    def test_keyword(self):
        """
        keyword kos form ends with colon
        """

        k = keyword("beta")
        self.assertEqual(kos_repr(k), "beta:")


    def test_empty_keyword(self):
        """
        empty keyword kos form is double colon, not lone colon
        """

        k = keyword("")
        self.assertEqual(kos_repr(k), "::")


    def test_symbol_colon(self):
        """
        lone colon is a symbol, distinct from empty keyword
        """

        s = symbol(":")
        self.assertEqual(kos_repr(s), ":")
        self.assertNotEqual(kos_repr(s), kos_repr(keyword("")))


    def test_proper_list(self):
        """
        proper list uses spaces between cars
        """

        p = cons(1, 2, 3, nil)
        self.assertEqual(kos_repr(p), "(1 2 3)")


    def test_improper_list(self):
        """
        improper list uses dot before final cdr
        """

        p = pair(1, 2)
        self.assertEqual(kos_repr(p), "(1 . 2)")


    def test_nested_pair_and_symbol(self):
        """
        nested values use kos form
        """

        p = cons(symbol("x"), cons(2, nil))
        self.assertEqual(kos_repr(p), "(x 2)")


    def test_recursive_pair(self):
        """
        recursive spine uses ellipsis like str()
        """

        p = pair(1, nil)
        setcdr(p, p)
        s = kos_repr(p)
        self.assertIn("...", s)


    def test_list_empty(self):
        """
        empty list uses hash-bracket form
        """

        self.assertEqual(kos_repr([]), "#[]")


    def test_list_values(self):
        """
        list elements use kos form inside hash-brackets
        """

        self.assertEqual(kos_repr([1, 2, 3]), "#[1 2 3]")


    def test_tuple_empty(self):
        """
        empty tuple uses hash-paren form
        """

        self.assertEqual(kos_repr(()), "#()")


    def test_tuple_values(self):
        """
        tuple elements use kos form inside hash-parens
        """

        self.assertEqual(kos_repr((1, 2, 3)), "#(1 2 3)")


    def test_dict_empty(self):
        """
        empty dict uses hash-brace form
        """

        self.assertEqual(kos_repr({}), "#{}")


    def test_dict_pairs(self):
        """
        dict entries use inline ``key: value`` form, colon glued to the key
        """

        self.assertEqual(kos_repr({"foo": 4}), '#{"foo": 4}')
        self.assertEqual(kos_repr({"foo": 4, 5: 6}), '#{"foo": 4 5: 6}')


    def test_ellipsis(self):
        """
        Ellipsis kos form is ..., not Python's Ellipsis
        """

        self.assertEqual(kos_repr(...), "...")
        self.assertEqual(kos_repr(Ellipsis), "...")
        self.assertNotEqual(kos_repr(...), repr(...))


    def test_nested_list_in_pair(self):
        """
        python list nested in pair uses hash-bracket form
        """

        p = cons(1, cons([2, 3], nil))
        self.assertEqual(kos_repr(p), "(1 #[2 3])")


    def test_recursive_list(self):
        """
        cyclic list uses ellipsis inside hash-brackets
        """

        lst = []
        lst.append(lst)
        self.assertEqual(kos_repr(lst), "#[#[...]]")


    def test_python___kos_repr__(self):
        """
        plain Python __kos_repr__ is invoked via kos_repr
        """

        class Box(object):
            def __kos_repr__(self):
                return "boxed"

        self.assertEqual(kos_repr(Box()), "boxed")


    def test___kos_repr___wrong_type(self):
        """
        non-str __kos_repr__ result raises TypeError
        """

        class Bad(object):
            def __kos_repr__(self):
                return 3

        with self.assertRaises(TypeError):
            kos_repr(Bad())


    def test___kos_repr___not_callable(self):
        """
        non-callable __kos_repr__ attribute falls back to repr
        """

        class Weird(object):
            __kos_repr__ = 1

        with self.assertRaises(TypeError):
            kos_repr(Weird())


class TestKosReprContainerMutation(unittest.TestCase):
    """
    The list and dict renderers hold each element across its repr and
    re-read the container, so an element that mutates the container
    from its repr cannot free what the renderer still reads
    """

    def test_list_element_clears_the_list(self):
        lst = []

        class Clearer:
            def __repr__(self):
                lst.clear()
                return "<clear>"

        lst.extend([Clearer(), 2, 3])
        self.assertEqual(kos_repr(lst), "#[<clear>]")
        self.assertEqual(lst, [])


    def test_list_element_grows_the_list(self):
        lst = []

        class Grower:
            def __repr__(self):
                lst.append(9)
                return "<grow>"

        lst.extend([Grower(), 2])
        self.assertEqual(kos_repr(lst), "#[<grow> 2 9]")


    def test_dict_key_deletes_its_own_entry(self):
        d = {}
        released = []

        class Val:
            def __del__(self):
                released.append(True)

            def __repr__(self):
                return "<val>"

        class Key:
            def __repr__(self):
                d.clear()
                return "<key>"

        d[Key()] = Val()
        self.assertEqual(kos_repr(d), "#{<key>: <val>}")
        self.assertEqual(released, [True])


class TestKosPartialKosRepr(unittest.TestCase):
    """
    Tests for KosPartial's Koskari s-expression kos form
    """

    def test_func_only(self):
        """
        a bare partial reprs as (partial <func>)
        """

        kp = KosPartial(print)
        self.assertEqual(kos_repr(kp), "(partial <built-in function print>)")


    def test_positionals_in_order(self):
        """
        positionals follow the func in order, each in kos form
        """

        kp = KosPartial(print, 1, 2)
        self.assertEqual(kos_repr(kp),
                         "(partial <built-in function print> 1 2)")


    def test_positional_uses_kos_form(self):
        """
        a positional symbol renders bare, not via Python repr
        """

        kp = KosPartial(print, symbol("y"))
        self.assertEqual(kos_repr(kp),
                         "(partial <built-in function print> y)")


    def test_keywords_as_kwd_tokens(self):
        """
        keywords render as name-colon token plus value in kos form
        """

        kp = KosPartial(print, sep="-")
        self.assertEqual(kos_repr(kp),
                         '(partial <built-in function print> sep: "-")')


    def test_mixed_positional_and_keyword(self):
        """
        positionals precede keywords, matching the spec example shape
        """

        kp = KosPartial(print, "x", sep="-")
        self.assertEqual(kos_repr(kp),
                         '(partial <built-in function print> "x" sep: "-")')


    def test_kos_repr_differs_from_python_repr(self):
        """
        the kos form is distinct from the functools-style Python repr
        """

        kp = KosPartial(print, 1)
        self.assertNotEqual(kos_repr(kp), repr(kp))
        self.assertIn("partial(", repr(kp))


class TestKosReprRegistry(unittest.TestCase):
    """
    Tests for the kos_repr type registry
    """

    def setUp(self):
        # snapshot so tearDown can restore, rather than clearing the
        # builtin container handlers that bootstrap registered (which the
        # whole rest of the suite relies on for koskari-style repr)
        self._registry_snapshot = kos_repr_registry_snapshot()


    def tearDown(self):
        kos_repr_registry_clear()
        for typ, handler in self._registry_snapshot.items():
            kos_repr_register(typ, handler)


    def test_register_and_kos_repr(self):
        """
        registry handler is used before repr
        """

        class Reggie(object):
            pass

        def handler(obj):
            return "reggie-out"

        kos_repr_register(Reggie, handler)
        self.assertEqual(kos_repr(Reggie()), "reggie-out")


    def test_bound_builtin_handler_is_called_not_rebound(self):
        """
        A bound builtin method registered as a handler is called with
        the object as its argument; its body never runs on the object
        as self
        """

        class Listy(list):
            pass

        kos_repr_register(Listy, {}.copy)
        try:
            with self.assertRaises(TypeError):
                kos_repr(Listy([1, 2]))
        finally:
            kos_repr_unregister(Listy)


    def test_builtin_handler_refuses_another_type(self):
        """
        A container handler taken from the snapshot and registered under
        another type raises TypeError instead of reading the object as a
        container, and called bare it raises the same
        """

        handler = kos_repr_registry_snapshot()[list]
        kos_repr_register(bytes, handler)
        try:
            with self.assertRaises(TypeError):
                kos_repr(b"not a list")
        finally:
            kos_repr_unregister(bytes)
        with self.assertRaises(TypeError):
            handler()


    def test_unregister_restores_repr(self):
        """
        after unregister, default repr path is used
        """

        class R2(object):
            pass

        kos_repr_register(R2, lambda o: "x")
        self.assertEqual(kos_repr(R2()), "x")
        kos_repr_unregister(R2)
        out = kos_repr(R2())
        self.assertTrue(out.startswith("<") or "R2" in out)


    def test_unregister_missing_is_ok(self):
        """
        unregister for unknown type does not raise
        """

        kos_repr_unregister(dict)


    def test_registry_clear(self):
        """
        clear removes handlers
        """

        class R3(object):
            pass

        kos_repr_register(R3, lambda o: "r3")
        self.assertEqual(kos_repr(R3()), "r3")
        kos_repr_registry_clear()
        out = kos_repr(R3())
        self.assertNotEqual(out, "r3")


    def test_snapshot_is_shallow_copy(self):
        """
        mutating snapshot dict keys does not remove live registry
        """

        class Snap(object):
            pass

        kos_repr_register(Snap, lambda o: "snap")
        snap = kos_repr_registry_snapshot()
        self.assertIn(Snap, snap)
        snap.clear()
        self.assertEqual(kos_repr(Snap()), "snap")


    def test_register_type_error_not_type(self):
        """
        first argument must be a type
        """

        with self.assertRaises(TypeError):
            kos_repr_register("not-a-type", lambda o: "n")


    def test_registry_before_instance___kos_repr__(self):
        """
        type registry runs before instance __kos_repr__
        """

        class Both(object):
            def __kos_repr__(self):
                return "instance"

        kos_repr_register(Both, lambda o: "type-level")
        try:
            self.assertEqual(kos_repr(Both()), "type-level")
        finally:
            kos_repr_unregister(Both)


class TestKosReprDepth(unittest.TestCase):
    """
    A deep nest is bounded by the recursion gate, as repr of Python's
    own containers is: the call returns a string or raises
    RecursionError, and never takes the process down. Which of the two
    depends on the interpreter; from 3.14 the gate measures the C stack
    itself, so a large stack walks the whole nest. A cycle is
    abbreviated, not recursed into
    """

    def nested(self, make, depth):
        value = make(())
        for _ in range(depth):
            value = make((value,))
        return value

    def assert_bounded(self, value):
        try:
            got = kos_repr(value)
        except RecursionError:
            return
        self.assertIsInstance(got, str)

    def test_deep_list_is_bounded(self):
        self.assert_bounded(self.nested(list, 100000))

    def test_deep_tuple_is_bounded(self):
        self.assert_bounded(self.nested(tuple, 100000))

    def test_deep_dict_is_bounded(self):
        value = {}
        for _ in range(100000):
            value = {"k": value}
        self.assert_bounded(value)

    def test_shallow_nest_and_cycle_still_render(self):
        self.assertEqual("#[#[#[]]]", kos_repr(self.nested(list, 2)))
        cycle = [1, 2]
        cycle.append(cycle)
        self.assertEqual("#[1 2 #[...]]", kos_repr(cycle))


# The end.
