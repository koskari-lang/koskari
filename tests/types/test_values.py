"""
Unit tests for values type

author: Christopher O'Brien <obriencj@gmail.com>
license: GPL v.3
"""


import unittest

from koskari.lib import values


class TestValuesCreate(unittest.TestCase):
    """Tests for values creation"""

    def test_create_empty(self):
        """values can be created with no arguments"""

        v = values()
        self.assertIsInstance(v, values)

    def test_create_positionals(self):
        """values can be created with positional arguments"""

        v = values(1, 2, 3)
        self.assertIsInstance(v, values)
        self.assertEqual(len(v), 3)

    def test_create_keywords(self):
        """values can be created with keyword arguments"""

        v = values(x=1, y=2)
        self.assertIsInstance(v, values)
        self.assertEqual(v['x'], 1)
        self.assertEqual(v['y'], 2)

    def test_create_both(self):
        """values can be created with both positionals and keywords"""

        v = values(1, 2, x=3, y=4)
        self.assertIsInstance(v, values)
        self.assertEqual(len(v), 2)
        self.assertEqual(v['x'], 3)


class TestValuesBool(unittest.TestCase):
    """Tests for values bool conversion"""

    def test_empty_is_falsy(self):
        """empty values is falsy"""

        v = values()
        self.assertFalse(v)
        self.assertFalse(bool(v))

    def test_positionals_is_truthy(self):
        """values with positionals is truthy"""

        v = values(1)
        self.assertTrue(v)
        self.assertTrue(bool(v))

    def test_keywords_is_truthy(self):
        """values with keywords only is truthy"""

        v = values(x=1)
        self.assertTrue(v)


class TestValuesSequence(unittest.TestCase):
    """Tests for values sequence protocol"""

    def test_len_positionals(self):
        """len() returns number of positional arguments"""

        v = values(1, 2, 3)
        self.assertEqual(len(v), 3)

    def test_len_no_keywords(self):
        """len() does not count keywords"""

        v = values(1, 2, x=3, y=4)
        self.assertEqual(len(v), 2)

    def test_getitem_index(self):
        """indexing by int returns positional"""

        v = values(10, 20, 30)
        self.assertEqual(v[0], 10)
        self.assertEqual(v[1], 20)
        self.assertEqual(v[2], 30)

    def test_getitem_negative_index(self):
        """negative indexing works"""

        v = values(10, 20, 30)
        self.assertEqual(v[-1], 30)
        self.assertEqual(v[-2], 20)

    def test_getitem_out_of_range(self):
        """out of range index raises IndexError"""

        v = values(1, 2)
        with self.assertRaises(IndexError):
            _ = v[5]

    def test_iter(self):
        """iterating yields positional arguments"""

        v = values(1, 2, 3)
        result = list(v)
        self.assertEqual(result, [1, 2, 3])


class TestValuesMapping(unittest.TestCase):
    """Tests for values mapping protocol"""

    def test_getitem_key(self):
        """indexing by string returns keyword value"""

        v = values(x=100, y=200)
        self.assertEqual(v['x'], 100)
        self.assertEqual(v['y'], 200)

    def test_getitem_missing_key(self):
        """missing key raises KeyError"""

        v = values(x=1)
        with self.assertRaises(KeyError):
            _ = v['missing']

    def test_getitem_int_key(self):
        """indexing by int key accesses positionals"""

        v = values(10, 20, 30)
        # Integer key via mapping protocol
        self.assertEqual(v[0], 10)

    def test_keys_method(self):
        """keys() returns keyword keys"""

        v = values(1, 2, x=3, y=4)
        keys = set(v.keys())
        self.assertEqual(keys, {'x', 'y'})

    def test_keys_empty(self):
        """keys() on values with no keywords returns empty iterator"""

        v = values(1, 2, 3)
        keys = list(v.keys())
        self.assertEqual(keys, [])


class TestValuesRepr(unittest.TestCase):
    """Tests for values repr"""

    def test_repr_empty(self):
        """repr of empty values"""

        v = values()
        self.assertEqual(repr(v), "values()")

    def test_repr_positionals(self):
        """repr of values with positionals"""

        v = values(1, 2)
        self.assertEqual(repr(v), "values(1, 2)")

    def test_repr_keywords(self):
        """repr of values with keywords contains key=value"""

        v = values(x=1)
        r = repr(v)
        self.assertIn("x=1", r)
        self.assertTrue(r.startswith("values("))
        self.assertTrue(r.endswith(")"))

    def test_repr_both(self):
        """repr of values with both positionals and keywords"""

        v = values(1, x=2)
        r = repr(v)
        self.assertIn("1", r)
        self.assertIn("x=2", r)


class TestValuesHash(unittest.TestCase):
    """Tests for values hash"""

    def test_hashable(self):
        """values are hashable"""

        v = values(1, 2, 3)
        h = hash(v)
        self.assertIsInstance(h, int)

    def test_hash_consistent(self):
        """equal values have equal hash"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(hash(v1), hash(v2))

    def test_hash_with_keywords(self):
        """values with keywords are hashable"""

        v = values(1, x=2)
        h = hash(v)
        self.assertIsInstance(h, int)


class TestValuesEquality(unittest.TestCase):
    """Tests for values equality"""

    def test_eq_same_positionals(self):
        """values with same positionals are equal"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(v1, v2)

    def test_ne_different_positionals(self):
        """values with different positionals are not equal"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 4)
        self.assertNotEqual(v1, v2)

    def test_eq_same_keywords(self):
        """values with same keywords are equal"""

        v1 = values(x=1, y=2)
        v2 = values(x=1, y=2)
        self.assertEqual(v1, v2)

    def test_ne_different_keywords(self):
        """values with different keywords are not equal"""

        v1 = values(x=1)
        v2 = values(x=2)
        self.assertNotEqual(v1, v2)

    def test_eq_tuple(self):
        """values with no keywords equals equivalent tuple"""

        v = values(1, 2, 3)
        self.assertEqual(v, (1, 2, 3))

    def test_ne_tuple_with_keywords(self):
        """values with keywords does not equal tuple"""

        v = values(1, 2, x=3)
        self.assertNotEqual(v, (1, 2))

    def test_eq_dict(self):
        """values with no positionals equals equivalent dict"""

        v = values(x=1, y=2)
        self.assertEqual(v, {'x': 1, 'y': 2})

    def test_ne_dict_with_positionals(self):
        """values with positionals does not equal dict"""

        v = values(1, x=2)
        self.assertNotEqual(v, {'x': 2})

    def test_unsupported_comparison(self):
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


class TestValuesAdd(unittest.TestCase):
    """Tests for values addition"""

    def test_add_values(self):
        """adding two values concatenates positionals"""

        v1 = values(1, 2)
        v2 = values(3, 4)
        result = v1 + v2
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    def test_add_values_merges_keywords(self):
        """adding two values merges keywords"""

        v1 = values(x=1)
        v2 = values(y=2)
        result = v1 + v2
        self.assertEqual(result['x'], 1)
        self.assertEqual(result['y'], 2)

    def test_add_values_keyword_override(self):
        """right values keywords override left"""

        v1 = values(x=1)
        v2 = values(x=2)
        result = v1 + v2
        self.assertEqual(result['x'], 2)

    def test_add_tuple(self):
        """adding tuple to values concatenates positionals"""

        v = values(1, 2)
        result = v + (3, 4)
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    def test_radd_tuple(self):
        """adding values to tuple concatenates positionals"""

        v = values(3, 4)
        result = (1, 2) + v
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    def test_add_list(self):
        """adding list to values concatenates positionals"""

        v = values(1, 2)
        result = v + [3, 4]
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2, 3, 4])

    def test_add_dict(self):
        """adding dict to values merges keywords"""

        v = values(1, 2, x=10)
        result = v + {'y': 20}
        self.assertIsInstance(result, values)
        self.assertEqual(list(result), [1, 2])
        self.assertEqual(result['x'], 10)
        self.assertEqual(result['y'], 20)

    def test_radd_dict(self):
        """adding values to dict merges keywords"""

        v = values(y=20)
        result = {'x': 10} + v
        self.assertIsInstance(result, values)
        self.assertEqual(result['x'], 10)
        self.assertEqual(result['y'], 20)


class TestValuesCall(unittest.TestCase):
    """Tests for values call protocol"""

    def test_call_function(self):
        """calling values applies function to args"""

        v = values(5, 10, 3)
        result = v(max)
        self.assertEqual(result, 10)

    def test_call_with_extra_args(self):
        """calling values with extra positionals includes them"""

        v = values(1, 2)
        result = v(max, 3, 4)
        self.assertEqual(result, 4)

    def test_call_with_keywords(self):
        """calling values with keywords merges them"""

        def func(*args, **kwargs):
            return (args, kwargs)

        v = values(1, 2, x=10)
        result = v(func, y=20)
        self.assertEqual(result, ((1, 2), {'x': 10, 'y': 20}))

    def test_call_no_function_error(self):
        """calling values without function raises TypeError"""

        v = values(1, 2, 3)
        with self.assertRaises(TypeError):
            v()

    def test_call_empty_values(self):
        """calling empty values works"""

        v = values()
        result = v(list)
        self.assertEqual(result, [])


class TestValuesIdentity(unittest.TestCase):
    """Tests for values identity and copying"""

    def test_identity(self):
        """same values object is equal to itself"""

        v = values(1, 2, 3)
        self.assertIs(v, v)
        self.assertEqual(v, v)

    def test_different_instances(self):
        """equal values are not the same object"""

        v1 = values(1, 2, 3)
        v2 = values(1, 2, 3)
        self.assertEqual(v1, v2)
        self.assertIsNot(v1, v2)


# The end.
