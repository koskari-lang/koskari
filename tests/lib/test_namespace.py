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

import pytest

from koskari.lib import (
    CollectorNamespace, Module, ModuleNamespace, Namespace, NamespaceSpec, Ref)


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
        with self.assertRaises(KeyError):
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
        with self.assertRaises(KeyError):
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
        # the copy carries the binding for y, but not child's value
        self.assertIn('y', cp.refs)
        self.assertNotIn('y', cp)
        with self.assertRaises(KeyError):
            _ = cp['y']

        cp['y'] = 99
        self.assertEqual(cp['y'], 99)
        self.assertEqual(child['y'], 42)


    async def test_wide_scope_slots_are_unbounded(self):
        """
        Every declared name has a fast slot, past 128 included; the cap
        used to truncate the slot table and lose the names beyond it
        """
        names = tuple(f"v{i}" for i in range(200))
        spec = NamespaceSpec("wide", names, ())
        ns = spec.get_namespace(None)
        self.assertEqual(len(ns.refs), 200)
        self.assertEqual(len(ns), 0)
        ns["v0"] = 1
        ns["v127"] = 2
        ns["v128"] = 3
        ns["v199"] = 4
        self.assertEqual((ns["v0"], ns["v127"], ns["v128"], ns["v199"]),
                         (1, 2, 3, 4))
        with self.assertRaises(NameError):
            ns.deref("v150")

        # the Python constructor path accepts the same width
        wide = Namespace("wide", None, names)
        wide["v199"] = 9
        self.assertEqual(wide["v199"], 9)

    async def test_wide_scope_closures_land_after_the_locals(self):
        """
        Closure refs follow the locals in slot order even when the locals
        alone pass 128; they used to be written past the allocation
        """
        parent = Namespace("parent", None, ("a", "b", "c"))
        parent["a"], parent["b"], parent["c"] = 10, 20, 30
        locals_ = tuple(f"v{i}" for i in range(130))
        spec = NamespaceSpec("child", locals_, ("a", "b", "c"))
        child = spec.get_namespace(parent)
        self.assertEqual(len(child.refs), 133)
        # only the three closures hold values so far
        self.assertEqual(len(child), 3)
        self.assertEqual((child["a"], child["b"], child["c"]), (10, 20, 30))
        child["v129"] = 7
        self.assertEqual(child["v129"], 7)
        child["c"] = 31
        self.assertEqual(parent["c"], 31)

    async def test_copy_returns_single_reference(self):
        """copy() hands back exactly one reference to the new namespace"""
        import sys
        ns = Namespace("ns", None, ('a',))
        base = sys.getrefcount(ns)
        cp = ns.copy()
        self.assertEqual(sys.getrefcount(cp), base)


    async def test_constructor_failure_raises(self):
        """
        A Namespace constructor that cannot resolve its closure names
        raises instead of crashing
        """
        with self.assertRaises(NameError):
            Namespace("n", None, (), ("nope",))


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
        with self.assertRaises(KeyError):
            _ = ns1['bar']
        with self.assertRaises(NameError):
            ns1.store('bar', 123)

        with self.assertRaises(NameError):
            ns2.deref('bar')
        with self.assertRaises(KeyError):
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

        # bound, though nothing holds a value yet
        self.assertIn('foo', ns1.refs)
        self.assertIn('bar', ns1.refs)
        self.assertNotIn('baz', ns1.refs)
        self.assertIn('qux', ns1.refs)
        self.assertEqual(len(ns1), 0)

        self.assertTrue(ns2.raw('baz'))

        self.assertIn('foo', ns2.refs)
        self.assertNotIn('bar', ns2.refs)
        self.assertIn('baz', ns2.refs)
        self.assertNotIn('qux', ns2.refs)

        self.assertIs(ns1.ref('foo'), ns2.ref('foo'))

        self.assertIs(ns2.find('foo'), ns2.ref('foo'))
        self.assertIs(ns2.find('bar'), ns1.ref('bar'))
        self.assertIs(ns2.find('baz'), ns2.ref('baz'))
        self.assertIs(ns2.find('qux'), ns1.ref('qux'))


    async def test_keys(self):
        """keys() lists the names holding a value; refs lists every binding"""
        ns = Namespace("ns", None, ('a', 'b', 'c'))
        self.assertEqual(ns.keys(), [])
        self.assertEqual(sorted(ns.refs.keys()), ['a', 'b', 'c'])

        ns['b'] = 2
        self.assertEqual(ns.keys(), ['b'])
        self.assertEqual(sorted(ns.refs.keys()), ['a', 'b', 'c'])


    async def test_items(self):
        """items() returns (name, value) pairs for the names holding a value"""
        ns = Namespace("ns", None, ('a', 'b', 'c'))
        ns['a'] = 1
        ns['b'] = 2
        items = ns.items()
        self.assertEqual(sorted(items), [('a', 1), ('b', 2)])
        self.assertEqual(sorted(ns.values()), [1, 2])
        self.assertEqual(sorted(ns), ['a', 'b'])


    async def test_bound(self):
        """bound() returns True if name is bound"""
        ns = Namespace("ns", None, ('a',))
        self.assertTrue(ns.bound('a'))
        self.assertEqual(ns.bound('missing'), 0)


    async def test_len(self):
        """len() counts the names holding a value; refs counts bindings"""
        ns = Namespace("ns", None, ('a', 'b', 'c'))
        self.assertEqual(len(ns), 0)
        self.assertEqual(len(ns.refs), 3)

        ns['a'] = 1
        self.assertEqual(len(ns), 1)
        self.assertEqual(len(ns.refs), 3)


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


    async def test_ref_rejects_non_name_key(self):
        """
        A ref request with a key that is neither str nor symbol raises
        TypeError and leaves no binding behind
        """

        ns = ModuleNamespace("m", {})
        with self.assertRaises(TypeError):
            ns.ref(123)
        self.assertNotIn(123, ns)
        self.assertEqual(list(ns.keys()), [])


    async def test_create(self):
        """ModuleNamespace can be created"""
        mn = ModuleNamespace("mn", {})

        self.assertIs(type(mn), ModuleNamespace)
        self.assertIsInstance(mn, ModuleNamespace)
        self.assertIsInstance(mn, Namespace)


    async def test_copy_rejected(self):
        """
        copy() has no fast slots to duplicate on a ModuleNamespace and must
        raise TypeError rather than track a NULL result
        """
        mn = ModuleNamespace("mn", {})
        with self.assertRaises(TypeError):
            mn.copy()


    async def test_name_and_parent(self):
        """ModuleNamespace has name and no parent"""
        ns = ModuleNamespace("ns", {})
        self.assertEqual(ns.name, "ns")
        self.assertIsNone(ns.parent)

    async def test_builtins_fallback(self):
        """resolve() falls through to builtins; the module's own reads do not"""
        ns = ModuleNamespace("ns", {'b': 987})
        self.assertEqual(ns.resolve('b'), 987)
        self.assertEqual(ns.builtins['b'], 987)

        with self.assertRaises(NameError):
            ns.deref('b')
        with self.assertRaises(KeyError):
            _ = ns['b']
        self.assertIsNone(ns.get('b'))
        self.assertNotIn('b', ns)


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

        self.assertEqual(mod.resolve('a'), 123)
        mod.store('a', 456)
        self.assertEqual(mod.deref('a'), 456)
        self.assertEqual(mod.resolve('a'), 456)

        # Original dict unchanged
        self.assertEqual(glbl['a'], 123)
        self.assertEqual(mod.builtins['a'], 123)


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
        self.assertEqual(mod1.resolve('a'), 1)

        ns1 = Namespace("ns1", mod1, ('z',), (), ('a',))
        self.assertEqual(ns1.deref('a'), 1)
        ns1.store('a', 5)
        self.assertEqual(mod1.deref('a'), 5)

        del mod1
        del ns1

        mod2 = ModuleNamespace("mod2", data)
        # New ModuleNamespace sees original builtin value
        self.assertEqual(mod2.resolve('a'), 1)


    async def test_default_name(self):
        """ModuleNamespace without name gets default"""
        ns = ModuleNamespace(None, {})
        self.assertEqual(ns.name, "<module>")


    async def test_len_empty(self):
        """Empty ModuleNamespace has len 0 and is falsey"""
        ns = ModuleNamespace("ns", {})
        self.assertEqual(len(ns), 0)
        self.assertFalse(ns)


    async def test_len_after_store(self):
        """len and truthiness track binds after store"""
        ns = ModuleNamespace("ns", {})
        ns.store('a', 1)
        ns.store('b', 2)
        self.assertEqual(len(ns), 2)
        self.assertTrue(ns)


    async def test_len_builtins_not_counted(self):
        """Builtins never contribute to len, before or after a read"""
        ns = ModuleNamespace("ns", {'x': 1})
        self.assertEqual(len(ns), 0)
        self.assertFalse(ns)
        self.assertEqual(list(ns.keys()), [])

        self.assertEqual(ns.resolve('x'), 1)
        self.assertEqual(len(ns), 0)
        self.assertFalse(ns)
        self.assertEqual(list(ns.keys()), [])
        self.assertEqual(len(ns.refs), 0)


    async def test_value_reads_do_not_bind(self):
        """resolve reaches a builtin; the module's own reads miss it; none bind"""
        ns = ModuleNamespace("ns", {'x': 1})

        self.assertEqual(ns.resolve('x'), 1)
        self.assertEqual(ns.resolve('zzz', 5), 5)
        with self.assertRaises(NameError):
            ns.deref('x')
        with self.assertRaises(KeyError):
            _ = ns['x']
        self.assertIsNone(ns.get('x'))
        self.assertEqual(ns.get('zzz', 5), 5)

        self.assertEqual(len(ns), 0)
        self.assertEqual(len(ns.refs), 0)
        self.assertEqual(list(ns.keys()), [])


    async def test_missing_reads_do_not_bind(self):
        """A NameError on a miss leaves no empty binding behind"""
        ns = ModuleNamespace("ns", {})

        with self.assertRaises(NameError):
            ns.deref('zzz')
        with self.assertRaises(KeyError):
            ns['zzz']
        with self.assertRaises(NameError):
            ns.resolve('zzz')

        self.assertEqual(len(ns), 0)
        self.assertEqual(len(ns.refs), 0)
        self.assertNotIn('zzz', ns)
        self.assertNotIn('zzz', ns.refs)


    async def test_contains_does_not_bind(self):
        """Membership means a value held here; builtins are not members"""
        ns = ModuleNamespace("ns", {'x': 1})

        self.assertNotIn('x', ns)
        self.assertIn('x', ns.builtins)
        self.assertNotIn('zzz', ns)
        self.assertNotIn('zzz', ns)
        self.assertEqual(len(ns), 0)
        self.assertEqual(len(ns.refs), 0)

        ns.store('a', 1)
        self.assertIn('a', ns)
        self.assertEqual(len(ns), 1)


    async def test_ref_request_binds_and_retains(self):
        """ref() creates an empty binding on a miss and keeps it"""
        ns = ModuleNamespace("ns", {})

        r = ns.ref('zzz')
        self.assertIn('zzz', ns.refs)
        self.assertIs(ns.refs['zzz'], r)
        self.assertTrue(ns.bound('zzz'))
        self.assertEqual(len(ns.refs), 1)
        # bound, but holding nothing, so not yet in the mapping
        self.assertNotIn('zzz', ns)
        self.assertEqual(len(ns), 0)
        with self.assertRaises(NameError):
            ns.deref('zzz')

        r.store(7)
        self.assertIn('zzz', ns)
        self.assertEqual(len(ns), 1)
        self.assertEqual(ns.deref('zzz'), 7)
        self.assertIs(ns.ref('zzz'), r)


    async def test_child_resolve_reaches_builtins(self):
        """resolve() on a child walks to the module and its builtins; get() stays local"""
        mod = ModuleNamespace("mod", {'x': 1})
        mod['m'] = 2
        ns = Namespace("ns", mod, ('a',), (), ())
        ns['a'] = 3

        self.assertEqual(ns.resolve('a'), 3)
        self.assertEqual(ns.resolve('m'), 2)
        self.assertEqual(ns.resolve('x'), 1)
        self.assertEqual(ns.resolve('zzz', 5), 5)
        with self.assertRaises(NameError):
            ns.resolve('zzz')

        self.assertEqual(ns.get('a'), 3)
        self.assertIsNone(ns.get('m'))
        self.assertIsNone(ns.get('x'))
        self.assertEqual(ns.get('zzz', 5), 5)
        self.assertEqual(len(mod), 1)


class TestNamespaceTiers(unittest.IsolatedAsyncioTestCase):
    """
    The mapping protocol answers at the value tier, refs at the binding
    tier, builtins at the builtins tier, and resolve across them
    """

    async def test_mapping_delete_empties_and_keeps_binding(self):
        ns = ModuleNamespace("ns", {})
        ns['a'] = 1
        r = ns.ref('a')

        del ns['a']
        self.assertNotIn('a', ns)
        self.assertIn('a', ns.refs)
        self.assertIs(ns.refs['a'], r)
        self.assertTrue(r.empty)

        with self.assertRaises(KeyError):
            del ns['never']


    async def test_mapping_iteration_and_views(self):
        ns = ModuleNamespace("ns", {'blt': 0})
        ns['a'] = 1
        ns['b'] = 2
        ns.ref('c')

        self.assertEqual(sorted(ns), ['a', 'b'])
        self.assertEqual(sorted(ns.keys()), ['a', 'b'])
        self.assertEqual(sorted(ns.values()), [1, 2])
        self.assertEqual(sorted(ns.items()), [('a', 1), ('b', 2)])
        self.assertEqual(dict(ns), {'a': 1, 'b': 2})
        self.assertEqual(len(ns), 2)


    async def test_refs_view(self):
        ns = Namespace("ns", None, ('a', 'b'))
        ns['a'] = 1
        refs = ns.refs

        self.assertEqual(sorted(refs), ['a', 'b'])
        self.assertEqual(sorted(refs.keys()), ['a', 'b'])
        self.assertEqual(len(refs), 2)
        self.assertIn('b', refs)
        self.assertNotIn('zzz', refs)
        with self.assertRaises(KeyError):
            _ = refs['zzz']

        self.assertIsInstance(refs['a'], Ref)
        self.assertEqual(refs['a'].deref(), 1)
        self.assertTrue(refs['b'].empty)
        for name, ref in refs.items():
            self.assertIs(ref, ns.ref(name))
        self.assertEqual(len(refs.values()), 2)

        # the view is live
        ns['b'] = 2
        self.assertFalse(refs['b'].empty)


    async def test_builtins_view_is_read_only(self):
        blt = {'x': 1}
        ns = ModuleNamespace("ns", blt)
        self.assertEqual(ns.builtins['x'], 1)
        with self.assertRaises(TypeError):
            ns.builtins['y'] = 2
        self.assertEqual(blt, {'x': 1})


    async def test_bound_is_strict(self):
        ns = ModuleNamespace("ns", {'x': 1})
        self.assertFalse(ns.bound('x'))
        self.assertFalse(ns.bound('zzz'))
        ns.ref('zzz')
        self.assertTrue(ns.bound('zzz'))


    async def test_module_mapping_get_is_local(self):
        ns = ModuleNamespace("ns", {'x': 1})
        self.assertIsNone(ns.get('x'))
        self.assertEqual(ns.get('x', 5), 5)
        ns['x'] = 2
        self.assertEqual(ns.get('x'), 2)
        self.assertEqual(ns.resolve('x'), 2)
        del ns['x']
        self.assertIsNone(ns.get('x'))
        # an emptied module binding shadows the builtin for resolution
        with self.assertRaises(NameError):
            ns.resolve('x')


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

        # the mapping ranges over what the body declared; the captured
        # b is reachable through the namespace, not the mapping
        self.assertEqual(cn['a'], 456)
        self.assertEqual(cn.deref('b'), "world")
        self.assertNotIn('b', cn)
        self.assertIn('b', cn.refs)
        with self.assertRaises(KeyError):
            _ = cn['b']

        # Child of CollectorNamespace skips cn's refs, finds pn's
        ns = Namespace("ns", cn, (), ('a', 'b'))

        self.assertEqual(ns['a'], 123)
        self.assertEqual(ns['b'], "world")

        # pn's 'a' ref is not cn's 'a' ref
        self.assertIsNot(pn.deref('a'), cn.deref('a'))
        self.assertIsNot(cn.deref('a'), ns.deref('a'))

        # ns's 'a' ref is pn's 'a' ref (skipped cn)
        self.assertIs(pn.deref('a'), ns.deref('a'))


    async def test_gathered_syms_readable_not_exported(self):
        """
        Closure syms stay in binds for evaluation but are omitted from keys()
        and dict() so BUILD_CLASS does not export them as class attributes
        """

        pn = Namespace("pn", None, ('int',), ())
        pn['int'] = int

        cn = CollectorNamespace("cn", pn, ('foo',), ('int',))
        cn['foo'] = 1

        self.assertIs(cn.deref('int'), int)
        self.assertIn('foo', list(cn.keys()))
        self.assertNotIn('int', list(cn.keys()))
        self.assertEqual(dict(cn), {'foo': 1})


    async def test_ref_falls_through_past_body_declaration(self):
        """
        A ref request on a collector resolves from its parent; the body's
        own declaration of the name neither answers nor shadows it, and
        the body's binding still reaches the class dict
        """

        pn = Namespace("pn", None, ('x',), ())
        pn['x'] = 5

        cn = CollectorNamespace("cn", pn, ('x',), ())
        cn['x'] = 1

        self.assertIs(cn.ref('x'), pn.ref('x'))
        self.assertEqual(cn['x'], 1)
        self.assertEqual(dict(cn), {'x': 1})


    async def test_ref_with_no_enclosing_binding_raises(self):
        """
        A ref request for a name only the body declares has nothing to
        fall through to
        """

        cn = CollectorNamespace("cn", None, ('y',), ())
        cn['y'] = 1

        with self.assertRaises(NameError):
            cn.ref('y')
        self.assertEqual(dict(cn), {'y': 1})


    async def test_class_cell_ref_is_the_collectors_own(self):
        """
        __class__ is the one body cell that outlives the body, and a ref
        request for it answers from the collector
        """

        cn = CollectorNamespace("cn", None, ('__class__',), ())
        cn['__class__'] = 'k'

        self.assertEqual(cn.ref('__class__').deref(), 'k')


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


    async def test_module_ns_attribute_reads_do_not_bind(self):
        """Module attribute reads over a ModuleNamespace never bind"""
        ns = ModuleNamespace("ns", {'x': 1})
        m = Module(ns)

        # a builtin is not an attribute of the module
        with self.assertRaises(AttributeError):
            _ = m.x
        with self.assertRaises(AttributeError):
            _ = m.zzz

        self.assertEqual(len(ns), 0)
        self.assertEqual(len(ns.refs), 0)
        self.assertNotIn('zzz', ns)

        m.zzz = 2
        self.assertEqual(ns.deref('zzz'), 2)
        self.assertEqual(len(ns), 1)


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
        """Module attributes are the module's own values, never its builtins"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        with self.assertRaises(AttributeError):
            _ = m.a
        self.assertEqual(mod.resolve('a'), 123)

        m.a = 456
        self.assertEqual(m.a, 456)
        self.assertEqual(mod['a'], 456)
        with self.assertRaises(AttributeError):
            _ = m.b

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
        """dir() lists the names holding a value; failed reads bind nothing"""
        glbl = {'a': 123, 'b': "hello"}
        mod = ModuleNamespace('mod', glbl)

        m = Module(mod)

        self.assertEqual(dir(m), [])

        with self.assertRaises(AttributeError):
            _ = m.a
        self.assertEqual(dir(m), [])
        self.assertEqual(len(mod.refs), 0)

        m.z = 999
        self.assertEqual(dir(m), ['z'])

        # an emptied binding drops out of dir but stays bound
        del mod['z']
        self.assertEqual(dir(m), [])
        self.assertIn('z', mod.refs)


    async def test_requires_namespace(self):
        """Module requires a Namespace instance"""
        with self.assertRaises(TypeError):
            Module("not a namespace")

        with self.assertRaises(TypeError):
            Module({})


# The end.
