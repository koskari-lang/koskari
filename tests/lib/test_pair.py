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
Unit tests for pair and nil types

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import (
    pair, nil, cons, car, cdr, setcar, setcdr, build_unpack_pair,
    make_recursive, break_recursive,
)

import pytest

pytestmark = pytest.mark.phase_lib


class TestPair(unittest.IsolatedAsyncioTestCase):
    """Tests for the pair type"""

    async def test_create(self):
        """pair can be created with head and tail"""
        p = pair(1, 2)
        self.assertIsInstance(p, pair)

    async def test_car(self):
        """car() returns the head element"""
        p = pair("head", "tail")
        self.assertEqual(car(p), "head")

    async def test_cdr(self):
        """cdr() returns the tail element"""
        p = pair("head", "tail")
        self.assertEqual(cdr(p), "tail")

    async def test_setcar(self):
        """setcar() modifies the head element"""
        p = pair(1, 2)
        setcar(p, "new")
        self.assertEqual(car(p), "new")

    async def test_setcdr(self):
        """setcdr() modifies the tail element"""
        p = pair(1, 2)
        setcdr(p, "new")
        self.assertEqual(cdr(p), "new")

    async def test_len(self):
        """len() always returns 2 for pairs"""
        p = pair(1, 2)
        self.assertEqual(len(p), 2)

    async def test_getitem(self):
        """pairs support indexing with [0] and [1]"""
        p = pair("a", "b")
        self.assertEqual(p[0], "a")
        self.assertEqual(p[1], "b")

    async def test_setitem(self):
        """pairs support assignment with [0] and [1]"""
        p = pair(1, 2)
        p[0] = "x"
        p[1] = "y"
        self.assertEqual(p[0], "x")
        self.assertEqual(p[1], "y")

    async def test_getitem_out_of_range(self):
        """indexing outside [0, 1] raises IndexError"""
        p = pair(1, 2)
        with self.assertRaises(IndexError):
            _ = p[2]

    async def test_getitem_negative(self):
        """negative indexing works (Python normalizes before sq_item)"""
        p = pair("a", "b")
        self.assertEqual(p[-1], "b")
        self.assertEqual(p[-2], "a")

    async def test_setitem_out_of_range(self):
        """assignment outside [0, 1] raises IndexError"""
        p = pair(1, 2)
        with self.assertRaises(IndexError):
            p[2] = "x"

    async def test_iter(self):
        """pairs are iterable, yielding head then tail"""
        p = pair("a", "b")
        result = list(p)
        self.assertEqual(result, ["a", "b"])

    async def test_repr(self):
        """repr() returns cons(...) format"""
        p = pair(1, 2)
        self.assertEqual(repr(p), "cons(1, 2)")

    async def test_str_improper(self):
        """str() of improper pair uses dot notation"""
        p = pair(1, 2)
        self.assertEqual(str(p), "(1 . 2)")

    async def test_str_proper(self):
        """str() of proper list omits final dot"""
        p = cons(1, 2, 3, nil)
        self.assertEqual(str(p), "(1 2 3)")

    async def test_eq_same(self):
        """pairs with same structure are equal"""
        p1 = pair(1, 2)
        p2 = pair(1, 2)
        self.assertEqual(p1, p2)

    async def test_eq_different(self):
        """pairs with different structure are not equal"""
        p1 = pair(1, 2)
        p2 = pair(1, 3)
        self.assertNotEqual(p1, p2)

    async def test_eq_nested(self):
        """nested pairs with same structure are equal"""
        p1 = cons(1, 2, 3, nil)
        p2 = cons(1, 2, 3, nil)
        self.assertEqual(p1, p2)

    async def test_ne(self):
        """!= works correctly"""
        p1 = pair(1, 2)
        p2 = pair(1, 3)
        self.assertTrue(p1 != p2)
        self.assertFalse(p1 != pair(1, 2))

    async def test_requires_two_args(self):
        """pair() requires exactly two arguments"""
        with self.assertRaises(TypeError):
            pair(1)
        with self.assertRaises(TypeError):
            pair()

    async def test_no_named_args(self):
        """pair() rejects keyword arguments"""
        with self.assertRaises(TypeError):
            pair(head=1, tail=2)

    async def test_copy(self):
        """__copy__() creates a shallow copy"""
        import copy
        p = cons(1, 2, 3, nil)
        p2 = copy.copy(p)
        self.assertEqual(p, p2)
        self.assertIsNot(p, p2)

    async def test_is_proper_true(self):
        """is_proper() returns True for nil-terminated list"""
        p = cons(1, 2, 3, nil)
        self.assertTrue(p.is_proper())

    async def test_is_proper_false(self):
        """is_proper() returns False for non-nil-terminated list"""
        p = pair(1, 2)
        self.assertFalse(p.is_proper())

    async def test_is_recursive_false(self):
        """is_recursive() returns False for non-recursive list"""
        p = cons(1, 2, 3, nil)
        self.assertFalse(p.is_recursive())

    async def test_is_recursive_true(self):
        """is_recursive() returns True for recursive list"""
        p = pair(1, nil)
        setcdr(p, p)  # make it circular
        self.assertTrue(p.is_recursive())

    async def test_length(self):
        """length() returns number of pairs in list"""
        p = cons(1, 2, 3, nil)
        self.assertEqual(p.length(), 3)

    async def test_length_improper(self):
        """length() counts tail of improper list"""
        p = cons(1, 2, 3)
        self.assertEqual(p.length(), 3)

    async def test_follow(self):
        """follow() iterates over pairs themselves"""
        p = cons(1, 2, 3, nil)
        pairs = list(p.follow())
        self.assertEqual(len(pairs), 4)  # 3 pairs + nil
        self.assertIsInstance(pairs[0], pair)
        self.assertIs(pairs[3], nil)

    async def test_unpack(self):
        """unpack() iterates over car values"""
        p = cons(1, 2, 3, nil)
        values = list(p.unpack())
        self.assertEqual(values, [1, 2, 3])

    async def test_unpack_improper(self):
        """unpack() includes tail of improper list"""
        p = cons(1, 2, 3)
        values = list(p.unpack())
        self.assertEqual(values, [1, 2, 3])

    async def test_position(self):
        """get_position() and set_position() work correctly"""
        p = pair(1, 2)
        self.assertIsNone(p.get_position())
        pos = ("file.ki", 10, 5)
        p.set_position(pos)
        self.assertEqual(p.get_position(), pos)

    async def test_clear_position(self):
        """clear_position() removes position info"""
        p = pair(1, 2)
        p.set_position(("file.ki", 10, 5))
        p.clear_position()
        self.assertIsNone(p.get_position())


class TestNil(unittest.IsolatedAsyncioTestCase):
    """Tests for the nil singleton"""

    async def test_singleton(self):
        """nil is a singleton"""
        from koskari.lib import nil as nil1
        from koskari.lib import nil as nil2
        self.assertIs(nil1, nil2)

    async def test_repr(self):
        """repr(nil) returns 'nil'"""
        self.assertEqual(repr(nil), "nil")

    async def test_str(self):
        """str(nil) returns 'nil'"""
        self.assertEqual(str(nil), "nil")

    async def test_bool_false(self):
        """nil is falsy"""
        self.assertFalse(nil)
        self.assertFalse(bool(nil))

    async def test_len(self):
        """len(nil) is 0"""
        self.assertEqual(len(nil), 0)

    async def test_iter_empty(self):
        """iterating nil yields nothing"""
        result = list(nil)
        self.assertEqual(result, [])

    async def test_getitem_error(self):
        """indexing nil raises IndexError"""
        with self.assertRaises(IndexError):
            _ = nil[0]

    async def test_setitem_error(self):
        """assignment to nil raises IndexError"""
        with self.assertRaises(IndexError):
            nil[0] = 1

    async def test_car_error(self):
        """car(nil) raises TypeError"""
        with self.assertRaises(TypeError):
            car(nil)

    async def test_cdr_error(self):
        """cdr(nil) raises TypeError"""
        with self.assertRaises(TypeError):
            cdr(nil)

    async def test_setcar_error(self):
        """setcar(nil, x) raises TypeError"""
        with self.assertRaises(TypeError):
            setcar(nil, 1)

    async def test_setcdr_error(self):
        """setcdr(nil, x) raises TypeError"""
        with self.assertRaises(TypeError):
            setcdr(nil, 1)

    async def test_is_pair(self):
        """nil is an instance of pair (subtype)"""
        self.assertIsInstance(nil, pair)

    async def test_nil_type_constructor(self):
        """NilType() returns nil singleton"""
        nil_type = type(nil)
        self.assertIs(nil_type(), nil)

    async def test_nil_type_rejects_args(self):
        """NilType() rejects arguments"""
        nil_type = type(nil)
        with self.assertRaises(TypeError):
            nil_type(1)


class TestCons(unittest.IsolatedAsyncioTestCase):
    """Tests for the cons() function"""

    async def test_empty(self):
        """cons() with no arguments returns nil"""
        self.assertIs(cons(), nil)

    async def test_single(self):
        """cons(x) creates pair with nil tail"""
        p = cons(1)
        self.assertEqual(car(p), 1)
        self.assertIs(cdr(p), nil)

    async def test_two(self):
        """cons(a, b) creates improper pair"""
        p = cons(1, 2)
        self.assertEqual(car(p), 1)
        self.assertEqual(cdr(p), 2)

    async def test_multiple(self):
        """cons(a, b, c, ...) creates linked list"""
        p = cons(1, 2, 3, 4)
        self.assertEqual(car(p), 1)
        self.assertEqual(car(cdr(p)), 2)
        self.assertEqual(car(cdr(cdr(p))), 3)
        self.assertEqual(cdr(cdr(cdr(p))), 4)

    async def test_proper_list(self):
        """cons(a, b, c, nil) creates proper list"""
        p = cons(1, 2, 3, nil)
        self.assertTrue(p.is_proper())
        self.assertEqual(list(p.unpack()), [1, 2, 3])

    async def test_recursive(self):
        """make_recursive(cons(...)) creates circular list"""
        p = cons(1, 2, 3, nil)
        make_recursive(p)
        self.assertTrue(p.is_recursive())
        # Follow should eventually loop back
        seen = set()
        current = p
        for _ in range(10):
            if id(current) in seen:
                break
            seen.add(id(current))
            current = cdr(current)
        self.assertIn(id(current), seen)
        break_recursive(p)
        self.assertFalse(p.is_recursive())


class TestBuildUnpackPair(unittest.IsolatedAsyncioTestCase):
    """Tests for the build_unpack_pair() function"""

    async def test_empty(self):
        """build_unpack_pair() with no args returns nil"""
        self.assertIs(build_unpack_pair(), nil)

    async def test_single_list(self):
        """build_unpack_pair([1, 2, 3]) creates improper list (non-pair tail)"""
        p = build_unpack_pair([1, 2, 3])
        # Python lists are not pairs, so result is improper
        self.assertFalse(p.is_proper())
        self.assertEqual(list(p.unpack()), [1, 2, 3])

    async def test_single_pair(self):
        """build_unpack_pair(proper_pair) preserves proper termination"""
        proper = cons(1, 2, 3, nil)
        p = build_unpack_pair(proper)
        self.assertTrue(p.is_proper())
        self.assertEqual(list(p.unpack()), [1, 2, 3])

    async def test_multiple_lists(self):
        """build_unpack_pair concatenates multiple sequences"""
        p = build_unpack_pair([1, 2], [3, 4])
        self.assertEqual(list(p.unpack()), [1, 2, 3, 4])

    async def test_with_pairs(self):
        """build_unpack_pair works with pair arguments"""
        p1 = cons(1, 2, nil)
        p2 = cons(3, 4, nil)
        result = build_unpack_pair(p1, p2)
        self.assertEqual(list(result.unpack()), [1, 2, 3, 4])

    async def test_skips_nil(self):
        """build_unpack_pair skips nil arguments"""
        p = build_unpack_pair([1, 2], nil, [3, 4])
        self.assertEqual(list(p.unpack()), [1, 2, 3, 4])

    async def test_preserves_improper_tail(self):
        """build_unpack_pair preserves improper tail from last arg"""
        improper = cons(3, 4, 5)  # improper: ends with 5, not nil
        p = build_unpack_pair([1, 2], improper)
        self.assertFalse(p.is_proper())


class TestCarCdrErrors(unittest.IsolatedAsyncioTestCase):
    """Tests for error handling in car/cdr functions"""

    async def test_car_non_pair(self):
        """car() on non-pair raises TypeError"""
        with self.assertRaises(TypeError):
            car(42)
        with self.assertRaises(TypeError):
            car("hello")
        with self.assertRaises(TypeError):
            car([1, 2, 3])

    async def test_cdr_non_pair(self):
        """cdr() on non-pair raises TypeError"""
        with self.assertRaises(TypeError):
            cdr(42)
        with self.assertRaises(TypeError):
            cdr("hello")


class TestPairRecursion(unittest.IsolatedAsyncioTestCase):
    """Tests for handling recursive/circular structures"""

    async def test_repr_recursive(self):
        """repr() handles recursive pairs"""
        p = pair(1, nil)
        setcdr(p, p)
        r = repr(p)
        # Should contain recursive indicator, not infinite loop
        self.assertIn("recursive", r.lower())

    async def test_str_recursive(self):
        """str() handles recursive pairs"""
        p = pair(1, nil)
        setcdr(p, p)
        s = str(p)
        # Should contain ellipsis or indicator, not infinite loop
        self.assertIn("...", s)

    async def test_eq_recursive(self):
        """equality check handles recursive pairs"""
        p1 = pair(1, nil)
        setcdr(p1, p1)
        p2 = pair(1, nil)
        setcdr(p2, p2)
        # Should complete without infinite recursion
        self.assertEqual(p1, p2)

    async def test_copy_recursive(self):
        """copy() handles recursive pairs"""
        import copy
        p = pair(1, nil)
        setcdr(p, p)
        p2 = copy.copy(p)
        self.assertTrue(p2.is_recursive())

    async def test_length_recursive(self):
        """length() handles recursive pairs"""
        p = pair(1, pair(2, nil))
        setcdr(cdr(p), p)
        # Should return finite length, not infinite loop
        length = p.length()
        self.assertEqual(length, 2)


class TestPairAppendExtendSplice(unittest.IsolatedAsyncioTestCase):
    """Tests for append, extend, splice methods"""

    async def test_append_proper(self):
        """append on proper list adds element before nil"""
        p = cons("foo", "bar", nil)
        result = p.append("baz")
        self.assertIsNone(result)
        self.assertEqual(list(p.unpack()), ["foo", "bar", "baz"])
        self.assertTrue(p.is_proper())

    async def test_append_improper(self):
        """append on improper list inserts before tail"""
        p = cons("foo", "bar")
        result = p.append("baz")
        self.assertIsNone(result)
        self.assertEqual(list(p.unpack()), ["foo", "bar", "baz"])
        self.assertFalse(p.is_proper())
        self.assertEqual(cdr(cdr(p)), "baz")

    async def test_append_nil_error(self):
        """append on nil raises TypeError"""
        with self.assertRaises(TypeError):
            nil.append(1)

    async def test_append_recursive_error(self):
        """append on recursive pair raises ValueError"""
        p = pair(1, nil)
        setcdr(p, p)
        with self.assertRaises(ValueError):
            p.append(2)

    async def test_extend_with_list(self):
        """extend with list concatenates elements"""
        p = cons("foo", "bar", nil)
        p.extend([1, 2, 3])
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertTrue(p.is_proper())

    async def test_extend_improper_with_list(self):
        """extend improper list with list inserts before tail"""
        p = cons("foo", "bar")
        p.extend([1, 2, 3])
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertTrue(p.is_proper())

    async def test_extend_with_tuple(self):
        """extend with tuple works like list"""
        p = cons(1, 2, nil)
        p.extend((3, 4))
        self.assertEqual(list(p.unpack()), [1, 2, 3, 4])

    async def test_extend_with_iterator(self):
        """extend with iterator consumes and appends"""
        p = cons(1, nil)
        p.extend(iter([2, 3]))
        self.assertEqual(list(p.unpack()), [1, 2, 3])

    async def test_extend_with_proper_cons(self):
        """extend with proper cons copies and splices"""
        p = cons("foo", "bar", nil)
        other = cons(1, 2, 3, nil)
        p.extend(other)
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertTrue(p.is_proper())
        self.assertIsNot(cdr(cdr(p)), other)

    async def test_extend_with_improper_cons(self):
        """extend with improper cons preserves improper tail"""
        p = cons("foo", "bar", nil)
        other = cons(1, 2, 3)
        p.extend(other)
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertFalse(p.is_proper())
        self.assertEqual(cdr(cdr(cdr(cdr(p)))), 3)

    async def test_extend_improper_with_proper_cons(self):
        """extend improper with proper cons yields proper"""
        p = cons("foo", "bar")
        other = cons(1, 2, 3, nil)
        p.extend(other)
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertTrue(p.is_proper())

    async def test_extend_nil_error(self):
        """extend on nil raises TypeError"""
        with self.assertRaises(TypeError):
            nil.extend([1])

    async def test_extend_recursive_error(self):
        """extend on recursive pair raises ValueError"""
        p = pair(1, nil)
        setcdr(p, p)
        with self.assertRaises(ValueError):
            p.extend([2])

    async def test_splice_with_cons_in_place(self):
        """splice with cons reuses argument in place"""
        p = cons("foo", "bar", nil)
        other = cons(1, 2, 3, nil)
        p.splice(other)
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])
        self.assertIs(cdr(cdr(p)), other)

    async def test_splice_with_list(self):
        """splice with list builds new cons like extend"""
        p = cons("foo", "bar", nil)
        p.splice([1, 2, 3])
        self.assertEqual(list(p.unpack()), ["foo", "bar", 1, 2, 3])

    async def test_splice_nil_error(self):
        """splice on nil raises TypeError"""
        with self.assertRaises(TypeError):
            nil.splice(cons(1, nil))

    async def test_splice_recursive_error(self):
        """splice on recursive pair raises ValueError"""
        p = pair(1, nil)
        setcdr(p, p)
        with self.assertRaises(ValueError):
            p.splice(cons(2, nil))


class TestPairNilDistinct(unittest.IsolatedAsyncioTestCase):
    """Tests that pair and nil have proper type relationship"""

    async def test_nil_is_pair_subtype(self):
        """NilType is a subtype of pair"""
        self.assertTrue(isinstance(nil, pair))

    async def test_pair_is_not_nil(self):
        """regular pairs are not nil"""
        p = pair(1, 2)
        self.assertIsNot(p, nil)
        self.assertNotEqual(type(p), type(nil))

    async def test_nil_type_different(self):
        """nil has its own type"""
        nil_type = type(nil)
        pair_type = type(pair(1, 2))
        self.assertIsNot(nil_type, pair_type)
        self.assertEqual(nil_type.__name__, "NilType")
        self.assertEqual(pair_type.__name__, "pair")


# The end.
