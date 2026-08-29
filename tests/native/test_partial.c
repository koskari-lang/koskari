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
   Memory tests for KosPartial.

   Verifies KosCallable identity, attribute exposure, reference-count
   correctness across construction, nested flattening, calling, pickling
   reduction, and instance __dict__ -- with refcounts restored after dealloc so
   Valgrind / ASan see no leaks.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
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


/* Build a KosPartial from func plus optional bound args tuple and keywords
   dict. Returns a new reference, or NULL on error. */
static PyObject *make_kospartial(
    PyObject *func,
    PyObject *bound,
    PyObject *kwds) {

  PyObject *head = PyTuple_Pack(1, func);
  if (! head)
    return NULL;

  PyObject *args;
  if (bound) {
    args = PySequence_Concat(head, bound);
    Py_DECREF(head);
    if (! args)
      return NULL;
  } else {
    args = head;
  }

  PyObject *kp = PyObject_Call(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosPartialCallableType), args, kwds);
  Py_DECREF(args);
  return kp;
}


/* === tests === */


static int test_kospartial_is_koscallable(void) {
  /*
    A KosPartial is a native KosCallable and an exact-type instance.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *kp = make_kospartial(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len)");

  TEST_ASSERT(KosCallable_Check(kp), "KosPartial passes KosCallable_Check");
  TEST_ASSERT(KosPartial_CheckExact(kp), "KosPartial_CheckExact");

  Py_DECREF(kp);
  return 0;
}


static int test_kospartial_type_is_koscallable_subtype(void) {
  /*
    KosPartialCallableType must be a KosEvaluated with a call suite so
    dispatch routes through the install slot.
  */
  TEST_ASSERT(
      PyType_IsSubtype(KOS_EVALUATED_PYTYPE(KosPartialCallableType),
                       KOS_EVALUATED_PYTYPE(KosEvaluatedType)),
      "KosPartial is subtype of _KosEvaluated");
  return 0;
}


static int test_kospartial_not_functools_partial(void) {
  /*
    KosPartial is duck-typed, not in functools.partial's inheritance tree.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *functools = PyImport_ImportModule("functools");
  TEST_ASSERT_NOT_NULL(functools, "import functools");
  PyObject *partial_type = PyObject_GetAttrString(functools, "partial");
  Py_DECREF(functools);
  TEST_ASSERT_NOT_NULL(partial_type, "get functools.partial");

  PyObject *kp = make_kospartial(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len)");

  TEST_ASSERT(PyObject_IsInstance(kp, partial_type) == 0,
              "KosPartial is not isinstance functools.partial");

  Py_DECREF(kp);
  Py_DECREF(partial_type);
  return 0;
}


static int test_kospartial_attributes(void) {
  /*
    func / args / keywords expose the bound callable and arguments; keywords
    defaults to an empty dict.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *one = PyLong_FromLong(1);
  TEST_ASSERT_NOT_NULL(one, "make 1");
  PyObject *bound = PyTuple_Pack(1, one);
  Py_DECREF(one);
  TEST_ASSERT_NOT_NULL(bound, "pack bound args");

  PyObject *kp = make_kospartial(len_fn, bound, NULL);
  Py_DECREF(bound);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len, 1)");

  PyObject *func = PyObject_GetAttrString(kp, "func");
  PyObject *args = PyObject_GetAttrString(kp, "args");
  PyObject *kw = PyObject_GetAttrString(kp, "keywords");
  TEST_ASSERT_NOT_NULL(func, "read func");
  TEST_ASSERT_NOT_NULL(args, "read args");
  TEST_ASSERT_NOT_NULL(kw, "read keywords");

  TEST_ASSERT_SAME(func, len_fn, "func is len");
  TEST_ASSERT(PyTuple_Check(args) && PyTuple_GET_SIZE(args) == 1,
              "args is a 1-tuple");
  TEST_ASSERT(PyDict_Check(kw) && PyDict_Size(kw) == 0,
              "keywords is an empty dict");

  Py_DECREF(func);
  Py_DECREF(args);
  Py_DECREF(kw);
  Py_DECREF(kp);
  return 0;
}


static int test_kospartial_refcount_func(void) {
  /*
    func refcount increases by exactly 1 for the partial's life, then returns
    to the original count after dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  Py_ssize_t initial = Py_REFCNT(len_fn);

  PyObject *kp = make_kospartial(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len)");
  TEST_ASSERT(Py_REFCNT(len_fn) > initial, "func refcount increased");

  Py_DECREF(kp);
  TEST_ASSERT_EQ(Py_REFCNT(len_fn), initial, "func refcount restored");
  return 0;
}


static int test_kospartial_refcount_bound_arg(void) {
  /*
    A bound positional argument is held while the partial lives, and released
    on dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  /* 123456 is outside the small-int cache, so its refcount is meaningful. */
  PyObject *marker = PyLong_FromLong(123456);
  TEST_ASSERT_NOT_NULL(marker, "make marker");
  Py_ssize_t initial = Py_REFCNT(marker);

  PyObject *bound = PyTuple_Pack(1, marker);
  TEST_ASSERT_NOT_NULL(bound, "pack bound args");

  PyObject *kp = make_kospartial(len_fn, bound, NULL);
  Py_DECREF(bound);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len, marker)");
  TEST_ASSERT(Py_REFCNT(marker) > initial, "bound arg refcount increased");

  Py_DECREF(kp);
  TEST_ASSERT_EQ(Py_REFCNT(marker), initial, "bound arg refcount restored");

  Py_DECREF(marker);
  return 0;
}


static int test_kospartial_flatten_refcounts(void) {
  /*
    KosPartial(KosPartial(fn, a), b) collapses to fn with merged args; the inner
    partial is not retained, and all refcounts balance after dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  Py_ssize_t len_initial = Py_REFCNT(len_fn);

  PyObject *a = PyLong_FromLong(111111);
  PyObject *b = PyLong_FromLong(222222);
  TEST_ASSERT_NOT_NULL(a, "make a");
  TEST_ASSERT_NOT_NULL(b, "make b");
  Py_ssize_t a_initial = Py_REFCNT(a);
  Py_ssize_t b_initial = Py_REFCNT(b);

  PyObject *abound = PyTuple_Pack(1, a);
  PyObject *bbound = PyTuple_Pack(1, b);
  TEST_ASSERT_NOT_NULL(abound, "pack a");
  TEST_ASSERT_NOT_NULL(bbound, "pack b");

  PyObject *inner = make_kospartial(len_fn, abound, NULL);
  Py_DECREF(abound);
  TEST_ASSERT_NOT_NULL(inner, "construct inner");

  PyObject *outer = make_kospartial(inner, bbound, NULL);
  Py_DECREF(bbound);
  TEST_ASSERT_NOT_NULL(outer, "construct outer (flattening)");

  /* outer.func is len, not the inner partial. */
  PyObject *ofunc = PyObject_GetAttrString(outer, "func");
  PyObject *oargs = PyObject_GetAttrString(outer, "args");
  TEST_ASSERT_NOT_NULL(ofunc, "read outer.func");
  TEST_ASSERT_NOT_NULL(oargs, "read outer.args");
  TEST_ASSERT_SAME(ofunc, len_fn, "outer.func flattened to len");
  TEST_ASSERT(PyTuple_Check(oargs) && PyTuple_GET_SIZE(oargs) == 2,
              "outer.args merged to 2 items");
  Py_DECREF(ofunc);
  Py_DECREF(oargs);

  Py_DECREF(inner);
  Py_DECREF(outer);

  TEST_ASSERT_EQ(Py_REFCNT(len_fn), len_initial, "len refcount restored");
  TEST_ASSERT_EQ(Py_REFCNT(a), a_initial, "a refcount restored");
  TEST_ASSERT_EQ(Py_REFCNT(b), b_initial, "b refcount restored");

  Py_DECREF(a);
  Py_DECREF(b);
  return 0;
}


static int test_kospartial_call_merges(void) {
  /*
    tp_call merges bound keywords with the call: KosPartial(int, base=2)("10")
    yields 2, and nothing leaks.
  */
  PyObject *kw = PyDict_New();
  TEST_ASSERT_NOT_NULL(kw, "make keywords");
  PyObject *two = PyLong_FromLong(2);
  TEST_ASSERT_NOT_NULL(two, "make 2");
  int sr = PyDict_SetItemString(kw, "base", two);
  Py_DECREF(two);
  TEST_ASSERT(sr == 0, "set base=2");

  PyObject *kp = make_kospartial((PyObject *) &PyLong_Type, NULL, kw);
  Py_DECREF(kw);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(int, base=2)");

  PyObject *call_args = Py_BuildValue("(s)", "10");
  TEST_ASSERT_NOT_NULL(call_args, "build call args");
  PyObject *result = PyObject_CallObject(kp, call_args);
  Py_DECREF(call_args);
  TEST_ASSERT_NOT_NULL(result, "call KosPartial(int, base=2)('10')");

  TEST_ASSERT(PyLong_Check(result) && PyLong_AsLong(result) == 2,
              "int('10', base=2) == 2");

  Py_DECREF(result);
  Py_DECREF(kp);
  return 0;
}


static int test_kospartial_reduce(void) {
  /*
    __reduce__ returns a 3-tuple (callable, args, state) without leaking.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *kp = make_kospartial(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len)");

  PyObject *reduced = PyObject_CallMethod(kp, "__reduce__", NULL);
  TEST_ASSERT_NOT_NULL(reduced, "call __reduce__");
  TEST_ASSERT(PyTuple_Check(reduced) && PyTuple_GET_SIZE(reduced) == 3,
              "__reduce__ returns a 3-tuple");

  Py_DECREF(reduced);
  Py_DECREF(kp);
  return 0;
}


static int test_kospartial_instance_dict(void) {
  /*
    Setting an attribute populates the instance __dict__, which is released on
    dealloc.
  */
  PyObject *len_fn = get_len_builtin();
  TEST_ASSERT_NOT_NULL(len_fn, "get builtins.len");

  PyObject *kp = make_kospartial(len_fn, NULL, NULL);
  TEST_ASSERT_NOT_NULL(kp, "construct KosPartial(len)");

  /* 987654 is outside the small-int cache, so its refcount is meaningful
     (small ints are immortal in 3.12 and never change refcount). */
  PyObject *val = PyLong_FromLong(987654);
  TEST_ASSERT_NOT_NULL(val, "make value");
  Py_ssize_t val_initial = Py_REFCNT(val);

  int sr = PyObject_SetAttrString(kp, "label", val);
  TEST_ASSERT(sr == 0, "set attribute");
  TEST_ASSERT(Py_REFCNT(val) > val_initial, "attribute holds a reference");

  PyObject *got = PyObject_GetAttrString(kp, "label");
  TEST_ASSERT_NOT_NULL(got, "read attribute");
  TEST_ASSERT_SAME(got, val, "attribute round-trips");
  Py_DECREF(got);

  Py_DECREF(kp);
  TEST_ASSERT_EQ(Py_REFCNT(val), val_initial,
                 "attribute refcount restored after dealloc");
  Py_DECREF(val);
  return 0;
}


/* === suite entry point === */


int run_partial_tests(void) {

  BEGIN_TEST_SUITE("KosPartial Memory Tests");

  RUN_TEST(test_kospartial_type_is_koscallable_subtype);
  RUN_TEST(test_kospartial_is_koscallable);
  RUN_TEST(test_kospartial_not_functools_partial);
  RUN_TEST(test_kospartial_attributes);
  RUN_TEST(test_kospartial_refcount_func);
  RUN_TEST(test_kospartial_refcount_bound_arg);
  RUN_TEST(test_kospartial_flatten_refcounts);
  RUN_TEST(test_kospartial_call_merges);
  RUN_TEST(test_kospartial_reduce);
  RUN_TEST(test_kospartial_instance_dict);

  return 0;
}


// The end.
