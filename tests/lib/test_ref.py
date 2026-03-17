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
Unit tests for reference types

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest
from itertools import count
from unittest.mock import Mock

from koskari.lib import (
    Ref, AttributeRef, ItemRef, DynamicRef, ConstantRef, MemoizedRef,
    WeakRef,
    symbol,
)
from koskari.types import FormalsSpec
from tests import make_callable
from tests.lib.opcode import build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestSymbolicRef(unittest.IsolatedAsyncioTestCase):
    """Tests for the base Ref type"""

    async def test_create(self):
        """Ref can be created with no arguments"""
        r = Ref()
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """Ref without name argument gets default name"""
        r = Ref()
        self.assertEqual(r.name, "<Ref>")

    async def test_custom_name(self):
        """Ref with name argument uses that name"""
        r = Ref("foo")
        self.assertEqual(r.name, "foo")

    async def test_custom_symbol_name(self):
        """Ref accepts symbol for name and normalizes to string"""
        r = Ref(symbol("foo"))
        self.assertEqual(r.name, "foo")

    async def test_invalid_name_type_raises_typeerror(self):
        """Ref rejects non-string non-symbol names"""
        with self.assertRaises(TypeError):
            Ref(123)

    async def test_empty_initially(self):
        """Ref without value is empty"""
        r = Ref()
        self.assertTrue(r.empty)

    async def test_deref_empty_raises(self):
        """deref() on empty Ref raises NameError"""
        r = Ref()
        with self.assertRaises(NameError):
            r.deref()

    async def test_store_and_deref(self):
        """store() sets value, deref() retrieves it"""
        r = Ref()
        r.store(123)
        self.assertEqual(r.deref(), 123)

    async def test_store_overwrites(self):
        """store() overwrites previous value"""
        r = Ref()
        r.store(123)
        r.store(456)
        self.assertEqual(r.deref(), 456)

    async def test_clear(self):
        """clear() makes Ref empty again"""
        r = Ref()
        r.store(123)
        self.assertFalse(r.empty)
        r.clear()
        self.assertTrue(r.empty)
        with self.assertRaises(NameError):
            r.deref()

    async def test_store_after_clear(self):
        """store() works after clear()"""
        r = Ref()
        r.store(123)
        r.clear()
        r.store(456)
        self.assertEqual(r.deref(), 456)

    async def test_initial_value(self):
        """Ref can be created with initial value"""
        r = Ref("myref", 999)
        self.assertEqual(r.name, "myref")
        self.assertFalse(r.empty)
        self.assertEqual(r.deref(), 999)

    async def test_no_named_args(self):
        """Ref rejects keyword arguments"""
        with self.assertRaises(TypeError):
            Ref(name="foo")


class Obj:
    """Simple test object for attribute tests"""
    pass


class TestAttributeRef(unittest.IsolatedAsyncioTestCase):
    """Tests for AttributeRef type"""

    async def test_create(self):
        """AttributeRef can be created"""
        o = Obj()
        o.foo = "bar"
        r = AttributeRef(None, o, "foo")
        self.assertIsInstance(r, AttributeRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """AttributeRef without name gets default name"""
        o = Obj()
        o.foo = "bar"
        r = AttributeRef(None, o, "foo")
        self.assertEqual(r.name, "<AttributeRef>")

    async def test_custom_symbol_name(self):
        """AttributeRef accepts symbol for name and normalizes to string"""
        o = Obj()
        o.foo = "bar"
        r = AttributeRef(symbol("myattr"), o, "foo")
        self.assertEqual(r.name, "myattr")

    async def test_deref(self):
        """deref() returns the attribute value"""
        o = Obj()
        o.tacos = "yes, please"
        r = AttributeRef(None, o, "tacos")
        self.assertEqual(r.deref(), "yes, please")

    async def test_empty_false_when_attr_exists(self):
        """empty is False when attribute exists"""
        o = Obj()
        o.tacos = "yes"
        r = AttributeRef(None, o, "tacos")
        self.assertFalse(r.empty)

    async def test_store(self):
        """store() sets the attribute"""
        o = Obj()
        o.tacos = "yes"
        r = AttributeRef(None, o, "tacos")
        r.store("wonderful")
        self.assertEqual(o.tacos, "wonderful")
        self.assertEqual(r.deref(), "wonderful")

    async def test_clear(self):
        """clear() deletes the attribute"""
        o = Obj()
        o.tacos = "yes"
        r = AttributeRef(None, o, "tacos")
        r.clear()
        self.assertTrue(r.empty)
        self.assertFalse(hasattr(o, "tacos"))

    async def test_deref_missing_attr_raises(self):
        """deref() on missing attribute raises AttributeError"""
        o = Obj()
        r = AttributeRef(None, o, "missing")
        with self.assertRaises(AttributeError):
            r.deref()

    async def test_attr_must_be_string(self):
        """AttributeRef requires string for attr argument"""
        o = Obj()
        with self.assertRaises(TypeError):
            AttributeRef(None, o, 123)

    async def test_obj_property(self):
        """obj property returns the referenced object"""
        o = Obj()
        o.x = 1
        r = AttributeRef(None, o, "x")
        self.assertIs(r.obj, o)

    async def test_attr_property(self):
        """attr property returns the attribute name"""
        o = Obj()
        o.x = 1
        r = AttributeRef(None, o, "x")
        self.assertEqual(r.attr, "x")


class TestItemRef(unittest.IsolatedAsyncioTestCase):
    """Tests for ItemRef type"""

    async def test_create(self):
        """ItemRef can be created"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertIsInstance(r, ItemRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """ItemRef without name gets default name"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertEqual(r.name, "<ItemRef>")

    async def test_custom_symbol_name(self):
        """ItemRef accepts symbol for name and normalizes to string"""
        d = {"key": "value"}
        r = ItemRef(symbol("myitem"), d, "key")
        self.assertEqual(r.name, "myitem")

    async def test_deref_dict(self):
        """deref() works with dict"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertEqual(r.deref(), "value")

    async def test_deref_list(self):
        """deref() works with list"""
        l = ["foo", "bar", "baz"]
        r = ItemRef(None, l, -1)
        self.assertEqual(r.deref(), "baz")

    async def test_empty_false_when_key_exists(self):
        """empty is False when key exists"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertFalse(r.empty)

    async def test_empty_true_when_key_missing(self):
        """empty is True when key is missing"""
        d = {}
        r = ItemRef(None, d, "missing")
        self.assertTrue(r.empty)

    async def test_store(self):
        """store() sets the item"""
        d = {"key": "old"}
        r = ItemRef(None, d, "key")
        r.store("new")
        self.assertEqual(d["key"], "new")

    async def test_clear(self):
        """clear() deletes the item"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        r.clear()
        self.assertNotIn("key", d)

    async def test_clear_list(self):
        """clear() on list removes item at index"""
        l = ["foo", "bar", "baz"]
        r = ItemRef(None, l, -1)
        self.assertEqual(r.deref(), "baz")
        r.clear()
        self.assertEqual(r.deref(), "bar")
        r.clear()
        self.assertEqual(r.deref(), "foo")
        r.clear()
        self.assertTrue(r.empty)

    async def test_deref_missing_raises(self):
        """deref() on missing key raises KeyError"""
        d = {}
        r = ItemRef(None, d, "missing")
        with self.assertRaises(KeyError):
            r.deref()

    async def test_obj_property(self):
        """obj property returns the referenced object"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertIs(r.obj, d)

    async def test_key_property(self):
        """key property returns the key"""
        d = {"key": "value"}
        r = ItemRef(None, d, "key")
        self.assertEqual(r.key, "key")


class TestDynamicRef(unittest.IsolatedAsyncioTestCase):
    """Tests for DynamicRef type"""

    async def test_create(self):
        """DynamicRef can be created"""
        def getfn():
            return 42
        r = DynamicRef(None, getfn)
        self.assertIsInstance(r, DynamicRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """DynamicRef without name gets default name"""
        r = DynamicRef(None, lambda: 1)
        self.assertEqual(r.name, "<DynamicRef>")

    async def test_custom_symbol_name(self):
        """DynamicRef accepts symbol for name and normalizes to string"""
        r = DynamicRef(symbol("mydyn"), lambda: 1)
        self.assertEqual(r.name, "mydyn")

    async def test_deref_calls_getfn(self):
        """deref() calls the getter function"""
        c = count()
        def getfn():
            return next(c)
        r = DynamicRef(None, getfn)
        self.assertEqual(r.deref(), 0)
        self.assertEqual(r.deref(), 1)
        self.assertEqual(r.deref(), 2)

    async def test_store_without_setfn_raises(self):
        """store() without setfn raises AttributeError"""
        r = DynamicRef(None, lambda: 1)
        with self.assertRaises(AttributeError):
            r.store(100)

    async def test_clear_without_delfn_raises(self):
        """clear() without delfn raises AttributeError"""
        r = DynamicRef(None, lambda: 1)
        with self.assertRaises(AttributeError):
            r.clear()

    async def test_store_with_setfn(self):
        """store() calls the setter function"""
        value = [0]
        def getfn():
            return value[0]
        def setfn(v):
            value[0] = v
        r = DynamicRef(None, getfn, setfn)
        r.store(100)
        self.assertEqual(r.deref(), 100)

    async def test_clear_with_delfn(self):
        """clear() calls the deleter function"""
        value = [42]
        def getfn():
            return value[0]
        def delfn():
            value[0] = 0
        r = DynamicRef(None, getfn, None, delfn)
        self.assertEqual(r.deref(), 42)
        r.clear()
        self.assertEqual(r.deref(), 0)

    async def test_empty_default_false(self):
        """empty is False by default"""
        r = DynamicRef(None, lambda: 1)
        self.assertFalse(r.empty)

    async def test_empty_with_emptyfn(self):
        """empty calls emptyfn when provided"""
        is_empty = [False]
        def emptyfn():
            return is_empty[0]
        r = DynamicRef(None, lambda: 1, None, None, emptyfn)
        self.assertFalse(r.empty)
        is_empty[0] = True
        self.assertTrue(r.empty)

    def test_deref_with_koskari_expression_getter(self):
        """deref via demand_sync when getter is Koskari Expression"""
        code, ns = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        getter = make_callable(code, ns)
        r = DynamicRef(None, getter)
        self.assertEqual(r.deref(), 42)

    def test_deref_with_koskari_function_getter(self):
        """deref via demand_sync when getter is Koskari Function"""
        _, base_ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('x',),
            values={'x': 100},
        )
        code = build_code(
            [['FAST', 0], ['RETURN', 1]],
            local_syms=(),
            closure_syms=('x',),
            name='getter',
        )
        getter = make_callable(code, base_ns, FormalsSpec())
        r = DynamicRef(None, getter)
        self.assertEqual(r.deref(), 100)

    def test_store_with_koskari_function_setter(self):
        """store via demand_sync when setter is Koskari Function"""
        box = [0]
        _, base_ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('box',),
            values={'box': box},
        )
        code = build_code(
            [['FAST', 1], ['CONST', 0], ['FAST', 0], ['ITEM_STORE', 0],
             ['CONST', None], ['RETURN', 1]],
            local_syms=('v',),
            closure_syms=('box',),
            name='setter',
        )
        setter = make_callable(code, base_ns, FormalsSpec(('v',)))
        r = DynamicRef(None, lambda: box[0], setter)
        r.store(99)
        self.assertEqual(r.deref(), 99)


class TestConstantRef(unittest.IsolatedAsyncioTestCase):
    """Tests for ConstantRef type"""

    async def test_create(self):
        """ConstantRef can be created"""
        r = ConstantRef(None, 101)
        self.assertIsInstance(r, ConstantRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """ConstantRef without name gets default name"""
        r = ConstantRef(None, 101)
        self.assertEqual(r.name, "<ConstantRef>")

    async def test_custom_symbol_name(self):
        """ConstantRef accepts symbol for name and normalizes to string"""
        r = ConstantRef(symbol("myconst"), 101)
        self.assertEqual(r.name, "myconst")

    async def test_deref(self):
        """deref() returns the constant value"""
        r = ConstantRef(None, 101)
        self.assertEqual(r.deref(), 101)

    async def test_never_empty(self):
        """ConstantRef is never empty"""
        r = ConstantRef(None, 101)
        self.assertFalse(r.empty)

    async def test_store_raises(self):
        """store() raises ValueError"""
        r = ConstantRef(None, 101)
        with self.assertRaises(ValueError):
            r.store(999)

    async def test_clear_raises(self):
        """clear() raises ValueError"""
        r = ConstantRef(None, 101)
        with self.assertRaises(ValueError):
            r.clear()

    async def test_value_unchanged_after_store_attempt(self):
        """Value remains unchanged after failed store()"""
        r = ConstantRef(None, 101)
        try:
            r.store(999)
        except ValueError:
            pass
        self.assertEqual(r.deref(), 101)


class TestMemoizedRef(unittest.IsolatedAsyncioTestCase):
    """Tests for MemoizedRef type"""

    async def test_create(self):
        """MemoizedRef can be created"""
        r = MemoizedRef(None, lambda: 42)
        self.assertIsInstance(r, MemoizedRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """MemoizedRef without name gets default name"""
        r = MemoizedRef(None, lambda: 42)
        self.assertEqual(r.name, "<MemoizedRef>")

    async def test_custom_symbol_name(self):
        """MemoizedRef accepts symbol for name and normalizes to string"""
        r = MemoizedRef(symbol("mymemo"), lambda: 42)
        self.assertEqual(r.name, "mymemo")

    async def test_initially_empty(self):
        """MemoizedRef is empty before first deref"""
        r = MemoizedRef(None, lambda: 42)
        self.assertTrue(r.empty)

    async def test_deref_calls_getfn_once(self):
        """deref() calls getfn only once and caches result"""
        lazy = Mock(side_effect=range(15, 20))
        r = MemoizedRef(None, lazy)

        self.assertEqual(r.deref(), 15)
        self.assertEqual(r.deref(), 15)
        lazy.assert_called_once()

    async def test_not_empty_after_deref(self):
        """MemoizedRef is not empty after first deref"""
        r = MemoizedRef(None, lambda: 42)
        r.deref()
        self.assertFalse(r.empty)

    async def test_clear_resets_memoization(self):
        """clear() resets the cached value"""
        lazy = Mock(side_effect=[10, 20, 30])
        r = MemoizedRef(None, lazy)

        self.assertEqual(r.deref(), 10)
        lazy.assert_called_once()

        r.clear()
        self.assertTrue(r.empty)

        lazy.reset_mock()
        self.assertEqual(r.deref(), 20)
        lazy.assert_called_once()

    async def test_store_raises(self):
        """store() raises ValueError"""
        r = MemoizedRef(None, lambda: 42)
        with self.assertRaises(ValueError):
            r.store(999)

    async def test_store_raises_after_clear(self):
        """store() raises ValueError even after clear"""
        r = MemoizedRef(None, lambda: 42)
        r.deref()
        r.clear()
        with self.assertRaises(ValueError):
            r.store(999)

    async def test_getfn_error_propagates(self):
        """Errors from getfn propagate through deref()"""
        def bad():
            raise ValueError("bleep bloop")
        r = MemoizedRef(None, bad)
        with self.assertRaises(ValueError):
            r.deref()

    async def test_requires_callable(self):
        """MemoizedRef requires a callable getfn"""
        with self.assertRaises(ValueError):
            MemoizedRef(None, None)


class TestWeakRef(unittest.IsolatedAsyncioTestCase):
    """Tests for WeakRef type"""

    async def test_create(self):
        """WeakRef can be created"""
        obj = Obj()
        r = WeakRef(None, obj)
        self.assertIsInstance(r, WeakRef)
        self.assertIsInstance(r, Ref)

    async def test_default_name(self):
        """WeakRef without name gets default name"""
        r = WeakRef(None, Obj())
        self.assertEqual(r.name, "<WeakRef>")

    async def test_custom_symbol_name(self):
        """WeakRef accepts symbol for name and normalizes to string"""
        r = WeakRef(symbol("myweak"), Obj())
        self.assertEqual(r.name, "myweak")

    async def test_empty_initially_when_no_value(self):
        """WeakRef without value is empty"""
        r = WeakRef()
        self.assertTrue(r.empty)

    async def test_deref_empty_raises(self):
        """deref() on empty WeakRef raises NameError"""
        r = WeakRef()
        with self.assertRaises(NameError):
            r.deref()

    async def test_store_and_deref(self):
        """store() and deref() work with weakly referenced object"""
        obj = Obj()
        r = WeakRef(None, obj)
        self.assertFalse(r.empty)
        self.assertIs(r.deref(), obj)

    async def test_expired_deref_raises_and_clears(self):
        """deref() on expired weakref raises NameError and clears slot"""
        obj = Obj()
        r = WeakRef(None, obj)
        del obj
        import gc
        gc.collect()
        with self.assertRaises(NameError):
            r.deref()
        self.assertTrue(r.empty)
        with self.assertRaises(NameError):
            r.deref()

    async def test_store_overwrites(self):
        """store() overwrites previous value"""
        a, b = Obj(), Obj()
        r = WeakRef(None, a)
        self.assertIs(r.deref(), a)
        r.store(b)
        self.assertIs(r.deref(), b)

    async def test_clear(self):
        """clear() makes WeakRef empty"""
        obj = Obj()
        r = WeakRef(None, obj)
        self.assertFalse(r.empty)
        r.clear()
        self.assertTrue(r.empty)
        with self.assertRaises(NameError):
            r.deref()

    async def test_no_named_args(self):
        """WeakRef rejects keyword arguments"""
        with self.assertRaises(TypeError):
            WeakRef(name="foo", value=Obj())


class TestRefTypeHierarchy(unittest.IsolatedAsyncioTestCase):
    """Tests for ref type relationships"""

    async def test_all_inherit_from_ref(self):
        """All ref types inherit from Ref"""
        o = Obj()
        o.x = 1
        d = {"k": "v"}

        refs = [
            Ref(),
            AttributeRef(None, o, "x"),
            ItemRef(None, d, "k"),
            DynamicRef(None, lambda: 1),
            ConstantRef(None, 42),
            MemoizedRef(None, lambda: 42),
            WeakRef(None, o),
        ]

        for r in refs:
            self.assertIsInstance(r, Ref)


# The end.
