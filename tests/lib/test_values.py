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
Unit tests for values type

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import values


pytestmark = pytest.mark.phase_lib


class TestValuesCreate(unittest.IsolatedAsyncioTestCase):
    """Tests for values creation"""

    async def test_create_empty(self):
        """values can be created with no arguments"""

        v = values()
        self.assertIsInstance(v, values)

    async def test_create_positionals(self):
        """values can be created with positional arguments"""

        v = values(1, 2, 3)
        self.assertIsInstance(v, values)
        self.assertEqual(len(v), 3)

    async def test_create_keywords(self):
        """values can be created with keyword arguments"""

        v = values(x=1, y=2)
        self.assertIsInstance(v, values)
        self.assertEqual(v['x'], 1)
        self.assertEqual(v['y'], 2)

    async def test_create_both(self):
        """values can be created with both positionals and keywords"""

        v = values(1, 2, x=3, y=4)
        self.assertIsInstance(v, values)
        self.assertEqual(len(v), 2)
        self.assertEqual(v['x'], 3)


class TestValuesBool(unittest.IsolatedAsyncioTestCase):
    """Tests for values bool conversion"""

    async def test_empty_is_falsy(self):
        """empty values is falsy"""

        v = values()
        self.assertFalse(v)
        self.assertFalse(bool(v))

    async def test_positionals_is_truthy(self):
        """values with positionals is truthy"""

        v = values(1)
        self.assertTrue(v)
        self.assertTrue(bool(v))

    async def test_keywords_is_truthy(self):
        """values with keywords only is truthy"""

        v = values(x=1)
        self.assertTrue(v)


class TestValuesSequence(unittest.IsolatedAsyncioTestCase):
    """Tests for values sequence protocol"""

    async def test_len_positionals(self):
        """len() returns number of positional arguments"""

        v = values(1, 2, 3)
        self.assertEqual(len(v), 3)

    async def test_len_no_keywords(self):
        """len() does not count keywords"""

        v = values(1, 2, x=3, y=4)
        self.assertEqual(len(v), 2)

    async def test_getitem_index(self):
        """indexing by int returns positional"""

        v = values(10, 20, 30)
        self.assertEqual(v[0], 10)
        self.assertEqual(v[1], 20)
        self.assertEqual(v[2], 30)

    async def test_getitem_negative_index(self):
        """negative indexing works"""

        v = values(10, 20, 30)
        self.assertEqual(v[-1], 30)
        self.assertEqual(v[-2], 20)

    async def test_getitem_out_of_range(self):
        """out of range index raises IndexError"""

        v = values(1, 2)
        with self.assertRaises(IndexError):
            _ = v[5]

    async def test_iter(self):
        """iterating yields positional arguments"""

        v = values(1, 2, 3)
        result = list(v)
        self.assertEqual(result, [1, 2, 3])


class TestValuesMapping(unittest.IsolatedAsyncioTestCase):
    """Tests for values mapping protocol"""

    async def test_getitem_key(self):
        """indexing by string returns keyword value"""

        v = values(x=100, y=200)
        self.assertEqual(v['x'], 100)
        self.assertEqual(v['y'], 200)

    async def test_getitem_missing_key(self):
        """missing key raises KeyError"""

        v = values(x=1)
        with self.assertRaises(KeyError):
            _ = v['missing']

    async def test_getitem_int_key(self):
        """indexing by int key accesses positionals"""

        v = values(10, 20, 30)
        # Integer key via mapping protocol
        self.assertEqual(v[0], 10)

    async def test_keys_method(self):
        """keys() returns keyword keys"""

        v = values(1, 2, x=3, y=4)
        keys = set(v.keys())
        self.assertEqual(keys, {'x', 'y'})

    async def test_keys_empty(self):
        """keys() on values with no keywords returns empty iterator"""

        v = values(1, 2, 3)
        keys = list(v.keys())
        self.assertEqual(keys, [])


class TestValuesRepr(unittest.IsolatedAsyncioTestCase):
    """Tests for values repr"""

    async def test_repr_empty(self):
        """repr of empty values"""

        v = values()
        self.assertEqual(repr(v), "values()")

    async def test_repr_positionals(self):
        """repr of values with positionals"""

        v = values(1, 2)
        self.assertEqual(repr(v), "values(1, 2)")

    async def test_repr_keywords(self):
        """repr of values with keywords contains key=value"""

        v = values(x=1)
        r = repr(v)
        self.assertIn("x=1", r)
        self.assertTrue(r.startswith("values("))
        self.assertTrue(r.endswith(")"))

    async def test_repr_both(self):
        """repr of values with both positionals and keywords"""

        v = values(1, x=2)
        r = repr(v)
        self.assertIn("1", r)
        self.assertIn("x=2", r)


class TestValuesHash(unittest.IsolatedAsyncioTestCase):
    """Tests for values hash"""

    async def test_hashable(self):
        """values are hashable"""

        v = values(1, 2, 3)
        h = hash(v)
        self.assertIsInstance(h, int)

    async def test_hash_consistent(self):
        """equal values have equal hash"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(hash(v1), hash(v2))

    async def test_hash_with_keywords(self):
        """values with keywords are hashable"""

        v = values(1, x=2)
        h = hash(v)
        self.assertIsInstance(h, int)


class TestValuesEquality(unittest.IsolatedAsyncioTestCase):
    """Tests for values equality"""

    async def test_eq_same_positionals(self):
        """values with same positionals are equal"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(v1, v2)

    async def test_ne_different_positionals(self):
        """values with different positionals are not equal"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 4)
        self.assertNotEqual(v1, v2)

    async def test_eq_same_keywords(self):
        """values with same keywords are equal"""

        v1 = values(x=1, y=2)
        v2 = values(x=1, y=2)
        self.assertEqual(v1, v2)

    async def test_ne_different_keywords(self):
        """values with different keywords are not equal"""

        v1 = values(x=1)
        v2 = values(x=2)
        self.assertNotEqual(v1, v2)

    async def test_eq_tuple(self):
        """values with no keywords equals equivalent tuple"""

        v = values(1, 2, 3)
        self.assertEqual(v, (1, 2, 3))

    async def test_ne_tuple_with_keywords(self):
        """values with keywords does not equal tuple"""

        v = values(1, 2, x=3)
        self.assertNotEqual(v, (1, 2))

    async def test_eq_dict(self):
        """values with no positionals equals equivalent dict"""

        v = values(x=1, y=2)
        self.assertEqual(v, {'x': 1, 'y': 2})

    async def test_ne_dict_with_positionals(self):
        """values with positionals does not equal dict"""

        v = values(1, x=2)
        self.assertNotEqual(v, {'x': 2})

    async def test_unsupported_comparison(self):
        """ordering comparisons raise TypeError"""

        v1 = values(1, 2)
        v2 = values(3, 4)
        with self.assertRaises(TypeError):
            _ = v1 < v2
        with self.assertRaises(TypeError):
            _ = v1 <= v2
        with self.assertRaises(TypeError):
            _ = v1 > v2
        with self.assertRaises(TypeError):
            _ = v1 >= v2


class TestValuesAdd(unittest.IsolatedAsyncioTestCase):
    """Tests for values addition"""

    async def test_add_values(self):
        """adding two values concatenates positionals"""

        v1 = values(1, 2)
        v2 = values(3, 4)
        result = v1 + v2
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    async def test_add_values_merges_keywords(self):
        """adding two values merges keywords"""

        v1 = values(x=1)
        v2 = values(y=2)
        result = v1 + v2
        self.assertEqual(result['x'], 1)
        self.assertEqual(result['y'], 2)

    async def test_add_values_keyword_override(self):
        """right values keywords override left"""

        v1 = values(x=1)
        v2 = values(x=2)
        result = v1 + v2
        self.assertEqual(result['x'], 2)

    async def test_add_tuple(self):
        """adding tuple to values concatenates positionals"""

        v = values(1, 2)
        result = v + (3, 4)
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    async def test_radd_tuple(self):
        """adding values to tuple concatenates positionals"""

        v = values(3, 4)
        result = (1, 2) + v
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    async def test_add_list(self):
        """adding list to values concatenates positionals"""

        v = values(1, 2)
        result = v + [3, 4]
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    async def test_add_dict(self):
        """adding dict to values merges keywords"""

        v = values(1, 2, x=10)
        result = v + {'y': 20}
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2])
        self.assertEqual(result['x'], 10)
        self.assertEqual(result['y'], 20)

    async def test_radd_dict(self):
        """adding values to dict merges keywords"""

        v = values(y=20)
        result = {'x': 10} + v
        self.assertIsInstance(result, values)
        self.assertEqual(result['x'], 10)
        self.assertEqual(result['y'], 20)


class TestValuesCall(unittest.IsolatedAsyncioTestCase):
    """Tests for values call protocol"""

    async def test_call_function(self):
        """calling values applies function to args"""

        v = values(5, 10, 3)
        result = v(max)
        self.assertEqual(result, 10)

    async def test_call_with_extra_args(self):
        """calling values with extra positionals includes them"""

        v = values(1, 2)
        result = v(max, 3, 4)
        self.assertEqual(result, 4)

    async def test_call_with_keywords(self):
        """calling values with keywords merges them"""

        def func(*args, **kwargs):
            return (args, kwargs)

        v = values(1, 2, x=10)
        result = v(func, y=20)
        self.assertEqual(result, ((1, 2), {'x': 10, 'y': 20}))

    async def test_call_no_function_error(self):
        """calling values without function raises TypeError"""

        v = values(1, 2, 3)
        with self.assertRaises(TypeError):
            v()

    async def test_call_empty_values(self):
        """calling empty values works"""

        v = values()
        result = v(list)
        self.assertEqual(result, [])


class TestValuesIdentity(unittest.IsolatedAsyncioTestCase):
    """Tests for values identity and copying"""

    async def test_identity(self):
        """same values object is equal to itself"""

        v = values(1, 2, 3)
        self.assertIs(v, v)
        self.assertEqual(v, v)

    async def test_different_instances(self):
        """equal values are not the same object"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(v1, v2)
        self.assertIsNot(v1, v2)


# The end.
