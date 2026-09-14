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
Unit tests for utility functions

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

import koskari.lib as lib
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class TestReapply(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the reapply function
    """

    async def test_reapply_zero_times(self):
        """
        reapply with count 0 returns original data
        """

        result = lib.reapply(lambda x: x + 1, 5, 0)
        self.assertEqual(result, 5)


    async def test_reapply_once(self):
        """
        reapply with count 1 applies function once
        """

        result = lib.reapply(lambda x: x * 2, 5, 1)
        self.assertEqual(result, 10)


    async def test_reapply_multiple(self):
        """
        reapply applies function count times
        """

        result = lib.reapply(lambda x: x + 1, 0, 5)
        self.assertEqual(result, 5)


    async def test_reapply_with_list(self):
        """
        reapply works with list transformations
        """

        result = lib.reapply(lambda x: x + [len(x)], [], 4)
        self.assertEqual(result, [0, 1, 2, 3])


    async def test_reapply_exception_propagates(self):
        """
        reapply propagates exceptions from function
        """

        def fail_on_3(x):
            if x >= 3:
                raise ValueError("hit 3")
            return x + 1

        with self.assertRaises(ValueError):
            lib.reapply(fail_on_3, 0, 5)


class TestApply(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the apply function
    """

    async def test_apply_no_args(self):
        """
        apply with no args calls function
        """

        result = lib.apply(list)
        self.assertEqual(result, [])


    async def test_apply_tuple_args(self):
        """
        apply with tuple args unpacks them
        """

        result = lib.apply(max, (1, 5, 3))
        self.assertEqual(result, 5)


    async def test_apply_list_args(self):
        """
        apply with list args unpacks them
        """

        result = lib.apply(sum, [[1, 2, 3]])
        self.assertEqual(result, 6)


    async def test_apply_with_kwargs(self):
        """
        apply with kwargs passes them
        """

        def func(*args, **kwargs):
            return (args, kwargs)

        result = lib.apply(func, (1, 2), {'x': 10})
        self.assertEqual(result, ((1, 2), {'x': 10}))


    async def test_apply_pair_args(self):
        """
        apply with pair args unpacks them
        """

        pair = lib.cons(1, 2, 3, lib.nil)
        result = lib.apply(max, pair)
        self.assertEqual(result, 3)


    async def test_apply_none_args(self):
        """
        apply with None args treats as empty
        """

        result = lib.apply(list, None)
        self.assertEqual(result, [])


    async def test_apply_none_kwargs(self):
        """
        apply with None kwargs ignores them
        """

        result = lib.apply(max, (1, 2, 3), None)
        self.assertEqual(result, 3)


    async def test_apply_is_callable_object(self):
        """
        apply is exposed as a callable helper object
        """

        self.assertTrue(callable(lib.apply))


    async def test_apply_python_to_koskari_function(self):
        """
        python caller can apply a Koskari function
        """

        code, ns = build_expr([["CONST", 77], ["RETURN", 1]])
        fn = make_callable(code, ns)
        result = await lib.apply(fn)
        self.assertEqual(result, 77)


class TestIdentity(unittest.TestCase):
    """
    Tests for the identity function
    """

    def test_identity_number(self):
        """
        identity returns number unchanged
        """

        result = lib.identity(42)
        self.assertEqual(result, 42)


    def test_identity_string(self):
        """
        identity returns string unchanged
        """

        result = lib.identity("hello")
        self.assertEqual(result, "hello")


    def test_identity_list(self):
        """
        identity returns same list object
        """

        original = [1, 2, 3]
        result = lib.identity(original)
        self.assertIs(result, original)


    def test_identity_none(self):
        """
        identity returns None unchanged
        """

        result = lib.identity(None)
        self.assertIsNone(result)


class TestConstantly(unittest.TestCase):
    """
    Tests for the constantly type
    """

    def test_constantly_is_type(self):
        """
        constantly is the type object itself
        """

        self.assertIsInstance(lib.constantly, type)
        self.assertEqual(lib.constantly.__name__, "constantly")


    def test_constantly_returns_callable(self):
        """
        constantly returns a callable
        """

        f = lib.constantly(42)
        self.assertTrue(callable(f))
        self.assertIsInstance(f, lib.constantly)


    def test_constantly_returns_value(self):
        """
        constantly callable returns its value
        """

        f = lib.constantly(42)
        self.assertEqual(f(), 42)


    def test_constantly_ignores_args(self):
        """
        constantly callable ignores arguments
        """

        f = lib.constantly("fixed")
        self.assertEqual(f(1, 2, 3), "fixed")
        self.assertEqual(f(x=10, y=20), "fixed")
        self.assertEqual(f(1, 2, x=3), "fixed")


    def test_constantly_same_value_each_time(self):
        """
        constantly callable returns same value on multiple calls
        """

        obj = object()
        f = lib.constantly(obj)
        self.assertIs(f(), obj)
        self.assertIs(f(), obj)
        self.assertIs(f(), obj)


    def test_constantly_repr(self):
        """
        constantly has meaningful repr
        """

        f = lib.constantly(42)
        r = repr(f)
        self.assertIn("constantly", r)
        self.assertIn("42", r)


    def test_constantly_arity(self):
        """
        constantly requires exactly one positional argument
        """

        with self.assertRaises(TypeError):
            lib.constantly()

        with self.assertRaises(TypeError):
            lib.constantly(1, 2)

        with self.assertRaises(TypeError):
            lib.constantly(value=1)


class TestPass(unittest.TestCase):
    """
    Tests for the _pass function
    """

    def test_pass_no_args(self):
        """
        _pass with no args returns None
        """

        result = lib._pass()
        self.assertIsNone(result)


    def test_pass_with_args(self):
        """
        _pass with args returns None
        """

        result = lib._pass(1, 2, 3)
        self.assertIsNone(result)


    def test_pass_with_kwargs(self):
        """
        _pass with kwargs returns None
        """

        result = lib._pass(x=1, y=2)
        self.assertIsNone(result)


    def test_pass_with_both(self):
        """
        _pass with args and kwargs returns None
        """

        result = lib._pass(1, 2, x=3, y=4)
        self.assertIsNone(result)


class TestBuildTuple(unittest.TestCase):
    """
    Tests for the build_tuple function
    """

    def test_build_tuple_empty(self):
        """
        build_tuple with no args returns empty tuple
        """

        result = lib.build_tuple()
        self.assertEqual(result, ())
        self.assertIsInstance(result, tuple)


    def test_build_tuple_single(self):
        """
        build_tuple with one arg returns single-element tuple
        """

        result = lib.build_tuple(42)
        self.assertEqual(result, (42,))


    def test_build_tuple_multiple(self):
        """
        build_tuple with multiple args returns tuple
        """

        result = lib.build_tuple(1, 2, 3)
        self.assertEqual(result, (1, 2, 3))


    def test_build_tuple_mixed_types(self):
        """
        build_tuple handles mixed types
        """

        result = lib.build_tuple(1, "two", [3], None)
        self.assertEqual(result, (1, "two", [3], None))


class TestBuildList(unittest.TestCase):
    """
    Tests for the build_list function
    """

    def test_build_list_empty(self):
        """
        build_list with no args returns empty list
        """

        result = lib.build_list()
        self.assertEqual(result, [])
        self.assertIsInstance(result, list)


    def test_build_list_single(self):
        """
        build_list with one arg returns single-element list
        """

        result = lib.build_list(42)
        self.assertEqual(result, [42])


    def test_build_list_multiple(self):
        """
        build_list with multiple args returns list
        """

        result = lib.build_list(1, 2, 3)
        self.assertEqual(result, [1, 2, 3])


class TestBuildSet(unittest.TestCase):
    """
    Tests for the build_set function
    """

    def test_build_set_empty(self):
        """
        build_set with no args returns empty set
        """

        result = lib.build_set()
        self.assertEqual(result, set())
        self.assertIsInstance(result, set)


    def test_build_set_single(self):
        """
        build_set with one arg returns single-element set
        """

        result = lib.build_set(42)
        self.assertEqual(result, {42})


    def test_build_set_multiple(self):
        """
        build_set with multiple args returns set
        """

        result = lib.build_set(1, 2, 3)
        self.assertEqual(result, {1, 2, 3})


    def test_build_set_deduplicates(self):
        """
        build_set removes duplicates
        """

        result = lib.build_set(1, 2, 1, 3, 2)
        self.assertEqual(result, {1, 2, 3})


class TestBuildDict(unittest.TestCase):
    """
    Tests for the build_dict function
    """

    def test_build_dict_empty(self):
        """
        build_dict with no args returns empty dict
        """

        result = lib.build_dict()
        self.assertEqual(result, {})
        self.assertIsInstance(result, dict)


    def test_build_dict_from_tuples(self):
        """
        build_dict from tuple pairs
        """

        result = lib.build_dict(('a', 1), ('b', 2))
        self.assertEqual(result, {'a': 1, 'b': 2})


    def test_build_dict_from_lists(self):
        """
        build_dict from list pairs
        """

        result = lib.build_dict(['a', 1], ['b', 2])
        self.assertEqual(result, {'a': 1, 'b': 2})


    def test_build_dict_with_kwargs(self):
        """
        build_dict merges keyword arguments
        """

        result = lib.build_dict(('a', 1), b=2, c=3)
        self.assertEqual(result, {'a': 1, 'b': 2, 'c': 3})


    def test_build_dict_kwargs_override(self):
        """
        build_dict kwargs override positional pairs
        """

        result = lib.build_dict(('a', 1), a=10)
        self.assertEqual(result, {'a': 10})


    def test_build_dict_from_pairs(self):
        """
        build_dict from cons pairs
        """

        pair1 = lib.cons('x', 100, lib.nil)
        pair2 = lib.cons('y', 200, lib.nil)
        result = lib.build_dict(pair1, pair2)
        self.assertEqual(result, {'x': 100, 'y': 200})


    def test_build_dict_only_kwargs(self):
        """
        build_dict with only kwargs
        """

        result = lib.build_dict(x=1, y=2, z=3)
        self.assertEqual(result, {'x': 1, 'y': 2, 'z': 3})


    def test_build_dict_nil_item(self):
        """
        build_dict rejects nil as an item
        """

        with self.assertRaises(ValueError):
            lib.build_dict(lib.nil)


    def test_build_dict_one_element_pair(self):
        """
        build_dict rejects a pair carrying only a key
        """

        with self.assertRaises(ValueError):
            lib.build_dict(lib.cons('x', lib.nil))


class TestGetAttrStar(unittest.TestCase):
    """
    Tests for the getattr_star runtime builtin (surface: getattr*)
    """

    def test_single_step_success(self):
        """
        getattr_star returns the attribute when present
        """

        class Box(object):
            x = 1

        self.assertEqual(lib.getattr_star(Box(), ['x']), 1)


    def test_chain_success(self):
        """
        getattr_star walks nested attributes left to right
        """

        class Inner(object):
            y = 2

        class Outer(object):
            inner = Inner()

        obj = Outer()
        self.assertEqual(lib.getattr_star(obj, ['inner', 'y']), 2)


    def test_empty_attrlist_raises(self):
        """
        getattr_star with an empty attrlist raises ValueError
        """

        with self.assertRaises(ValueError):
            lib.getattr_star(object(), [])


    def test_missing_leaf_returns_default(self):
        """
        getattr_star returns default on AttributeError at the leaf
        """

        class Box(object):
            pass

        self.assertEqual(lib.getattr_star(Box(), ['missing'], 99), 99)


    def test_missing_intermediate_returns_default(self):
        """
        getattr_star returns default when an intermediate link is missing
        """

        class Box(object):
            pass

        self.assertEqual(
            lib.getattr_star(Box(), ['a', 'b'], 'default'),
            'default')


    def test_missing_without_default_raises(self):
        """
        getattr_star propagates AttributeError when default is omitted
        """

        class Box(object):
            pass

        with self.assertRaises(AttributeError):
            lib.getattr_star(Box(), ['missing'])


    def test_requires_attrlist(self):
        """
        getattr_star requires element and attrlist
        """

        with self.assertRaises(TypeError):
            lib.getattr_star(object())


    def test_non_attribute_error_propagates(self):
        """
        getattr_star propagates exceptions other than AttributeError
        """

        class Bad(object):

            @property
            def boom(self):
                raise ValueError('nope')

        with self.assertRaises(ValueError):
            lib.getattr_star(Bad(), ['boom'], None)


class TestHasAttrStar(unittest.TestCase):
    """
    Tests for the hasattr_star runtime builtin (surface: hasattr*)
    """

    def test_single_step_true(self):
        """
        hasattr_star is true when the attribute is present
        """

        class Box(object):
            x = 1

        self.assertTrue(lib.hasattr_star(Box(), ['x']))


    def test_chain_true(self):
        """
        hasattr_star walks nested attributes left to right
        """

        class Inner(object):
            y = 2

        class Outer(object):
            inner = Inner()

        self.assertTrue(lib.hasattr_star(Outer(), ['inner', 'y']))


    def test_empty_attrlist_raises(self):
        """
        hasattr_star with an empty attrlist raises ValueError
        """

        with self.assertRaises(ValueError):
            lib.hasattr_star(object(), [])


    def test_missing_leaf_false(self):
        """
        hasattr_star is false when the leaf attribute is missing
        """

        self.assertFalse(lib.hasattr_star(object(), ['nope']))


    def test_missing_intermediate_false(self):
        """
        hasattr_star is false when an intermediate link is missing
        """

        class Box(object):
            pass

        self.assertFalse(lib.hasattr_star(Box(), ['a', 'b']))


    def test_requires_attrlist(self):
        """
        hasattr_star requires element and attrlist
        """

        with self.assertRaises(TypeError):
            lib.hasattr_star(object())


    def test_lookup_exception_false(self):
        """
        hasattr_star is false when lookup raises (Python hasattr semantics)
        """

        class Bad(object):

            @property
            def boom(self):
                raise ValueError('nope')

        self.assertFalse(lib.hasattr_star(Bad(), ['boom']))


class TestGetItemStar(unittest.TestCase):
    """
    Tests for the getitem_star runtime builtin (surface: getitem*)
    """

    def test_single_step_success(self):
        """
        getitem_star returns the item when present
        """

        self.assertEqual(lib.getitem_star({'x': 1}, ['x']), 1)


    def test_chain_dict_success(self):
        """
        getitem_star walks nested mappings left to right
        """

        obj = {'a': {'b': 2}}
        self.assertEqual(lib.getitem_star(obj, ['a', 'b']), 2)


    def test_chain_list_success(self):
        """
        getitem_star walks nested sequences left to right
        """

        self.assertEqual(lib.getitem_star([[1, 2]], [0, 1]), 2)


    def test_empty_keylist_raises(self):
        """
        getitem_star with an empty keylist raises ValueError
        """

        with self.assertRaises(ValueError):
            lib.getitem_star({}, [])


    def test_missing_leaf_returns_default(self):
        """
        getitem_star returns default on LookupError at the leaf
        """

        self.assertEqual(lib.getitem_star({}, ['missing'], 99), 99)


    def test_missing_intermediate_returns_default(self):
        """
        getitem_star returns default when an intermediate link is missing
        """

        self.assertEqual(
            lib.getitem_star({}, ['a', 'b'], 'default'),
            'default')


    def test_missing_without_default_raises_key_error(self):
        """
        getitem_star propagates KeyError when default is omitted
        """

        with self.assertRaises(KeyError):
            lib.getitem_star({}, ['missing'])


    def test_missing_index_without_default_raises(self):
        """
        getitem_star propagates IndexError when default is omitted
        """

        with self.assertRaises(IndexError):
            lib.getitem_star([[]], [0, 99])


    def test_requires_keylist(self):
        """
        getitem_star requires element and keylist
        """

        with self.assertRaises(TypeError):
            lib.getitem_star({})


    def test_non_lookup_error_propagates(self):
        """
        getitem_star propagates exceptions other than LookupError
        """

        class Bad(dict):

            def __getitem__(self, key):
                raise ValueError('nope')

        with self.assertRaises(ValueError):
            lib.getitem_star(Bad(), ['boom'], None)


class TestHasItemStar(unittest.TestCase):
    """
    Tests for the hasitem_star runtime builtin (surface: hasitem*)
    """

    def test_single_step_true(self):
        """
        hasitem_star is true when the item is present
        """

        self.assertTrue(lib.hasitem_star({'x': 1}, ['x']))


    def test_chain_true(self):
        """
        hasitem_star walks nested items left to right
        """

        self.assertTrue(lib.hasitem_star({'a': {'b': 2}}, ['a', 'b']))


    def test_empty_keylist_raises(self):
        """
        hasitem_star with an empty keylist raises ValueError
        """

        with self.assertRaises(ValueError):
            lib.hasitem_star({}, [])


    def test_missing_leaf_false(self):
        """
        hasitem_star is false when the leaf item is missing
        """

        self.assertFalse(lib.hasitem_star({}, ['nope']))


    def test_missing_intermediate_false(self):
        """
        hasitem_star is false when an intermediate link is missing
        """

        self.assertFalse(lib.hasitem_star({}, ['a', 'b']))


    def test_requires_keylist(self):
        """
        hasitem_star requires element and keylist
        """

        with self.assertRaises(TypeError):
            lib.hasitem_star({})


    def test_lookup_exception_false(self):
        """
        hasitem_star is false when lookup raises
        """

        class Bad(dict):

            def __getitem__(self, key):
                raise ValueError('nope')

        self.assertFalse(lib.hasitem_star(Bad(), ['boom']))


# The end.
