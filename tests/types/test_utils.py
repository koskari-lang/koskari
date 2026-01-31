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
:ai-assistant: Claude Opus 4 via Cursor
"""


import unittest

from koskari import lib


class TestReapply(unittest.TestCase):
    """Tests for the reapply function"""

    def test_reapply_zero_times(self):
        """reapply with count 0 returns original data"""

        result = lib.reapply(lambda x: x + 1, 5, 0)
        self.assertEqual(result, 5)

    def test_reapply_once(self):
        """reapply with count 1 applies function once"""

        result = lib.reapply(lambda x: x * 2, 5, 1)
        self.assertEqual(result, 10)

    def test_reapply_multiple(self):
        """reapply applies function count times"""

        result = lib.reapply(lambda x: x + 1, 0, 5)
        self.assertEqual(result, 5)

    def test_reapply_with_list(self):
        """reapply works with list transformations"""

        result = lib.reapply(lambda x: x + [len(x)], [], 4)
        self.assertEqual(result, [0, 1, 2, 3])

    def test_reapply_exception_propagates(self):
        """reapply propagates exceptions from function"""

        def fail_on_3(x):
            if x >= 3:
                raise ValueError("hit 3")
            return x + 1

        with self.assertRaises(ValueError):
            lib.reapply(fail_on_3, 0, 5)


class TestApply(unittest.TestCase):
    """Tests for the apply function"""

    def test_apply_no_args(self):
        """apply with no args calls function"""

        result = lib.apply(list)
        self.assertEqual(result, [])

    def test_apply_tuple_args(self):
        """apply with tuple args unpacks them"""

        result = lib.apply(max, (1, 5, 3))
        self.assertEqual(result, 5)

    def test_apply_list_args(self):
        """apply with list args unpacks them"""

        result = lib.apply(sum, [[1, 2, 3]])
        self.assertEqual(result, 6)

    def test_apply_with_kwargs(self):
        """apply with kwargs passes them"""

        def func(*args, **kwargs):
            return (args, kwargs)

        result = lib.apply(func, (1, 2), {'x': 10})
        self.assertEqual(result, ((1, 2), {'x': 10}))

    def test_apply_pair_args(self):
        """apply with pair args unpacks them"""

        pair = lib.cons(1, lib.cons(2, lib.cons(3, lib.nil)))
        result = lib.apply(max, pair)
        self.assertEqual(result, 3)

    def test_apply_none_args(self):
        """apply with None args treats as empty"""

        result = lib.apply(list, None)
        self.assertEqual(result, [])

    def test_apply_none_kwargs(self):
        """apply with None kwargs ignores them"""

        result = lib.apply(max, (1, 2, 3), None)
        self.assertEqual(result, 3)


class TestIdentity(unittest.TestCase):
    """Tests for the identity function"""

    def test_identity_number(self):
        """identity returns number unchanged"""

        result = lib.identity(42)
        self.assertEqual(result, 42)

    def test_identity_string(self):
        """identity returns string unchanged"""

        result = lib.identity("hello")
        self.assertEqual(result, "hello")

    def test_identity_list(self):
        """identity returns same list object"""

        original = [1, 2, 3]
        result = lib.identity(original)
        self.assertIs(result, original)

    def test_identity_none(self):
        """identity returns None unchanged"""

        result = lib.identity(None)
        self.assertIsNone(result)


class TestConstantly(unittest.TestCase):
    """Tests for the constantly function"""

    def test_constantly_returns_callable(self):
        """constantly returns a callable"""

        f = lib.constantly(42)
        self.assertTrue(callable(f))

    def test_constantly_returns_value(self):
        """constantly callable returns its value"""

        f = lib.constantly(42)
        self.assertEqual(f(), 42)

    def test_constantly_ignores_args(self):
        """constantly callable ignores arguments"""

        f = lib.constantly("fixed")
        self.assertEqual(f(1, 2, 3), "fixed")
        self.assertEqual(f(x=10, y=20), "fixed")
        self.assertEqual(f(1, 2, x=3), "fixed")

    def test_constantly_same_value_each_time(self):
        """constantly callable returns same value on multiple calls"""

        obj = object()
        f = lib.constantly(obj)
        self.assertIs(f(), obj)
        self.assertIs(f(), obj)
        self.assertIs(f(), obj)

    def test_constantly_repr(self):
        """constantly has meaningful repr"""

        f = lib.constantly(42)
        r = repr(f)
        self.assertIn("constantly", r)
        self.assertIn("42", r)


class TestPass(unittest.TestCase):
    """Tests for the _pass function"""

    def test_pass_no_args(self):
        """_pass with no args returns None"""

        result = lib._pass()
        self.assertIsNone(result)

    def test_pass_with_args(self):
        """_pass with args returns None"""

        result = lib._pass(1, 2, 3)
        self.assertIsNone(result)

    def test_pass_with_kwargs(self):
        """_pass with kwargs returns None"""

        result = lib._pass(x=1, y=2)
        self.assertIsNone(result)

    def test_pass_with_both(self):
        """_pass with args and kwargs returns None"""

        result = lib._pass(1, 2, x=3, y=4)
        self.assertIsNone(result)


class TestBuildTuple(unittest.TestCase):
    """Tests for the build_tuple function"""

    def test_build_tuple_empty(self):
        """build_tuple with no args returns empty tuple"""

        result = lib.build_tuple()
        self.assertEqual(result, ())
        self.assertIsInstance(result, tuple)

    def test_build_tuple_single(self):
        """build_tuple with one arg returns single-element tuple"""

        result = lib.build_tuple(42)
        self.assertEqual(result, (42,))

    def test_build_tuple_multiple(self):
        """build_tuple with multiple args returns tuple"""

        result = lib.build_tuple(1, 2, 3)
        self.assertEqual(result, (1, 2, 3))

    def test_build_tuple_mixed_types(self):
        """build_tuple handles mixed types"""

        result = lib.build_tuple(1, "two", [3], None)
        self.assertEqual(result, (1, "two", [3], None))


class TestBuildList(unittest.TestCase):
    """Tests for the build_list function"""

    def test_build_list_empty(self):
        """build_list with no args returns empty list"""

        result = lib.build_list()
        self.assertEqual(result, [])
        self.assertIsInstance(result, list)

    def test_build_list_single(self):
        """build_list with one arg returns single-element list"""

        result = lib.build_list(42)
        self.assertEqual(result, [42])

    def test_build_list_multiple(self):
        """build_list with multiple args returns list"""

        result = lib.build_list(1, 2, 3)
        self.assertEqual(result, [1, 2, 3])


class TestBuildSet(unittest.TestCase):
    """Tests for the build_set function"""

    def test_build_set_empty(self):
        """build_set with no args returns empty set"""

        result = lib.build_set()
        self.assertEqual(result, set())
        self.assertIsInstance(result, set)

    def test_build_set_single(self):
        """build_set with one arg returns single-element set"""

        result = lib.build_set(42)
        self.assertEqual(result, {42})

    def test_build_set_multiple(self):
        """build_set with multiple args returns set"""

        result = lib.build_set(1, 2, 3)
        self.assertEqual(result, {1, 2, 3})

    def test_build_set_deduplicates(self):
        """build_set removes duplicates"""

        result = lib.build_set(1, 2, 1, 3, 2)
        self.assertEqual(result, {1, 2, 3})


class TestBuildDict(unittest.TestCase):
    """Tests for the build_dict function"""

    def test_build_dict_empty(self):
        """build_dict with no args returns empty dict"""

        result = lib.build_dict()
        self.assertEqual(result, {})
        self.assertIsInstance(result, dict)

    def test_build_dict_from_tuples(self):
        """build_dict from tuple pairs"""

        result = lib.build_dict(('a', 1), ('b', 2))
        self.assertEqual(result, {'a': 1, 'b': 2})

    def test_build_dict_from_lists(self):
        """build_dict from list pairs"""

        result = lib.build_dict(['a', 1], ['b', 2])
        self.assertEqual(result, {'a': 1, 'b': 2})

    def test_build_dict_with_kwargs(self):
        """build_dict merges keyword arguments"""

        result = lib.build_dict(('a', 1), b=2, c=3)
        self.assertEqual(result, {'a': 1, 'b': 2, 'c': 3})

    def test_build_dict_kwargs_override(self):
        """build_dict kwargs override positional pairs"""

        result = lib.build_dict(('a', 1), a=10)
        self.assertEqual(result, {'a': 10})

    def test_build_dict_from_pairs(self):
        """build_dict from cons pairs"""

        pair1 = lib.cons('x', lib.cons(100, lib.nil))
        pair2 = lib.cons('y', lib.cons(200, lib.nil))
        result = lib.build_dict(pair1, pair2)
        self.assertEqual(result, {'x': 100, 'y': 200})

    def test_build_dict_only_kwargs(self):
        """build_dict with only kwargs"""

        result = lib.build_dict(x=1, y=2, z=3)
        self.assertEqual(result, {'x': 1, 'y': 2, 'z': 3})


# The end.
