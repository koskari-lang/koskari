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
Unit tests for type predicates and helper functions

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import (
    cons, demand_call, keyword, nil, pair, symbol)
from koskari.types import (
    BuiltinPredicate, TypePredicate, build_proper,
    fill_position, get_position, is_keyword, is_nil, is_on_demand, is_pair,
    is_proper, is_recursive, is_symbol, set_position, unpack)


pytestmark = pytest.mark.phase_lib


class TestTypePredicate(unittest.IsolatedAsyncioTestCase):
    """Tests for the TypePredicate class"""

    async def test_create(self):
        """TypePredicate can be created with a name and type"""
        pred = TypePredicate("test?", int)
        self.assertEqual(pred.__name__, "test?")

    async def test_callable(self):
        """TypePredicate is callable"""
        pred = TypePredicate("int?", int)
        self.assertTrue(callable(pred))

    async def test_returns_true_for_instance(self):
        """TypePredicate returns True for instances of the type"""
        pred = TypePredicate("int?", int)
        self.assertTrue(pred(42))

    async def test_returns_false_for_non_instance(self):
        """TypePredicate returns False for non-instances"""
        pred = TypePredicate("int?", int)
        self.assertFalse(pred("hello"))

    async def test_repr(self):
        """TypePredicate has a descriptive repr"""
        pred = TypePredicate("int?", int)
        self.assertIn("int?", repr(pred))

    async def test_default_name(self):
        """TypePredicate uses type name + ? if name is None"""
        pred = TypePredicate(None, int)
        self.assertEqual(pred.__name__, "int?")


class TestBuiltinPredicate(unittest.IsolatedAsyncioTestCase):
    """Tests for the BuiltinPredicate class"""

    async def test_create(self):
        """BuiltinPredicate can be created with name and callable"""
        pred = BuiltinPredicate("even?", lambda x: x % 2 == 0)
        self.assertEqual(pred.__name__, "even?")

    async def test_callable(self):
        """BuiltinPredicate is callable"""
        pred = BuiltinPredicate("even?", lambda x: x % 2 == 0)
        self.assertTrue(callable(pred))

    async def test_with_partial_args(self):
        """BuiltinPredicate can use partial application"""
        import operator
        pred = BuiltinPredicate("is_none?", operator.is_, None)
        self.assertTrue(pred(None))
        self.assertFalse(pred(42))

    async def test_repr(self):
        """BuiltinPredicate has a descriptive repr"""
        pred = BuiltinPredicate("test?", lambda x: True)
        self.assertIn("test?", repr(pred))


class TestIsSymbol(unittest.IsolatedAsyncioTestCase):
    """Tests for is_symbol predicate"""

    async def test_true_for_symbol(self):
        """is_symbol returns True for symbols"""
        self.assertTrue(is_symbol(symbol("foo")))

    async def test_false_for_keyword(self):
        """is_symbol returns False for keywords"""
        self.assertFalse(is_symbol(keyword("foo")))

    async def test_false_for_string(self):
        """is_symbol returns False for strings"""
        self.assertFalse(is_symbol("foo"))

    async def test_false_for_pair(self):
        """is_symbol returns False for pairs"""
        self.assertFalse(is_symbol(pair(1, 2)))

    async def test_name(self):
        """is_symbol has the expected name"""
        self.assertEqual(is_symbol.__name__, "symbol?")


class TestIsKeyword(unittest.IsolatedAsyncioTestCase):
    """Tests for is_keyword predicate"""

    async def test_true_for_keyword(self):
        """is_keyword returns True for keywords"""
        self.assertTrue(is_keyword(keyword("foo")))

    async def test_false_for_symbol(self):
        """is_keyword returns False for symbols"""
        self.assertFalse(is_keyword(symbol("foo")))

    async def test_false_for_string(self):
        """is_keyword returns False for strings"""
        self.assertFalse(is_keyword("foo"))

    async def test_name(self):
        """is_keyword has the expected name"""
        self.assertEqual(is_keyword.__name__, "keyword?")


class TestIsPair(unittest.IsolatedAsyncioTestCase):
    """Tests for is_pair predicate"""

    async def test_true_for_pair(self):
        """is_pair returns True for pairs"""
        self.assertTrue(is_pair(pair(1, 2)))

    async def test_true_for_cons(self):
        """is_pair returns True for cons-created pairs"""
        self.assertTrue(is_pair(cons(1, 2)))

    async def test_true_for_nil(self):
        """is_pair returns True for nil (nil is a pair subtype)"""
        self.assertTrue(is_pair(nil))

    async def test_false_for_list(self):
        """is_pair returns False for Python lists"""
        self.assertFalse(is_pair([1, 2]))

    async def test_false_for_tuple(self):
        """is_pair returns False for tuples"""
        self.assertFalse(is_pair((1, 2)))

    async def test_name(self):
        """is_pair has the expected name"""
        self.assertEqual(is_pair.__name__, "pair?")


class TestIsNil(unittest.IsolatedAsyncioTestCase):
    """Tests for is_nil predicate"""

    async def test_true_for_nil(self):
        """is_nil returns True for nil"""
        self.assertTrue(is_nil(nil))

    async def test_false_for_pair(self):
        """is_nil returns False for pairs"""
        self.assertFalse(is_nil(pair(1, 2)))

    async def test_false_for_none(self):
        """is_nil returns False for Python None"""
        self.assertFalse(is_nil(None))

    async def test_false_for_empty_list(self):
        """is_nil returns False for empty Python list"""
        self.assertFalse(is_nil([]))

    async def test_name(self):
        """is_nil has the expected name"""
        self.assertEqual(is_nil.__name__, "nil?")


class TestIsProper(unittest.IsolatedAsyncioTestCase):
    """Tests for is_proper predicate"""

    async def test_true_for_proper_list(self):
        """is_proper returns True for proper lists"""
        proper = cons(1, 2, 3, nil)
        self.assertTrue(is_proper(proper))

    async def test_false_for_improper_pair(self):
        """is_proper returns False for improper pairs"""
        improper = cons(1, 2)
        self.assertFalse(is_proper(improper))

    async def test_true_for_nil(self):
        """is_proper returns True for nil (the empty proper list)"""
        self.assertTrue(is_proper(nil))

    async def test_false_for_non_pair(self):
        """is_proper returns False for non-pairs"""
        self.assertFalse(is_proper([1, 2, 3]))
        self.assertFalse(is_proper("hello"))
        self.assertFalse(is_proper(42))


class TestIsRecursive(unittest.IsolatedAsyncioTestCase):
    """Tests for is_recursive predicate"""

    async def test_false_for_proper_list(self):
        """is_recursive returns False for proper lists"""
        proper = cons(1, 2, nil)
        self.assertFalse(is_recursive(proper))

    async def test_true_for_circular(self):
        """is_recursive returns True for circular structures"""
        p = cons(1, nil)
        from koskari.lib import setcdr
        setcdr(p, p)
        self.assertTrue(is_recursive(p))

    async def test_false_for_non_pair(self):
        """is_recursive returns False for non-pairs"""
        self.assertFalse(is_recursive([1, 2, 3]))
        self.assertFalse(is_recursive(nil))


class TestBuildProper(unittest.IsolatedAsyncioTestCase):
    """Tests for build_proper function"""

    async def test_empty(self):
        """build_proper with no args returns nil"""
        result = build_proper()
        self.assertIs(result, nil)

    async def test_single(self):
        """build_proper with one arg creates single-element list"""
        result = build_proper(1)
        self.assertTrue(is_pair(result))
        self.assertEqual(result[0], 1)
        self.assertIs(result[1], nil)

    async def test_multiple(self):
        """build_proper with multiple args creates proper list"""
        result = build_proper(1, 2, 3)
        self.assertTrue(is_proper(result))
        self.assertEqual(list(result.unpack()), [1, 2, 3])

    async def test_preserves_order(self):
        """build_proper preserves argument order"""
        result = build_proper("a", "b", "c")
        items = list(result.unpack())
        self.assertEqual(items, ["a", "b", "c"])


class TestUnpack(unittest.IsolatedAsyncioTestCase):
    """Tests for unpack function"""

    async def test_unpack_pair(self):
        """unpack on a pair returns its unpack iterator"""
        p = cons(1, 2, 3, nil)
        result = list(unpack(p))
        self.assertEqual(result, [1, 2, 3])

    async def test_unpack_list(self):
        """unpack on a Python list returns an iterator"""
        result = list(unpack([1, 2, 3]))
        self.assertEqual(result, [1, 2, 3])

    async def test_unpack_tuple(self):
        """unpack on a tuple returns an iterator"""
        result = list(unpack((1, 2, 3)))
        self.assertEqual(result, [1, 2, 3])

    async def test_unpack_string(self):
        """unpack on a string returns character iterator"""
        result = list(unpack("abc"))
        self.assertEqual(result, ["a", "b", "c"])


class TestGetPosition(unittest.IsolatedAsyncioTestCase):
    """Tests for get_position function"""

    async def test_returns_none_for_no_position(self):
        """get_position returns None when no position is set"""
        p = cons(1, nil)
        self.assertIsNone(get_position(p))

    async def test_returns_default_for_no_position(self):
        """get_position returns default when no position is set"""
        p = cons(1, nil)
        default = (1, 0)
        self.assertEqual(get_position(p, default), default)

    async def test_returns_position_when_set(self):
        """get_position returns the position after set_position"""
        p = cons(1, nil)
        pos = (10, 5)
        p.set_position(pos)
        self.assertEqual(get_position(p), pos)

    async def test_returns_default_for_non_pair(self):
        """get_position returns default for non-pairs"""
        self.assertIsNone(get_position("not a pair"))
        self.assertEqual(get_position(42, (0, 0)), (0, 0))

    async def test_returns_default_for_nil(self):
        """get_position returns default for nil"""
        self.assertIsNone(get_position(nil))
        self.assertEqual(get_position(nil, (1, 1)), (1, 1))


class TestSetPosition(unittest.IsolatedAsyncioTestCase):
    """Tests for set_position function"""

    async def test_sets_position(self):
        """set_position sets the position on a pair"""
        p = cons(1, nil)
        pos = (5, 10)
        set_position(p, pos)
        self.assertEqual(get_position(p), pos)

    async def test_no_op_for_non_pair(self):
        """set_position is a no-op for non-pairs"""
        value = "not a pair"
        set_position(value, (1, 1))

    async def test_no_op_for_none_position(self):
        """set_position is a no-op when position is None"""
        p = cons(1, nil)
        set_position(p, None)
        self.assertIsNone(get_position(p))

    async def test_follow_false(self):
        """set_position with follow=False only sets on first pair"""
        p = cons(1, 2, nil)
        pos = (1, 0)
        set_position(p, pos, follow=False)
        self.assertEqual(get_position(p), pos)
        self.assertIsNone(get_position(p[1]))

    async def test_follow_true(self):
        """set_position with follow=True sets on all pairs"""
        p = cons(1, 2, nil)
        pos = (1, 0)
        set_position(p, pos, follow=True)
        self.assertEqual(get_position(p), pos)
        self.assertEqual(get_position(p[1]), pos)


class TestFillPosition(unittest.IsolatedAsyncioTestCase):
    """Tests for fill_position function"""

    async def test_fills_empty_position(self):
        """fill_position sets position when not already set"""
        p = cons(1, nil)
        pos = (5, 10)
        fill_position(p, pos)
        self.assertEqual(get_position(p), pos)

    async def test_does_not_overwrite(self):
        """fill_position does not overwrite existing position"""
        p = cons(1, nil)
        original = (1, 1)
        new_pos = (9, 9)
        set_position(p, original)
        fill_position(p, new_pos)
        self.assertEqual(get_position(p), original)

    async def test_no_op_for_non_pair(self):
        """fill_position is a no-op for non-pairs"""
        value = "not a pair"
        fill_position(value, (1, 1))

    async def test_follow_fills_all(self):
        """fill_position with follow=True fills all pairs without position"""
        inner = cons(2, nil)
        outer = cons(1, inner)
        fill_position(outer, (5, 5), follow=True)
        self.assertEqual(get_position(outer), (5, 5))
        self.assertEqual(get_position(inner), (5, 5))


class TestIsOnDemand(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the is_on_demand async predicate.

    Probes the execution context by attempting a single suspension: if
    KoskariSyncViolation is thrown into the coroutine, we are on the demand
    path; if the suspension succeeds normally, we are not.
    """

    def test_returns_true_under_demand_call(self):
        """
        is_on_demand returns True when driven by demand_call.
        """

        result = demand_call(is_on_demand)
        self.assertTrue(result)


    async def test_returns_false_in_async_context(self):
        """
        is_on_demand returns False when awaited in a normal async context.
        """

        result = await is_on_demand()
        self.assertFalse(result)


# The end.
