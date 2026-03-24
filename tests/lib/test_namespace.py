# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
Unit tests for namespace types

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest
from operator import itemgetter

from koskari.lib import (
    CollectorNamespace, Module, ModuleNamespace,
    Namespace, NamespaceSpec,     Ref,
)

import pytest

pytestmark = pytest.mark.phase_lib


class TestNamespaceSpec(unittest.IsolatedAsyncioTestCase):
    """Tests for the NamespaceSpec type"""

    async def test_create_empty(self):
        """NamespaceSpec can be created with empty sequences"""
        spec = NamespaceSpec(None, (), ())
        self.assertIsNone(spec.name)
        self.assertEqual(spec.local_syms, ())
        self.assertEqual(spec.closure_syms, ())


    async def test_create_with_values(self):
        """NamespaceSpec stores name and symbol sequences"""
        spec = NamespaceSpec("foo", ("a", "b"), ("z",))
        self.assertEqual(spec.name, "foo")
        self.assertEqual(spec.local_syms, ("a", "b"))
        self.assertEqual(spec.closure_syms, ("z",))


    async def test_get_namespace(self):
        """NamespaceSpec.get_namespace() creates a Namespace"""
        spec1 = NamespaceSpec(None, ("z",), ())
        self.assertIsNone(spec1.name)
        self.assertEqual(spec1.local_syms, ("z",))
        self.assertEqual(spec1.closure_syms, ())

        spec2 = NamespaceSpec("foo", ("a", "b"), ("z",))
        self.assertEqual(spec2.name, "foo")
        self.assertEqual(spec2.local_syms, ("a", "b"))
        self.assertEqual(spec2.closure_syms, ("z",))

        ns1 = spec1.get_namespace(None)
        ns2 = spec2.get_namespace(ns1)

        ns1["z"] = 123
        self.assertEqual(ns2["z"], 123)


    async def test_contains(self):
        """in operator works on NamespaceSpec"""
        spec = NamespaceSpec("test", ("a", "b"), ("z",))
        self.assertIn("a", spec)
        self.assertIn("b", spec)
        self.assertIn("z", spec)
        self.assertNotIn("missing", spec)


    async def test_repr(self):
        """repr shows name, closure_syms, local_syms, and global_syms"""
        spec = NamespaceSpec(None, ("k",), (), ())
        self.assertEqual(repr(spec), "NamespaceSpec(None, (), ('k',), ())")
        spec2 = NamespaceSpec("foo", ("a", "b"), ("z",), ())
        self.assertEqual(
            repr(spec2), "NamespaceSpec('foo', ('z',), ('a', 'b'), ())")


class TestNamespace(unittest.IsolatedAsyncioTestCase):
    """Tests for the Namespace type"""


    async def test_type_name(self):
        """Namespace type has correct name"""
        self.assertEqual(Namespace.__name__, "Namespace")


    async def test_create(self):
        """Namespace can be created"""
        ns = Namespace("ns", None, (), ())
        self.assertIs(type(ns), Namespace)


    async def test_name_and_parent(self):
        """Namespace stores name and parent"""
        ns = Namespace("ns", None, ('a',))
        self.assertEqual(ns.name, "ns")
        self.assertIsNone(ns.parent)


    async def test_deref_empty_raises(self):
        """deref() on unbound symbol raises NameError"""
        ns = Namespace("ns", None, ('a',))
        with self.assertRaises(NameError):
            ns.deref('a')


    async def test_store_and_deref(self):
        """store() sets value, deref() retrieves it"""
        ns = Namespace("ns", None, ('a',))
        ns.store('a', 123)
        self.assertEqual(ns.deref('a'), 123)


    async def test_raw_binding(self):
        """raw() returns True for fast bindings without ref"""
        ns = Namespace("ns", None, ('a',))
        ns.store('a', 123)
        self.assertTrue(ns.raw('a'))
        self.assertEqual(ns.deref('a'), 123)
        self.assertTrue(ns.raw('a'))


    async def test_subscript_access(self):
        """Namespace supports subscript access"""
        ns = Namespace("ns", None, ('a',))
        ns.store('a', 123)
        self.assertEqual(ns['a'], 123)

        ns['a'] = 456
        self.assertEqual(ns.deref('a'), 456)
        self.assertEqual(ns['a'], 456)


    async def test_ref_converts_raw(self):
        """ref() converts raw binding to reference"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 456
        self.assertTrue(ns.raw('a'))

        r = ns.ref('a')
        self.assertFalse(ns.raw('a'))
        self.assertEqual(r.deref(), 456)


    async def test_ref_clear(self):
        """Clearing ref makes binding empty"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 456
        r = ns.ref('a')

        r.clear()
        with self.assertRaises(NameError):
            ns.deref('a')
        with self.assertRaises(NameError):
            _ = ns['a']


    async def test_ref_store(self):
        """Storing via ref updates namespace"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 456
        r = ns.ref('a')

        r.clear()
        r.store(999)
        self.assertEqual(ns.deref('a'), 999)
        self.assertEqual(ns['a'], 999)


    async def test_clear_binding(self):
        """clear() removes value from binding"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 456
        r = ns.ref('a')

        ns.clear('a')
        with self.assertRaises(NameError):
            ns.deref('a')
        with self.assertRaises(NameError):
            _ = ns['a']


    async def test_copy(self):
        """copy() returns a new top-level namespace with same shape and closures"""
        parent = Namespace("parent", None, ('x',))
        parent['x'] = 100

        spec = NamespaceSpec("child", ('y',), ('x',))
        child = spec.get_namespace(parent)
        child['y'] = 42

        cp = child.copy()
        self.assertIsNot(cp, child)
        self.assertIs(cp.parent, parent)
        self.assertEqual(cp['x'], 100)
        self.assertIn('y', cp)
        with self.assertRaises(NameError):
            _ = cp['y']

        cp['y'] = 99
        self.assertEqual(cp['y'], 99)
        self.assertEqual(child['y'], 42)


    async def test_nested_namespaces(self):
        """Nested namespaces share closure bindings"""
        ns1 = Namespace("ns1", None, ('foo',))
        self.assertTrue(ns1.raw('foo'))

        ns2 = Namespace("ns2", ns1, ('bar',), ('foo',))

        # Creating closure converts raw to ref
        self.assertFalse(ns1.raw('foo'))
        self.assertFalse(ns2.raw('foo'))
        self.assertTrue(ns2.raw('bar'))

        self.assertEqual(ns1.name, "ns1")
        self.assertEqual(ns2.name, "ns2")
        self.assertIsNone(ns1.parent)
        self.assertIs(ns2.parent, ns1)


    async def test_nested_isolation(self):
        """Child namespace bindings are isolated from parent"""
        ns1 = Namespace("ns1", None, ('foo',))
        ns2 = Namespace("ns2", ns1, ('bar',), ('foo',))

        with self.assertRaises(NameError):
            ns1.deref('bar')
        with self.assertRaises(NameError):
            _ = ns1['bar']
        with self.assertRaises(NameError):
            ns1.store('bar', 123)

        with self.assertRaises(NameError):
            ns2.deref('bar')
        with self.assertRaises(NameError):
            _ = ns2['bar']

        ns2.store('bar', 123)
        self.assertTrue(ns2.raw('bar'))
        self.assertEqual(ns2.deref('bar'), 123)

        with self.assertRaises(NameError):
            ns1.deref('bar')


    async def test_nested_closure_sharing(self):
        """Parent and child share the same closure ref"""
        ns1 = Namespace("ns1", None, ('foo',))
        ns2 = Namespace("ns2", ns1, ('bar',), ('foo',))

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))

        ns1['foo'] = 987
        self.assertEqual(ns2['foo'], 987)

        del ns2['foo']
        with self.assertRaises(NameError):
            ns1.deref('foo')


    async def test_bind_ref(self):
        """bind() replaces the reference for a binding"""
        ns1 = Namespace("ns1", None, ('foo',))
        ns2 = Namespace("ns2", ns1, ('bar',), ('foo',))

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))
        ns2.store('foo', 123)
        self.assertEqual(ns1.deref('foo'), 123)

        madness = Ref('madness', 999)
        self.assertFalse(ns2.raw('foo'))

        ns2.bind('foo', madness)
        self.assertFalse(ns2.raw('foo'))

        self.assertIs(ns2.ref('foo'), madness)
        self.assertEqual(ns2.deref('foo'), 999)
        self.assertEqual(ns1.deref('foo'), 123)


    async def test_bind_local(self):
        """bind() works on local bindings"""
        ns1 = Namespace("ns1", None, ('foo',))
        ns2 = Namespace("ns2", ns1, ('bar',), ('foo',))

        self.assertTrue(ns2.raw('bar'))

        sanity = Ref('sanity', 456)
        ns2.bind('bar', sanity)
        self.assertFalse(ns2.raw('bar'))

        self.assertIs(ns2.ref('bar'), sanity)
        self.assertEqual(ns2.deref('bar'), 456)

        with self.assertRaises(NameError):
            ns1.deref('bar')


    async def test_bind_requires_ref(self):
        """bind() raises TypeError if not given a ref"""
        ns = Namespace("ns", None, ('bar',))
        with self.assertRaises(TypeError):
            ns.bind('bar', 123)


    async def test_find(self):
        """find() searches parent namespaces"""
        ns1 = Namespace("ns1", None, ('foo', 'bar', 'qux'))
        ns2 = Namespace("ns2", ns1, ('baz',), ('foo',))

        self.assertIn('foo', ns1)
        self.assertIn('bar', ns1)
        self.assertNotIn('baz', ns1)
        self.assertIn('qux', ns1)

        self.assertTrue(ns2.raw('baz'))

        self.assertIn('foo', ns2)
        self.assertNotIn('bar', ns2)
        self.assertIn('baz', ns2)
        self.assertNotIn('qux', ns2)

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))

        self.assertIs(ns2.find('foo'), ns2.ref('foo'))
        self.assertIs(ns2.find('bar'), ns1.ref('bar'))
        self.assertIs(ns2.find('baz'), ns2.ref('baz'))
        self.assertIs(ns2.find('qux'), ns1.ref('qux'))


    async def test_keys(self):
        """keys() returns list of binding names"""
        ns = Namespace("ns", None, ('a', 'b', 'c'))
        keys = ns.keys()
        self.assertEqual(sorted(keys), ['a', 'b', 'c'])


    async def test_items(self):
        """items() returns list of (name, ref) pairs"""
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 1
        ns['b'] = 2
        items = ns.items()
        self.assertEqual(len(items), 2)


    async def test_bound(self):
        """bound() returns True if name is bound"""
        ns = Namespace("ns", None, ('a',))
        self.assertTrue(ns.bound('a'))
        self.assertEqual(ns.bound('missing'), 0)


    async def test_len(self):
        """len() returns number of bindings"""
        ns = Namespace("ns", None, ('a', 'b', 'c'))
        self.assertEqual(len(ns), 3)


    async def test_default_name(self):
        """Namespace without name gets default"""
        ns = Namespace()
        self.assertEqual(ns.name, "<namespace>")


class TestModuleNamespace(unittest.IsolatedAsyncioTestCase):
    """
    Tests for the ModuleNamespace type
    """

    async def test_type(self):
        """ModuleNamespace is a subclass of Namespace"""
        self.assertTrue(issubclass(ModuleNamespace, Namespace))
        self.assertEqual(ModuleNamespace.__name__, "ModuleNamespace")


    async def test_create(self):
        """ModuleNamespace can be created"""
        mn = ModuleNamespace("mn", {})

        self.assertIs(type(mn), ModuleNamespace)
        self.assertIsInstance(mn, ModuleNamespace)
        self.assertIsInstance(mn, Namespace)


    async def test_name_and_parent(self):
        """ModuleNamespace has name and no parent"""
        ns = ModuleNamespace("ns", {})
        self.assertEqual(ns.name, "ns")
        self.assertIsNone(ns.parent)

    async def test_builtins_fallback(self):
        """ModuleNamespace falls back to builtins for undefined"""
        ns = ModuleNamespace("ns", {'b': 987})
        self.assertEqual(ns.deref('b'), 987)


    async def test_store_and_deref(self):
        """store() and deref() work on ModuleNamespace"""
        ns = ModuleNamespace("ns", {})

        with self.assertRaises(NameError):
            ns.deref('a')

        ns.store('a', 123)
        self.assertEqual(ns.deref('a'), 123)
        self.assertEqual(ns['a'], 123)

        ns['a'] = 456
        self.assertEqual(ns.deref('a'), 456)
        self.assertEqual(ns['a'], 456)


    async def test_ref_operations(self):
        """ref operations work on ModuleNamespace"""
        ns = ModuleNamespace("ns", {})
        ns['a'] = 456

        r = ns.ref('a')
        self.assertEqual(r.deref(), 456)

        r.clear()
        with self.assertRaises(NameError):
            ns.deref('a')

        r.store(999)
        self.assertEqual(ns.deref('a'), 999)

        ns.clear('a')
        with self.assertRaises(NameError):
            ns.deref('a')


    async def test_builtins_separate_from_refs(self):
        """Builtins dict is not modified by namespace operations"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace("mod", glbl)

        self.assertEqual(mod.deref('a'), 123)
        mod.store('a', 456)
        self.assertEqual(mod.deref('a'), 456)

        # Original dict unchanged
        self.assertEqual(glbl['a'], 123)


    async def test_nested_with_namespace(self):
        """ModuleNamespace works as parent for Namespace"""
        ns1 = ModuleNamespace("ns1", {})
        ns2 = Namespace("ns2", ns1, ('bar',), (), ('foo',))

        self.assertEqual(ns1.name, "ns1")
        self.assertIsNone(ns1.parent)
        self.assertEqual(ns2.name, "ns2")
        self.assertIs(ns2.parent, ns1)

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))
        self.assertIsNot(ns1.ref('bar'), ns2.ref('bar'))


    async def test_closure_from_module(self):
        """Closures from ModuleNamespace share refs"""
        ns1 = ModuleNamespace("ns1", {})
        ns2 = Namespace("ns2", ns1, ('bar',), (), ('foo',))

        ns2.store('foo', 987)
        self.assertEqual(ns2.deref('foo'), 987)
        self.assertEqual(ns1.deref('foo'), 987)

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))
        ns1['foo'] = 987
        self.assertEqual(ns2['foo'], 987)

        del ns2['foo']
        with self.assertRaises(NameError):
            ns1.deref('foo')


    async def test_recreate_namespace(self):
        """Recreating ModuleNamespace reuses builtins values"""
        data = {'a': 1, 'b': 2}

        mod1 = ModuleNamespace("mod1", data)
        self.assertEqual(mod1.deref('a'), 1)

        ns1 = Namespace("ns1", mod1, ('z',), (), ('a',))
        self.assertEqual(ns1.deref('a'), 1)
        ns1.store('a', 5)
        self.assertEqual(mod1.deref('a'), 5)

        del mod1
        del ns1

        mod2 = ModuleNamespace("mod2", data)
        # New ModuleNamespace sees original builtin value
        self.assertEqual(mod2.deref('a'), 1)


    async def test_default_name(self):
        """ModuleNamespace without name gets default"""
        ns = ModuleNamespace(None, {})
        self.assertEqual(ns.name, "<module>")


class TestCollectorNamespace(unittest.IsolatedAsyncioTestCase):
    """Tests for the CollectorNamespace type"""

    async def test_type(self):
        """CollectorNamespace is a subclass of Namespace"""
        self.assertTrue(issubclass(CollectorNamespace, Namespace))
        self.assertEqual(CollectorNamespace.__name__, "CollectorNamespace")


    async def test_create(self):
        """CollectorNamespace can be created"""
        cn = CollectorNamespace("cn", None, (), ())

        self.assertIs(type(cn), CollectorNamespace)
        self.assertIsInstance(cn, CollectorNamespace)
        self.assertIsInstance(cn, Namespace)


    async def test_blocks_closure(self):
        """CollectorNamespace prevents closures over its refs"""
        pn = Namespace("pn", None, ('a', 'b'))
        pn['a'] = 123
        pn['b'] = "hello"

        cn = CollectorNamespace("cn", pn, ('a',), ('b',))
        cn['a'] = 456
        cn['b'] = "world"

        self.assertEqual(cn['a'], 456)
        self.assertEqual(cn['b'], "world")

        # Child of CollectorNamespace skips cn's refs, finds pn's
        ns = Namespace("ns", cn, (), ('a', 'b'))

        self.assertEqual(ns['a'], 123)
        self.assertEqual(ns['b'], "world")

        # pn's 'a' ref is not cn's 'a' ref
        self.assertIsNot(pn.deref('a'), cn.deref('a'))
        self.assertIsNot(cn.deref('a'), ns.deref('a'))

        # ns's 'a' ref is pn's 'a' ref (skipped cn)
        self.assertIs(pn.deref('a'), ns.deref('a'))


class TestModule(unittest.IsolatedAsyncioTestCase):
    """Tests for the Module type"""

    async def test_create_with_namespace(self):
        """Module can be created with a Namespace"""
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 123
        ns['b'] = "hello"

        m = Module(ns)
        self.assertIsInstance(m, Module)


    async def test_attribute_access(self):
        """Module provides attribute access to namespace"""
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 123
        ns['b'] = "hello"

        m = Module(ns)

        self.assertEqual(m.a, 123)
        self.assertEqual(m.b, "hello")


    async def test_attribute_assignment(self):
        """Module attribute assignment updates namespace"""
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 123
        ns['b'] = "hello"

        m = Module(ns)

        m.a = 456
        self.assertEqual(m.a, 456)
        self.assertEqual(ns['a'], 456)


    async def test_missing_attribute_raises(self):
        """Accessing missing attribute raises AttributeError"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 123

        m = Module(ns)

        with self.assertRaises(AttributeError):
            _ = m.z


    async def test_assign_missing_raises(self):
        """Assigning to missing attribute raises AttributeError"""
        ns = Namespace("ns", None, ('a',))
        ns['a'] = 123

        m = Module(ns)

        with self.assertRaises(AttributeError):
            m.z = None


    async def test_module_name(self):
        """Module __name__ returns namespace name"""
        ns = Namespace("mymod", None, ())
        m = Module(ns)

        self.assertEqual(m.__name__, "mymod")


    async def test_module_dict(self):
        """Module __dict__ returns the namespace"""
        ns = Namespace("mymod", None, ())
        m = Module(ns)

        self.assertIs(m.__dict__, ns)
        self.assertIs(vars(m), ns)


    async def test_module_dir(self):
        """Module dir() returns namespace keys"""
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 1
        ns['b'] = 2

        m = Module(ns)

        d = dir(m)
        self.assertEqual(sorted(d), ['a', 'b'])


    async def test_with_module_namespace(self):
        """Module works with ModuleNamespace"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        self.assertEqual(m.__name__, mod.name)
        self.assertIs(m.__dict__, mod)
        self.assertIs(vars(m), mod)


    async def test_module_namespace_access(self):
        """Module with ModuleNamespace accesses builtins"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        self.assertEqual(m.a, 123)
        self.assertEqual(m.b, "hello")

        m.a = 456
        self.assertEqual(m.a, 456)
        self.assertEqual(mod['a'], 456)

        # Original dict unchanged
        self.assertEqual(glbl['a'], 123)


    async def test_dynamic_attribute(self):
        """Module supports dynamic attribute creation via namespace"""
        glbl = {}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        with self.assertRaises(AttributeError):
            _ = m.z

        # Create ref first via namespace
        r = mod.ref('z')
        self.assertTrue(r.empty)

        m.z = 999
        self.assertFalse(r.empty)
        self.assertEqual(r.deref(), 999)
        self.assertEqual(m.z, 999)


    async def test_dir_after_access(self):
        """dir() reflects accessed bindings"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        # Empty before any access
        self.assertEqual(dir(m), [])

        # Access creates bindings
        _ = m.a
        _ = m.b
        m.z = 999

        d = dir(m)
        self.assertEqual(sorted(d), ['a', 'b', 'z'])


    async def test_requires_namespace(self):
        """Module requires a Namespace instance"""
        with self.assertRaises(TypeError):
            Module("not a namespace")

        with self.assertRaises(TypeError):
            Module({})


# The end.
