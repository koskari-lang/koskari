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
   Memory tests for values type

   Tests the values object creation, reference counting of args/kwds,
   and cyclic structure handling.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === values basic tests === */


static int test_values_create_empty(void) {
    /*
      Test that values can be created with an empty tuple.
    */
    PyObject *args = PyTuple_New(0);
    TEST_ASSERT_NOT_NULL(args, "create empty args");

    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");
    TEST_ASSERT(KosValues_CheckExact(v), "values type check");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_create_positionals(void) {
    /*
      Test that values can be created with positional arguments.
    */
    PyObject *args = Py_BuildValue("(iii)", 1, 2, 3);
    TEST_ASSERT_NOT_NULL(args, "create args tuple");

    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    /* Verify length via sequence protocol */
    Py_ssize_t len = PySequence_Length(v);
    TEST_ASSERT_EQ(len, 3, "values has 3 positionals");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_create_keywords(void) {
    /*
      Test that values can be created with keyword arguments.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *kwds = PyDict_New();
    TEST_ASSERT_NOT_NULL(args, "create empty args");
    TEST_ASSERT_NOT_NULL(kwds, "create kwds dict");

    PyObject *key = PyUnicode_FromString("foo");
    PyObject *val = PyLong_FromLong(42);
    PyDict_SetItem(kwds, key, val);
    Py_DECREF(key);
    Py_DECREF(val);

    PyObject *v = KosValues_New(args, kwds);
    TEST_ASSERT_NOT_NULL(v, "create values with kwds");

    Py_DECREF(v);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


static int test_values_is_koscallable(void) {
    /*
      values is a native KosCallable and an exact-type instance.
    */
    PyObject *args = PyTuple_New(0);
    TEST_ASSERT_NOT_NULL(args, "create empty args");

    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");
    TEST_ASSERT(KosCallable_Check(v), "values passes KosCallable_Check");
    TEST_ASSERT(KosValues_CheckExact(v), "KosValues_CheckExact");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_type_is_koscallable_subtype(void) {
    /*
      KosValuesType must be a KosEvaluated with a call suite so bytecode
      dispatch routes through the install slot.
    */
    TEST_ASSERT(
        PyType_IsSubtype(KOS_EVALUATED_PYTYPE(KosValuesType),
                         KOS_EVALUATED_PYTYPE(KosEvaluatedType)),
        "values is subtype of _KosEvaluated");
    return 0;
}


static int test_values_create_both(void) {
    /*
      Test that values can be created with both positionals and keywords.
    */
    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    PyObject *kwds = PyDict_New();
    TEST_ASSERT_NOT_NULL(args, "create args");
    TEST_ASSERT_NOT_NULL(kwds, "create kwds");

    PyObject *key = PyUnicode_FromString("x");
    PyObject *val = PyLong_FromLong(100);
    PyDict_SetItem(kwds, key, val);
    Py_DECREF(key);
    Py_DECREF(val);

    PyObject *v = KosValues_New(args, kwds);
    TEST_ASSERT_NOT_NULL(v, "create values with both");

    /* Check positional count */
    Py_ssize_t len = PySequence_Length(v);
    TEST_ASSERT_EQ(len, 2, "values has 2 positionals");

    Py_DECREF(v);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


static int test_values_null_args_error(void) {
    /*
      Test that values creation with NULL args raises an error.
    */
    PyObject *v = KosValues_New(NULL, NULL);
    TEST_ASSERT_NULL(v, "NULL args should fail");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error should be set");
    PyErr_Clear();
    return 0;
}


/* === refcount tests === */


static int test_values_refcount_args(void) {
    /*
      Test that values properly manage refcounts of args tuple.

      Note: PySequence_Tuple returns the same tuple (with refcount+1) when
      passed an exact tuple, so we test tuple refcount, not item refcount.
    */
    PyObject *item = PyLong_FromLong(100000);
    TEST_ASSERT_NOT_NULL(item, "create item");

    PyObject *args = PyTuple_New(1);
    PyTuple_SET_ITEM(args, 0, item);
    /* SET_ITEM steals reference */

    Py_ssize_t args_initial = Py_REFCNT(args);

    /* Creating values should reference the tuple */
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    /* PySequence_Tuple on exact tuple returns same tuple with refcount+1 */
    TEST_ASSERT_EQ(Py_REFCNT(args), args_initial + 1, "args refcount after values");

    /* Destroy values - should decrement tuple refcount */
    Py_DECREF(v);
    TEST_ASSERT_EQ(Py_REFCNT(args), args_initial, "args refcount after dealloc");

    /* Clean up - this will also decref item */
    Py_DECREF(args);
    return 0;
}


static int test_values_refcount_kwds(void) {
    /*
      Test that values properly copy and manage kwds dict refcounts.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *kwds = PyDict_New();
    TEST_ASSERT_NOT_NULL(args, "create args");
    TEST_ASSERT_NOT_NULL(kwds, "create kwds");

    PyObject *val = PyLong_FromLong(100001);
    PyObject *key = PyUnicode_FromString("test");
    PyDict_SetItem(kwds, key, val);

    Py_ssize_t val_initial = Py_REFCNT(val);

    PyObject *v = KosValues_New(args, kwds);
    TEST_ASSERT_NOT_NULL(v, "create values");

    /* Value should be referenced by both original kwds and copied kwds */
    TEST_ASSERT_EQ(Py_REFCNT(val), val_initial + 1, "val refcount after values");

    Py_DECREF(v);
    TEST_ASSERT_EQ(Py_REFCNT(val), val_initial, "val refcount after dealloc");

    Py_DECREF(key);
    Py_DECREF(val);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


/* === bool tests === */


static int test_values_bool_empty_false(void) {
    /*
      Test that empty values is falsy.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create empty values");

    int result = PyObject_IsTrue(v);
    TEST_ASSERT_EQ(result, 0, "empty values is falsy");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_bool_positionals_true(void) {
    /*
      Test that values with positionals is truthy.
    */
    PyObject *args = Py_BuildValue("(i)", 1);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values with positional");

    int result = PyObject_IsTrue(v);
    TEST_ASSERT_EQ(result, 1, "values with positional is truthy");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_bool_keywords_true(void) {
    /*
      Test that values with keywords only is truthy.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *kwds = PyDict_New();
    PyObject *key = PyUnicode_FromString("x");
    PyObject *val = PyLong_FromLong(1);
    PyDict_SetItem(kwds, key, val);

    PyObject *v = KosValues_New(args, kwds);
    TEST_ASSERT_NOT_NULL(v, "create values with keyword");

    int result = PyObject_IsTrue(v);
    TEST_ASSERT_EQ(result, 1, "values with keyword is truthy");

    Py_DECREF(v);
    Py_DECREF(key);
    Py_DECREF(val);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


/* === hash and equality tests === */


static int test_values_hash_consistent(void) {
    /*
      Test that hash is consistent for equivalent values.
    */
    PyObject *args1 = Py_BuildValue("(ii)", 1, 2);
    PyObject *args2 = Py_BuildValue("(ii)", 1, 2);

    PyObject *v1 = KosValues_New(args1, NULL);
    PyObject *v2 = KosValues_New(args2, NULL);
    TEST_ASSERT_NOT_NULL(v1, "create v1");
    TEST_ASSERT_NOT_NULL(v2, "create v2");

    Py_hash_t h1 = PyObject_Hash(v1);
    Py_hash_t h2 = PyObject_Hash(v2);

    TEST_ASSERT(h1 != -1, "v1 hash should succeed");
    TEST_ASSERT_EQ(h1, h2, "equal values should have equal hash");

    Py_DECREF(v1);
    Py_DECREF(v2);
    Py_DECREF(args1);
    Py_DECREF(args2);
    return 0;
}


static int test_values_eq_positionals(void) {
    /*
      Test equality for values with same positionals.
    */
    PyObject *args1 = Py_BuildValue("(ii)", 1, 2);
    PyObject *args2 = Py_BuildValue("(ii)", 1, 2);

    PyObject *v1 = KosValues_New(args1, NULL);
    PyObject *v2 = KosValues_New(args2, NULL);

    int eq = PyObject_RichCompareBool(v1, v2, Py_EQ);
    TEST_ASSERT_EQ(eq, 1, "equal values should compare equal");

    Py_DECREF(v1);
    Py_DECREF(v2);
    Py_DECREF(args1);
    Py_DECREF(args2);
    return 0;
}


static int test_values_ne_positionals(void) {
    /*
      Test inequality for values with different positionals.
    */
    PyObject *args1 = Py_BuildValue("(ii)", 1, 2);
    PyObject *args2 = Py_BuildValue("(ii)", 1, 3);

    PyObject *v1 = KosValues_New(args1, NULL);
    PyObject *v2 = KosValues_New(args2, NULL);

    int eq = PyObject_RichCompareBool(v1, v2, Py_EQ);
    TEST_ASSERT_EQ(eq, 0, "different values should not be equal");

    int ne = PyObject_RichCompareBool(v1, v2, Py_NE);
    TEST_ASSERT_EQ(ne, 1, "different values should be not-equal");

    Py_DECREF(v1);
    Py_DECREF(v2);
    Py_DECREF(args1);
    Py_DECREF(args2);
    return 0;
}


static int test_values_eq_tuple(void) {
    /*
      Test that values with no keywords equals equivalent tuple.
    */
    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    PyObject *v = KosValues_New(args, NULL);

    int eq = PyObject_RichCompareBool(v, args, Py_EQ);
    TEST_ASSERT_EQ(eq, 1, "values should equal equivalent tuple");

    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_eq_dict(void) {
    /*
      Test that values with no positionals equals equivalent dict.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *kwds = PyDict_New();
    PyObject *key = PyUnicode_FromString("x");
    PyObject *val = PyLong_FromLong(1);
    PyDict_SetItem(kwds, key, val);

    PyObject *v = KosValues_New(args, kwds);

    int eq = PyObject_RichCompareBool(v, kwds, Py_EQ);
    TEST_ASSERT_EQ(eq, 1, "values should equal equivalent dict");

    Py_DECREF(v);
    Py_DECREF(key);
    Py_DECREF(val);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


/* === iteration tests === */


static int test_values_iter(void) {
    /*
      Test that values can be iterated over positionals.
    */
    PyObject *args = Py_BuildValue("(iii)", 10, 20, 30);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *iter = PyObject_GetIter(v);
    TEST_ASSERT_NOT_NULL(iter, "get iterator");

    PyObject *item;
    int count = 0;
    while ((item = PyIter_Next(iter)) != NULL) {
        count++;
        Py_DECREF(item);
    }
    TEST_ASSERT_EQ(count, 3, "iterated 3 items");

    Py_DECREF(iter);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


/* === sequence/mapping tests === */


static int test_values_getitem_index(void) {
    /*
      Test indexing positionals by integer.
    */
    PyObject *args = Py_BuildValue("(iii)", 100, 200, 300);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *item = PySequence_GetItem(v, 1);
    TEST_ASSERT_NOT_NULL(item, "get item at index 1");

    long val = PyLong_AsLong(item);
    TEST_ASSERT_EQ(val, 200, "item at index 1 is 200");

    Py_DECREF(item);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_getitem_key(void) {
    /*
      Test indexing keywords by string key.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *kwds = PyDict_New();
    PyObject *key = PyUnicode_FromString("mykey");
    PyObject *val = PyLong_FromLong(999);
    PyDict_SetItem(kwds, key, val);

    PyObject *v = KosValues_New(args, kwds);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *result = PyObject_GetItem(v, key);
    TEST_ASSERT_NOT_NULL(result, "get item by key");

    long rval = PyLong_AsLong(result);
    TEST_ASSERT_EQ(rval, 999, "value for 'mykey' is 999");

    Py_DECREF(result);
    Py_DECREF(v);
    Py_DECREF(key);
    Py_DECREF(val);
    Py_DECREF(kwds);
    Py_DECREF(args);
    return 0;
}


static int test_values_getitem_missing_key(void) {
    /*
      Test that accessing missing key raises KeyError.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *key = PyUnicode_FromString("missing");
    PyObject *result = PyObject_GetItem(v, key);
    TEST_ASSERT_NULL(result, "missing key should return NULL");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_KeyError), "should raise KeyError");
    PyErr_Clear();

    Py_DECREF(key);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


/* === repr tests === */


static int test_values_repr_empty(void) {
    /*
      Test repr of empty values.
    */
    PyObject *args = PyTuple_New(0);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *repr = PyObject_Repr(v);
    TEST_ASSERT_NOT_NULL(repr, "get repr");

    const char *s = PyUnicode_AsUTF8(repr);
    TEST_ASSERT(strcmp(s, "values()") == 0, "repr is 'values()'");

    Py_DECREF(repr);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_repr_positionals(void) {
    /*
      Test repr of values with positionals.
    */
    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *repr = PyObject_Repr(v);
    TEST_ASSERT_NOT_NULL(repr, "get repr");

    const char *s = PyUnicode_AsUTF8(repr);
    TEST_ASSERT(strcmp(s, "values(1, 2)") == 0, "repr is 'values(1, 2)'");

    Py_DECREF(repr);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


/* === add tests === */


static int test_values_add_values(void) {
    /*
      Test adding two values together.
    */
    PyObject *args1 = Py_BuildValue("(ii)", 1, 2);
    PyObject *args2 = Py_BuildValue("(ii)", 3, 4);

    PyObject *v1 = KosValues_New(args1, NULL);
    PyObject *v2 = KosValues_New(args2, NULL);
    TEST_ASSERT_NOT_NULL(v1, "create v1");
    TEST_ASSERT_NOT_NULL(v2, "create v2");

    PyObject *result = PyNumber_Add(v1, v2);
    TEST_ASSERT_NOT_NULL(result, "add values");
    TEST_ASSERT(KosValues_CheckExact(result), "result is values");

    Py_ssize_t len = PySequence_Length(result);
    TEST_ASSERT_EQ(len, 4, "result has 4 positionals");

    Py_DECREF(result);
    Py_DECREF(v1);
    Py_DECREF(v2);
    Py_DECREF(args1);
    Py_DECREF(args2);
    return 0;
}


static int test_values_add_tuple(void) {
    /*
      Test adding a tuple to values.
    */
    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *extra = Py_BuildValue("(ii)", 3, 4);
    PyObject *result = PyNumber_Add(v, extra);
    TEST_ASSERT_NOT_NULL(result, "add tuple to values");
    TEST_ASSERT(KosValues_CheckExact(result), "result is values");

    Py_ssize_t len = PySequence_Length(result);
    TEST_ASSERT_EQ(len, 4, "result has 4 positionals");

    Py_DECREF(result);
    Py_DECREF(extra);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_add_dict(void) {
    /*
      Test adding a dict to values.
    */
    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *extra = PyDict_New();
    PyObject *key = PyUnicode_FromString("x");
    PyObject *val = PyLong_FromLong(99);
    PyDict_SetItem(extra, key, val);

    PyObject *result = PyNumber_Add(v, extra);
    TEST_ASSERT_NOT_NULL(result, "add dict to values");
    TEST_ASSERT(KosValues_CheckExact(result), "result is values");

    /* Positionals unchanged */
    Py_ssize_t len = PySequence_Length(result);
    TEST_ASSERT_EQ(len, 2, "result has 2 positionals");

    /* Keywords added */
    PyObject *kres = PyObject_GetItem(result, key);
    TEST_ASSERT_NOT_NULL(kres, "keyword present in result");
    Py_DECREF(kres);

    Py_DECREF(result);
    Py_DECREF(key);
    Py_DECREF(val);
    Py_DECREF(extra);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


/* === call tests === */


static int test_values_call(void) {
    /*
      Test calling values with a function.
    */
    PyObject *args = Py_BuildValue("(ii)", 10, 20);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    /* Use sum as the callable */
    PyObject *builtins = PyImport_ImportModule("builtins");
    PyObject *sum_func = PyObject_GetAttrString(builtins, "sum");
    TEST_ASSERT_NOT_NULL(sum_func, "get sum function");

    /* Call: v(sum) should call sum((10, 20)) which returns 30 */
    /* But sum takes an iterable, so we need to use a list */
    Py_DECREF(v);
    Py_DECREF(args);

    /* Let's use a simpler test with max */
    PyObject *max_func = PyObject_GetAttrString(builtins, "max");
    TEST_ASSERT_NOT_NULL(max_func, "get max function");

    args = Py_BuildValue("(iii)", 5, 10, 3);
    v = KosValues_New(args, NULL);

    /* v(max) should call max(5, 10, 3) = 10 */
    PyObject *call_args = Py_BuildValue("(O)", max_func);
    PyObject *result = PyObject_Call(v, call_args, NULL);
    TEST_ASSERT_NOT_NULL(result, "call values with max");

    long rval = PyLong_AsLong(result);
    TEST_ASSERT_EQ(rval, 10, "max(5, 10, 3) = 10");

    Py_DECREF(result);
    Py_DECREF(call_args);
    Py_DECREF(max_func);
    Py_DECREF(sum_func);
    Py_DECREF(builtins);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


static int test_values_call_no_args_error(void) {
    /*
      Test that calling values without arguments raises TypeError.
    */
    PyObject *args = Py_BuildValue("(i)", 1);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    PyObject *empty_args = PyTuple_New(0);
    PyObject *result = PyObject_Call(v, empty_args, NULL);
    TEST_ASSERT_NULL(result, "call with no args should fail");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_TypeError), "should raise TypeError");
    PyErr_Clear();

    Py_DECREF(empty_args);
    Py_DECREF(v);
    Py_DECREF(args);
    return 0;
}


/* === stress tests === */


static int test_values_stress_alloc(void) {
    /*
      Stress test: create and destroy many values objects.
    */
    PyObject *values[100];

    for (int i = 0; i < 100; i++) {
        PyObject *args = Py_BuildValue("(ii)", i, i + 1);
        values[i] = KosValues_New(args, NULL);
        TEST_ASSERT_NOT_NULL(values[i], "create stress values");
        Py_DECREF(args);
    }

    for (int i = 0; i < 100; i++) {
        Py_DECREF(values[i]);
    }

    return 0;
}


static int test_values_gc(void) {
    /*
      Test that values objects work with garbage collector.
    */
    PyObject *args = Py_BuildValue("(i)", 1);
    PyObject *v = KosValues_New(args, NULL);
    TEST_ASSERT_NOT_NULL(v, "create values");

    /* Force GC */
    PyGC_Collect();

    /* Values should still be valid */
    Py_ssize_t len = PySequence_Length(v);
    TEST_ASSERT_EQ(len, 1, "values still valid after GC");

    Py_DECREF(v);
    Py_DECREF(args);

    /* Force GC again to clean up */
    PyGC_Collect();

    return 0;
}


/* === test suite runner === */


int run_values_tests(void) {
    BEGIN_TEST_SUITE("Values Memory Tests");

    /* Basic creation tests */
    RUN_TEST(test_values_create_empty);
    RUN_TEST(test_values_create_positionals);
    RUN_TEST(test_values_create_keywords);
    RUN_TEST(test_values_create_both);
    RUN_TEST(test_values_is_koscallable);
    RUN_TEST(test_values_type_is_koscallable_subtype);
    RUN_TEST(test_values_null_args_error);

    /* Refcount tests */
    RUN_TEST(test_values_refcount_args);
    RUN_TEST(test_values_refcount_kwds);

    /* Bool tests */
    RUN_TEST(test_values_bool_empty_false);
    RUN_TEST(test_values_bool_positionals_true);
    RUN_TEST(test_values_bool_keywords_true);

    /* Hash and equality tests */
    RUN_TEST(test_values_hash_consistent);
    RUN_TEST(test_values_eq_positionals);
    RUN_TEST(test_values_ne_positionals);
    RUN_TEST(test_values_eq_tuple);
    RUN_TEST(test_values_eq_dict);

    /* Iteration tests */
    RUN_TEST(test_values_iter);

    /* Sequence/mapping tests */
    RUN_TEST(test_values_getitem_index);
    RUN_TEST(test_values_getitem_key);
    RUN_TEST(test_values_getitem_missing_key);

    /* Repr tests */
    RUN_TEST(test_values_repr_empty);
    RUN_TEST(test_values_repr_positionals);

    /* Add tests */
    RUN_TEST(test_values_add_values);
    RUN_TEST(test_values_add_tuple);
    RUN_TEST(test_values_add_dict);

    /* Call tests */
    RUN_TEST(test_values_call);
    RUN_TEST(test_values_call_no_args_error);

    /* Stress tests */
    RUN_TEST(test_values_stress_alloc);
    RUN_TEST(test_values_gc);

    return 0;
}


/* The end. */
