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
   Memory tests for KosProperty.

   Verifies reference count correctness and dealloc behaviour for KosProperty
   construction with KosCallable, plain Python callable, None, and repeated
   (idempotent) Demand wrapping.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === helpers === */


/* Return a borrowed reference to builtins.len (a plain Python callable). */
static PyObject *get_len_builtin(void) {

  static PyObject *len_fn = NULL;

  if (! len_fn) {
    PyObject *builtins = PyImport_ImportModule("builtins");
    if (! builtins)
      return NULL;
    len_fn = PyObject_GetAttrString(builtins, "len");
    Py_DECREF(builtins);
  }

  return len_fn;
}


/* Build a KosProperty from positional (fget, fset, fdel) args. Returns
   a new reference, or NULL on error. */
static PyObject *make_kosproperty(
    PyObject *fget,
    PyObject *fset,
    PyObject *fdel) {

  PyObject *args = PyTuple_Pack(3,
      fget ? fget : Py_None,
      fset ? fset : Py_None,
      fdel ? fdel : Py_None);

  if (! args)
    return NULL;

  PyObject *prop = PyObject_CallObject((PyObject *) &KosPropertyType, args);
  Py_DECREF(args);
  return prop;
}


/* === tests === */


static int test_kosproperty_isinstance_property(void) {
  /*
    KosProperty instances must satisfy isinstance(obj, property) since
    KosPropertyType.tp_base is builtins.property.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *prop = make_kosproperty(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(len)");

  TEST_ASSERT(
      PyObject_IsInstance(prop, (PyObject *) KosPropertyType.tp_base) == 1,
      "isinstance(prop, property)");

  Py_DECREF(prop);
  return 0;
}


static int test_kosproperty_type_is_subtype(void) {
  /*
    KosPropertyType must be a subtype of builtins.property after init.
  */
  TEST_ASSERT(KosPropertyType.tp_base != NULL,
              "KosPropertyType.tp_base is set");
  TEST_ASSERT(PyType_IsSubtype(&KosPropertyType, KosPropertyType.tp_base),
              "KosPropertyType is subtype of property");
  return 0;
}


static int test_kosproperty_python_callable_passthrough(void) {
  /*
    A plain Python callable must be stored directly (no Demand wrapping).
    The stored fget must be the same object as the argument.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *prop = make_kosproperty(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(len)");

  PyObject *stored = PyObject_GetAttrString(prop, "fget");
  TEST_ASSERT_NOT_NULL(stored, "read prop.fget");
  TEST_ASSERT_SAME(stored, len_fn, "fget is unchanged for Python callable");

  Py_DECREF(stored);
  Py_DECREF(prop);
  return 0;
}


static int test_kosproperty_koscallable_wrapped_in_demand(void) {
  /*
    A KosCallable (here: a Demand instance, which is itself a KosCallable)
    must be stored as Demand(callable). Since Demand(Demand(x)) is idempotent,
    KosProperty(demand_obj).fget is demand_obj itself.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  /* Build a Demand(len) — Demand is a KosCallable */
  PyObject *demand_obj = KosDemand_New(len_fn);
  TEST_ASSERT_NOT_NULL(demand_obj, "create Demand(len)");
  TEST_ASSERT(KosCallable_Check(demand_obj), "Demand passes KosCallable_Check");

  PyObject *prop = make_kosproperty(demand_obj, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(demand_obj)");

  PyObject *stored = PyObject_GetAttrString(prop, "fget");
  TEST_ASSERT_NOT_NULL(stored, "read prop.fget");

  /* Idempotent: Demand(Demand(fn)) returns Demand(fn) */
  TEST_ASSERT_SAME(stored, demand_obj, "fget is the same Demand for idempotent wrap");

  Py_DECREF(stored);
  Py_DECREF(prop);
  Py_DECREF(demand_obj);
  return 0;
}


static int test_kosproperty_none_fget(void) {
  /*
    KosProperty() with no args must store None (or NULL) for fget/fset/fdel;
    accessing prop.fget returns None.
  */
  PyObject *prop = make_kosproperty(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty()");

  PyObject *stored = PyObject_GetAttrString(prop, "fget");
  TEST_ASSERT_NOT_NULL(stored, "read prop.fget");
  TEST_ASSERT(stored == Py_None, "fget is None when not provided");

  Py_DECREF(stored);
  Py_DECREF(prop);
  return 0;
}


static int test_kosproperty_refcount_python_callable(void) {
  /*
    When a plain Python callable is stored as fget, its refcount increases
    by exactly 1 for the duration of the property's life, then returns to
    the original count after dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  Py_ssize_t initial = Py_REFCNT(len_fn);

  PyObject *prop = make_kosproperty(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(len)");

  /* Property holds a ref to len */
  TEST_ASSERT(Py_REFCNT(len_fn) > initial, "fget refcount increased");

  Py_DECREF(prop);

  TEST_ASSERT_EQ(Py_REFCNT(len_fn), initial, "fget refcount restored after dealloc");
  return 0;
}


static int test_kosproperty_refcount_demand_idempotent(void) {
  /*
    When a Demand is stored idempotently as fget, its refcount increases by
    exactly 1 while the property lives, then returns after dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *demand_obj = KosDemand_New(len_fn);
  TEST_ASSERT_NOT_NULL(demand_obj, "create Demand(len)");

  Py_ssize_t initial = Py_REFCNT(demand_obj);

  PyObject *prop = make_kosproperty(demand_obj, NULL, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(demand_obj)");

  TEST_ASSERT(Py_REFCNT(demand_obj) > initial, "demand refcount increased");

  Py_DECREF(prop);

  TEST_ASSERT_EQ(Py_REFCNT(demand_obj), initial,
                 "demand refcount restored after dealloc");
  Py_DECREF(demand_obj);
  return 0;
}


static int test_kosproperty_fset_fget_independent(void) {
  /*
    KosProperty(fget, fset) with different callables for fget and fset;
    each is stored independently without cross-contamination.
  */
  PyObject *builtins = PyImport_ImportModule("builtins");
  TEST_ASSERT_NOT_NULL(builtins, "import builtins");

  PyObject *len_fn = PyObject_GetAttrString(builtins, "len");
  PyObject *str_fn = PyObject_GetAttrString(builtins, "str");
  Py_DECREF(builtins);
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");
  TEST_ASSERT_NOT_NULL(str_fn, "get builtins.str");

  PyObject *prop = make_kosproperty(len_fn, str_fn, NULL);
  TEST_ASSERT_NOT_NULL(prop, "construct KosProperty(len, str)");

  PyObject *stored_fget = PyObject_GetAttrString(prop, "fget");
  PyObject *stored_fset = PyObject_GetAttrString(prop, "fset");
  TEST_ASSERT_NOT_NULL(stored_fget, "read prop.fget");
  TEST_ASSERT_NOT_NULL(stored_fset, "read prop.fset");

  TEST_ASSERT_SAME(stored_fget, len_fn, "fget is len");
  TEST_ASSERT_SAME(stored_fset, str_fn, "fset is str");

  Py_DECREF(stored_fget);
  Py_DECREF(stored_fset);
  Py_DECREF(prop);
  Py_DECREF(len_fn);
  Py_DECREF(str_fn);
  return 0;
}


/* === suite entry point === */


int run_property_tests(void) {

  BEGIN_TEST_SUITE("KosProperty Memory Tests");

  RUN_TEST(test_kosproperty_type_is_subtype);
  RUN_TEST(test_kosproperty_isinstance_property);
  RUN_TEST(test_kosproperty_python_callable_passthrough);
  RUN_TEST(test_kosproperty_koscallable_wrapped_in_demand);
  RUN_TEST(test_kosproperty_none_fget);
  RUN_TEST(test_kosproperty_refcount_python_callable);
  RUN_TEST(test_kosproperty_refcount_demand_idempotent);
  RUN_TEST(test_kosproperty_fset_fget_independent);

  return 0;
}


// The end.
