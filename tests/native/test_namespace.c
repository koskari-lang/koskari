/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   Memory tests for namespace types

   Tests reference counting, binding management, and namespace
   hierarchy for Namespace, NamespaceSpec, CollectorNamespace,
   ModuleNamespace, and Module types.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === NamespaceSpec tests === */


static int test_nspec_create(void) {
    /*
      Test that NamespaceSpec can be created.
    */
    PyObject *name = PyUnicode_FromString("test");
    PyObject *local = PyTuple_Pack(2,
                                   PyUnicode_FromString("a"),
                                   PyUnicode_FromString("b"));
    PyObject *closure = PyTuple_Pack(1, PyUnicode_FromString("z"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local_syms");
    TEST_ASSERT_NOT_NULL(closure, "create closure_syms");

    PyObject *spec = KosNamespaceSpec_New(name, local, closure, NULL, NULL);
    TEST_ASSERT_NOT_NULL(spec, "create NamespaceSpec");
    TEST_ASSERT(KosNamespaceSpec_CheckExact(spec), "spec type check");

    /* Verify contents */
    TEST_ASSERT_SAME(KosNamespaceSpec_NAME(spec), name, "name stored");
    TEST_ASSERT_NOT_NULL(KosNamespaceSpec_LOCAL_SYMS(spec), "local_syms stored");
    TEST_ASSERT_NOT_NULL(KosNamespaceSpec_CLOSURE_SYMS(spec), "closure_syms stored");

    Py_DECREF(spec);
    Py_DECREF(closure);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_nspec_none_values(void) {
    /*
      Test NamespaceSpec with None values.
    */
    PyObject *spec = KosNamespaceSpec_New(Py_None, Py_None, Py_None, NULL, NULL);
    TEST_ASSERT_NOT_NULL(spec, "create NamespaceSpec with None");

    /* None values become NULL internally */
    TEST_ASSERT_NULL(KosNamespaceSpec_NAME(spec), "name is NULL");
    TEST_ASSERT_NULL(KosNamespaceSpec_LOCAL_SYMS(spec), "local_syms is NULL");
    TEST_ASSERT_NULL(KosNamespaceSpec_CLOSURE_SYMS(spec), "closure_syms is NULL");

    Py_DECREF(spec);
    return 0;
}


static int test_nspec_refcount(void) {
    /*
      Test that NamespaceSpec properly manages refcounts.
    */
    PyObject *name = PyUnicode_FromString("reftest");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *closure = PyTuple_New(0);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(closure, "create closure");

    Py_ssize_t name_initial = Py_REFCNT(name);

    PyObject *spec = KosNamespaceSpec_New(name, local, closure, NULL, NULL);
    TEST_ASSERT_NOT_NULL(spec, "create spec");

    /* Name refcount should be incremented */
    TEST_ASSERT_EQ(Py_REFCNT(name), name_initial + 1, "name refcount incremented");

    /* Destroy spec - should decrement */
    Py_DECREF(spec);
    TEST_ASSERT_EQ(Py_REFCNT(name), name_initial, "name refcount restored");

    Py_DECREF(closure);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


/* === Namespace tests === */


static int test_namespace_create(void) {
    /*
      Test that Namespace can be created with local symbols.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(2,
                                   PyUnicode_FromString("a"),
                                   PyUnicode_FromString("b"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");
    TEST_ASSERT(KosNamespace_CheckExact(ns), "namespace type check");

    /* Verify properties */
    TEST_ASSERT_SAME(KosNamespace_NAME(ns), name, "name stored");
    TEST_ASSERT_NULL(KosNamespace_PARENT(ns), "no parent");
    TEST_ASSERT_EQ(KosNamespace_FAST_COUNT(ns), 2, "fast count is 2");

    Py_DECREF(ns);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_store_deref(void) {
    /*
      Test storing and dereferencing values in a namespace.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *key = PyUnicode_FromString("x");
    PyObject *value = PyLong_FromLong(12345);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(value, "create value");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    /* Store value */
    int result = KosNamespace_Store(ns, key, value);
    TEST_ASSERT_EQ(result, 0, "store succeeded");

    /* Deref value */
    PyObject *retrieved = KosNamespace_Deref(ns, key);
    TEST_ASSERT_NOT_NULL(retrieved, "deref succeeded");
    TEST_ASSERT_EQ(PyLong_AsLong(retrieved), 12345, "value matches");

    Py_DECREF(retrieved);
    Py_DECREF(ns);
    Py_DECREF(value);
    Py_DECREF(key);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_store_refcount(void) {
    /*
      Test that namespace store properly manages value refcounts.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *key = PyUnicode_FromString("x");
    PyObject *value = PyLong_FromLong(99999);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(value, "create value");

    Py_ssize_t val_initial = Py_REFCNT(value);

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    /* Store increments refcount (raw mode) */
    KosNamespace_Store(ns, key, value);
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial + 1, "value refcount incremented");

    /* Clear decrements refcount */
    KosNamespace_Clear(ns, key);
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial, "value refcount restored");

    Py_DECREF(ns);
    Py_DECREF(value);
    Py_DECREF(key);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_ref(void) {
    /*
      Test getting a reference from a namespace.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *key = PyUnicode_FromString("x");
    PyObject *value = PyLong_FromLong(42);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(value, "create value");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    /* Store value */
    KosNamespace_Store(ns, key, value);

    /* Get reference - converts raw to ref */
    PyObject *ref = KosNamespace_Ref(ns, key);
    TEST_ASSERT_NOT_NULL(ref, "get ref");
    TEST_ASSERT(KosRef_Check(ref), "ref is a Ref");

    /* Deref the ref */
    PyObject *retrieved = KosRef_Deref(ref);
    TEST_ASSERT_NOT_NULL(retrieved, "deref ref");
    TEST_ASSERT_EQ(PyLong_AsLong(retrieved), 42, "value matches");

    Py_DECREF(retrieved);
    Py_DECREF(ns);
    Py_DECREF(value);
    Py_DECREF(key);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_bind(void) {
    /*
      Test binding a ref to a namespace.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *key = PyUnicode_FromString("x");
    PyObject *value = PyLong_FromLong(999);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(value, "create value");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    /* Create a ref */
    PyObject *ref = KosSymbolicRef_New(key, value);
    TEST_ASSERT_NOT_NULL(ref, "create ref");

    /* Bind the ref */
    int result = KosNamespace_Bind(ns, key, ref);
    TEST_ASSERT_EQ(result, 0, "bind succeeded");

    /* Get the ref back */
    PyObject *bound = KosNamespace_Ref(ns, key);
    TEST_ASSERT_SAME(bound, ref, "bound ref is same");

    Py_DECREF(ref);
    Py_DECREF(ns);
    Py_DECREF(value);
    Py_DECREF(key);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_nested(void) {
    /*
      Test nested namespaces with closures.
    */
    PyObject *name1 = PyUnicode_FromString("parent");
    PyObject *name2 = PyUnicode_FromString("child");
    PyObject *s_foo = PyUnicode_FromString("foo");
    PyObject *s_bar = PyUnicode_FromString("bar");
    PyObject *local1 = NULL;
    PyObject *local2 = NULL;
    PyObject *closure2 = NULL;

    if (s_foo)
        local1 = PyTuple_Pack(1, s_foo);
    if (s_bar)
        local2 = PyTuple_Pack(1, s_bar);
    if (s_foo)
        closure2 = PyTuple_Pack(1, s_foo);
    Py_XDECREF(s_foo);
    Py_XDECREF(s_bar);

    TEST_ASSERT_NOT_NULL(name1, "create name1");
    TEST_ASSERT_NOT_NULL(name2, "create name2");
    TEST_ASSERT_NOT_NULL(local1, "create local1");
    TEST_ASSERT_NOT_NULL(local2, "create local2");
    TEST_ASSERT_NOT_NULL(closure2, "create closure2");

    /* Create parent namespace */
    PyObject *ns1 = KosNamespace_NewFull(name1, NULL, local1, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns1, "create parent namespace");

    /* Store value in parent */
    PyObject *key_foo = PyUnicode_FromString("foo");
    PyObject *value = PyLong_FromLong(123);
    KosNamespace_Store(ns1, key_foo, value);

    /* Create child namespace with closure over foo */
    PyObject *ns2 = KosNamespace_NewFull(name2, ns1, local2, closure2, NULL);
    TEST_ASSERT_NOT_NULL(ns2, "create child namespace");

    /* Parent should be set */
    TEST_ASSERT_SAME(KosNamespace_PARENT(ns2), ns1, "parent is ns1");

    /* Child should share foo ref with parent */
    PyObject *ref1 = KosNamespace_REF(ns1, key_foo);
    PyObject *ref2 = KosNamespace_REF(ns2, key_foo);
    TEST_ASSERT_NOT_NULL(ref1, "parent has foo ref");
    TEST_ASSERT_NOT_NULL(ref2, "child has foo ref");
    TEST_ASSERT_SAME(ref1, ref2, "refs are same object");

    Py_DECREF(ns2);
    Py_DECREF(ns1);
    Py_DECREF(value);
    Py_DECREF(key_foo);
    Py_DECREF(closure2);
    Py_DECREF(local2);
    Py_DECREF(local1);
    Py_DECREF(name2);
    Py_DECREF(name1);
    return 0;
}


static int test_namespace_fast_index(void) {
    /*
      Test fast index lookup.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(3,
                                   PyUnicode_FromString("a"),
                                   PyUnicode_FromString("b"),
                                   PyUnicode_FromString("c"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    PyObject *key_a = PyUnicode_FromString("a");
    PyObject *key_b = PyUnicode_FromString("b");
    PyObject *key_c = PyUnicode_FromString("c");
    PyObject *key_d = PyUnicode_FromString("d");

    int idx_a = KosNamespace_FastIndex(ns, key_a);
    int idx_b = KosNamespace_FastIndex(ns, key_b);
    int idx_c = KosNamespace_FastIndex(ns, key_c);
    int idx_d = KosNamespace_FastIndex(ns, key_d);

    TEST_ASSERT_EQ(idx_a, 0, "a is at index 0");
    TEST_ASSERT_EQ(idx_b, 1, "b is at index 1");
    TEST_ASSERT_EQ(idx_c, 2, "c is at index 2");
    TEST_ASSERT_EQ(idx_d, -1, "d not found");

    Py_DECREF(key_d);
    Py_DECREF(key_c);
    Py_DECREF(key_b);
    Py_DECREF(key_a);
    Py_DECREF(ns);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_namespace_fast_store_deref(void) {
    /*
      Test fast store and deref operations.
    */
    PyObject *name = PyUnicode_FromString("ns");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));
    PyObject *value = PyLong_FromLong(777);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(value, "create value");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    /* Fast store at index 0 */
    int result = KosNamespace_FastStore(ns, 0, value);
    TEST_ASSERT_EQ(result, 0, "fast store succeeded");

    /* Fast deref at index 0 */
    PyObject *retrieved = KosNamespace_FastDeref(ns, 0);
    TEST_ASSERT_NOT_NULL(retrieved, "fast deref succeeded");
    TEST_ASSERT_EQ(PyLong_AsLong(retrieved), 777, "value matches");

    Py_DECREF(retrieved);
    Py_DECREF(ns);
    Py_DECREF(value);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


/* === CollectorNamespace tests === */


static int test_collector_create(void) {
    /*
      Test that CollectorNamespace can be created.
    */
    PyObject *name = PyUnicode_FromString("collector");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");

    PyObject *cn = KosCollectorNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(cn, "create CollectorNamespace");
    TEST_ASSERT(KosCollectorNamespace_CheckExact(cn), "collector type check");
    TEST_ASSERT(KosNamespace_Check(cn), "is a Namespace");

    Py_DECREF(cn);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_collector_blocks_closure(void) {
    /*
      Test that CollectorNamespace blocks closure lookups over its LOCAL bindings.
      Setup: parent(foo) -> collector(foo as local) -> child(foo as closure)
      Child should skip collector's foo and find parent's foo.
    */
    PyObject *name_p = PyUnicode_FromString("parent");
    PyObject *name_c = PyUnicode_FromString("collector");
    PyObject *name_n = PyUnicode_FromString("child");
    PyObject *s_foo_p = PyUnicode_FromString("foo");
    PyObject *s_foo_c = PyUnicode_FromString("foo");
    PyObject *s_foo_n = PyUnicode_FromString("foo");
    PyObject *local_p = NULL;
    PyObject *local_c = NULL;
    PyObject *closure_n = NULL;

    if (s_foo_p)
        local_p = PyTuple_Pack(1, s_foo_p);
    if (s_foo_c)
        local_c = PyTuple_Pack(1, s_foo_c);
    if (s_foo_n)
        closure_n = PyTuple_Pack(1, s_foo_n);
    Py_XDECREF(s_foo_p);
    Py_XDECREF(s_foo_c);
    Py_XDECREF(s_foo_n);

    TEST_ASSERT_NOT_NULL(name_p, "create name_p");
    TEST_ASSERT_NOT_NULL(name_c, "create name_c");
    TEST_ASSERT_NOT_NULL(name_n, "create name_n");
    TEST_ASSERT_NOT_NULL(local_p, "create local_p");
    TEST_ASSERT_NOT_NULL(local_c, "create local_c");
    TEST_ASSERT_NOT_NULL(closure_n, "create closure_n");

    /* Create parent with local foo */
    PyObject *parent = KosNamespace_NewFull(name_p, NULL, local_p, NULL, NULL);
    TEST_ASSERT_NOT_NULL(parent, "create parent");

    PyObject *key_foo = PyUnicode_FromString("foo");
    PyObject *value_p = PyLong_FromLong(123);
    KosNamespace_Store(parent, key_foo, value_p);

    /* Create collector with its own local foo (not closure) */
    PyObject *collector = KosCollectorNamespace_NewFull(name_c, parent, local_c, NULL, NULL);
    TEST_ASSERT_NOT_NULL(collector, "create collector");

    PyObject *value_c = PyLong_FromLong(456);
    KosNamespace_Store(collector, key_foo, value_c);

    /* Child takes closure over foo - should skip collector's local and find parent's */
    PyObject *child = KosNamespace_NewFull(name_n, collector, NULL, closure_n, NULL);
    TEST_ASSERT_NOT_NULL(child, "create child");

    /* Parent's foo ref and child's foo ref should be same (collector's local skipped) */
    PyObject *ref_p = KosNamespace_REF(parent, key_foo);
    PyObject *ref_child = KosNamespace_REF(child, key_foo);
    TEST_ASSERT_NOT_NULL(ref_p, "parent has foo");
    TEST_ASSERT_NOT_NULL(ref_child, "child has foo");
    TEST_ASSERT_SAME(ref_p, ref_child, "child refs parent's foo (collector skipped)");

    /* Collector's foo ref should be different (it's a local binding) */
    PyObject *ref_col = KosNamespace_REF(collector, key_foo);
    TEST_ASSERT_NOT_NULL(ref_col, "collector has foo");
    TEST_ASSERT_NOT_SAME(ref_col, ref_p, "collector foo is different from parent");

    /* Verify values are correct */
    PyObject *val_from_child = KosRef_Deref(ref_child);
    PyObject *val_from_col = KosRef_Deref(ref_col);
    TEST_ASSERT_EQ(PyLong_AsLong(val_from_child), 123, "child sees parent value");
    TEST_ASSERT_EQ(PyLong_AsLong(val_from_col), 456, "collector has its own value");

    Py_DECREF(val_from_col);
    Py_DECREF(val_from_child);
    Py_DECREF(child);
    Py_DECREF(collector);
    Py_DECREF(parent);
    Py_DECREF(value_c);
    Py_DECREF(value_p);
    Py_DECREF(key_foo);
    Py_DECREF(closure_n);
    Py_DECREF(local_c);
    Py_DECREF(local_p);
    Py_DECREF(name_n);
    Py_DECREF(name_c);
    Py_DECREF(name_p);
    return 0;
}


/* === ModuleNamespace tests === */


static int test_modns_create(void) {
    /*
      Test that ModuleNamespace can be created.
    */
    PyObject *name = PyUnicode_FromString("modns");
    PyObject *builtins = PyDict_New();

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(builtins, "create builtins");

    PyObject *modns = KosModuleNamespace_New(name, builtins);
    TEST_ASSERT_NOT_NULL(modns, "create ModuleNamespace");
    TEST_ASSERT(KosModuleNamespace_CheckExact(modns), "modns type check");
    TEST_ASSERT(KosNamespace_Check(modns), "is a Namespace");

    Py_DECREF(modns);
    Py_DECREF(builtins);
    Py_DECREF(name);
    return 0;
}


static int test_modns_builtins_fallback(void) {
    /*
      Test that ModuleNamespace falls back to builtins.
    */
    PyObject *name = PyUnicode_FromString("modns");
    PyObject *builtins = PyDict_New();
    PyObject *key = PyUnicode_FromString("x");
    PyObject *builtin_val = PyLong_FromLong(999);

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(builtins, "create builtins");
    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(builtin_val, "create builtin_val");

    /* Add to builtins */
    PyDict_SetItem(builtins, key, builtin_val);

    PyObject *modns = KosModuleNamespace_New(name, builtins);
    TEST_ASSERT_NOT_NULL(modns, "create ModuleNamespace");

    /* Access should create a ref with builtin value */
    PyObject *retrieved = KosNamespace_Deref(modns, key);
    TEST_ASSERT_NOT_NULL(retrieved, "deref succeeded");
    TEST_ASSERT_EQ(PyLong_AsLong(retrieved), 999, "builtin value");

    Py_DECREF(retrieved);
    Py_DECREF(modns);
    Py_DECREF(builtin_val);
    Py_DECREF(key);
    Py_DECREF(builtins);
    Py_DECREF(name);
    return 0;
}


static int test_modns_refcount(void) {
    /*
      Test that ModuleNamespace manages builtins refcount.
    */
    PyObject *name = PyUnicode_FromString("modns");
    PyObject *builtins = PyDict_New();

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(builtins, "create builtins");

    Py_ssize_t blt_initial = Py_REFCNT(builtins);

    PyObject *modns = KosModuleNamespace_New(name, builtins);
    TEST_ASSERT_NOT_NULL(modns, "create ModuleNamespace");

    TEST_ASSERT_EQ(Py_REFCNT(builtins), blt_initial + 1, "builtins refcount incremented");

    Py_DECREF(modns);
    TEST_ASSERT_EQ(Py_REFCNT(builtins), blt_initial, "builtins refcount restored");

    Py_DECREF(builtins);
    Py_DECREF(name);
    return 0;
}


/* === Module tests === */


static int test_module_create(void) {
    /*
      Test that Module can be created.
    */
    PyObject *name = PyUnicode_FromString("mod");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    PyObject *mod = KosModule_New(ns);
    TEST_ASSERT_NOT_NULL(mod, "create Module");
    TEST_ASSERT(KosModule_CheckExact(mod), "module type check");

    /* Verify namespace stored */
    TEST_ASSERT_SAME(KosModule_NAMESPACE(mod), ns, "namespace stored");

    Py_DECREF(mod);
    Py_DECREF(ns);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_module_refcount(void) {
    /*
      Test that Module manages namespace refcount.
    */
    PyObject *name = PyUnicode_FromString("mod");
    PyObject *local = PyTuple_Pack(1, PyUnicode_FromString("x"));

    TEST_ASSERT_NOT_NULL(name, "create name");
    TEST_ASSERT_NOT_NULL(local, "create local");

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");

    Py_ssize_t ns_initial = Py_REFCNT(ns);

    PyObject *mod = KosModule_New(ns);
    TEST_ASSERT_NOT_NULL(mod, "create Module");

    TEST_ASSERT_EQ(Py_REFCNT(ns), ns_initial + 1, "namespace refcount incremented");

    Py_DECREF(mod);
    TEST_ASSERT_EQ(Py_REFCNT(ns), ns_initial, "namespace refcount restored");

    Py_DECREF(ns);
    Py_DECREF(local);
    Py_DECREF(name);
    return 0;
}


static int test_module_requires_namespace(void) {
    /*
      Test that Module rejects non-namespace.
    */
    PyObject *not_ns = PyLong_FromLong(42);
    TEST_ASSERT_NOT_NULL(not_ns, "create non-namespace");

    PyObject *mod = KosModule_New(not_ns);
    TEST_ASSERT_NULL(mod, "Module rejects non-namespace");

    /* Clear the TypeError that was raised */
    PyErr_Clear();

    Py_DECREF(not_ns);
    return 0;
}


/* === stress tests === */


static int test_namespace_stress_bindings(void) {
    /*
      Stress test: create namespace with many bindings.
    */
    PyObject *name = PyUnicode_FromString("stress");
    PyObject *syms[100];

    /* Create symbol names */
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "sym%d", i);
        syms[i] = PyUnicode_FromString(buf);
        TEST_ASSERT_NOT_NULL(syms[i], "create symbol name");
    }

    PyObject *local = PyTuple_New(100);
    for (int i = 0; i < 100; i++) {
        Py_INCREF(syms[i]);
        PyTuple_SET_ITEM(local, i, syms[i]);
    }

    PyObject *ns = KosNamespace_NewFull(name, NULL, local, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");
    TEST_ASSERT_EQ(KosNamespace_FAST_COUNT(ns), 100, "100 bindings");

    /* Store values */
    for (int i = 0; i < 100; i++) {
        PyObject *val = PyLong_FromLong(i * 10);
        KosNamespace_Store(ns, syms[i], val);
        Py_DECREF(val);
    }

    /* Verify values */
    for (int i = 0; i < 100; i++) {
        PyObject *val = KosNamespace_Deref(ns, syms[i]);
        TEST_ASSERT_NOT_NULL(val, "deref value");
        TEST_ASSERT_EQ(PyLong_AsLong(val), i * 10, "value matches");
        Py_DECREF(val);
    }

    Py_DECREF(ns);
    Py_DECREF(local);
    for (int i = 0; i < 100; i++) {
        Py_DECREF(syms[i]);
    }
    Py_DECREF(name);
    return 0;
}


static int test_namespace_nested_deep(void) {
    /*
      Test deeply nested namespaces with closures.
    */
    PyObject *sym = PyUnicode_FromString("x");
    PyObject *local = PyTuple_Pack(1, sym);
    PyObject *closure = PyTuple_Pack(1, sym);

    TEST_ASSERT_NOT_NULL(sym, "create sym");
    TEST_ASSERT_NOT_NULL(local, "create local");
    TEST_ASSERT_NOT_NULL(closure, "create closure");

    /* Create root namespace */
    PyObject *root_name = PyUnicode_FromString("root");
    PyObject *root = KosNamespace_NewFull(root_name, NULL, local, NULL, NULL);
    Py_XDECREF(root_name);
    TEST_ASSERT_NOT_NULL(root, "create root");

    /* Store initial value */
    PyObject *value = PyLong_FromLong(42);
    KosNamespace_Store(root, sym, value);
    Py_DECREF(value);

    /* Create chain of 50 nested namespaces */
    PyObject *current = root;
    Py_INCREF(current);

    for (int i = 0; i < 50; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "ns%d", i);
        PyObject *name = PyUnicode_FromString(buf);

        PyObject *child = KosNamespace_NewFull(name, current, NULL, closure, NULL);
        TEST_ASSERT_NOT_NULL(child, "create child");

        Py_DECREF(name);
        Py_DECREF(current);
        current = child;
    }

    /* Verify closure still works at deepest level */
    PyObject *ref_root = KosNamespace_REF(root, sym);
    PyObject *ref_deep = KosNamespace_REF(current, sym);
    TEST_ASSERT_NOT_NULL(ref_root, "root has ref");
    TEST_ASSERT_NOT_NULL(ref_deep, "deep has ref");
    TEST_ASSERT_SAME(ref_root, ref_deep, "refs are same through closure");

    Py_DECREF(current);
    Py_DECREF(root);
    Py_DECREF(closure);
    Py_DECREF(local);
    Py_DECREF(sym);

    /* Force GC */
    PyGC_Collect();

    return 0;
}


/* === test suite runner === */


int run_namespace_tests(void) {
    BEGIN_TEST_SUITE("Namespace Memory Tests");

    /* NamespaceSpec tests */
    RUN_TEST(test_nspec_create);
    RUN_TEST(test_nspec_none_values);
    RUN_TEST(test_nspec_refcount);

    /* Namespace tests */
    RUN_TEST(test_namespace_create);
    RUN_TEST(test_namespace_store_deref);
    RUN_TEST(test_namespace_store_refcount);
    RUN_TEST(test_namespace_ref);
    RUN_TEST(test_namespace_bind);
    RUN_TEST(test_namespace_nested);
    RUN_TEST(test_namespace_fast_index);
    RUN_TEST(test_namespace_fast_store_deref);

    /* CollectorNamespace tests */
    RUN_TEST(test_collector_create);
    RUN_TEST(test_collector_blocks_closure);

    /* ModuleNamespace tests */
    RUN_TEST(test_modns_create);
    RUN_TEST(test_modns_builtins_fallback);
    RUN_TEST(test_modns_refcount);

    /* Module tests */
    RUN_TEST(test_module_create);
    RUN_TEST(test_module_refcount);
    RUN_TEST(test_module_requires_namespace);

    /* Stress tests */
    RUN_TEST(test_namespace_stress_bindings);
    RUN_TEST(test_namespace_nested_deep);

    return 0;
}


/* The end. */
