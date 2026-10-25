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
   Memory tests for the Demand wrapper.

   Calls a Demand from Python with keyword arguments, so each call
   takes the general entry arm and its code-less root frame is kept by
   the wrapper between calls, then drops the wrapper so Valgrind / ASan
   see the kept frame released.

   Calls a BoundMethod over a Demand through PyObject_Vectorcall with
   and without the lent offset slot, with keywords, and with more
   arguments than its stack buffer holds.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === helpers === */


/* Return a new reference to builtins.int, which takes base as a
   keyword. */
static PyObject *get_int_builtin(void) {

  PyObject *builtins = PyImport_ImportModule("builtins");
  PyObject *int_fn;

  if (! builtins)
    return NULL;
  int_fn = PyObject_GetAttrString(builtins, "int");
  Py_DECREF(builtins);
  return int_fn;
}


/* A Demand over KosPartial(func): a KosCallable that is not a simple
   KosFunction, so every boundary call takes the general entry arm.
   Returns a new reference, or NULL on error. */
static PyObject *make_demand_over_partial(PyObject *func) {

  PyObject *partial = PyObject_CallOneArg(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosPartialCallableType), func);
  PyObject *demand;

  if (! partial)
    return NULL;
  demand = KosDemand_New(partial);
  Py_DECREF(partial);
  return demand;
}


/* Call demand(text, base=base) through PyObject_Vectorcall. Returns a
   new reference, or NULL on error. */
static PyObject *call_with_base(
    PyObject *demand,
    const char *text,
    long base) {

  PyObject *argv[2];
  PyObject *kwnames;
  PyObject *result = NULL;

  argv[0] = PyUnicode_FromString(text);
  argv[1] = PyLong_FromLong(base);
  kwnames = Py_BuildValue("(s)", "base");
  if (argv[0] && argv[1] && kwnames)
    result = PyObject_Vectorcall(demand, argv, 1, kwnames);
  Py_XDECREF(argv[0]);
  Py_XDECREF(argv[1]);
  Py_XDECREF(kwnames);
  return result;
}


/* A BoundMethod over Demand(KosPartial(builtins.name)) with the given
   self. Returns a new reference, or NULL on error. */
static PyObject *make_bound_over_builtin(const char *name, long self) {

  PyObject *builtins = PyImport_ImportModule("builtins");
  PyObject *fn;
  PyObject *demand;
  PyObject *bound_self;
  PyObject *bm = NULL;

  if (! builtins)
    return NULL;
  fn = PyObject_GetAttrString(builtins, name);
  Py_DECREF(builtins);
  if (! fn)
    return NULL;

  demand = make_demand_over_partial(fn);
  Py_DECREF(fn);
  if (! demand)
    return NULL;

  bound_self = PyLong_FromLong(self);
  if (bound_self)
    bm = KosBoundMethod_New(demand, bound_self);
  Py_XDECREF(bound_self);
  Py_DECREF(demand);
  return bm;
}


/* === tests === */


static int test_demand_keyword_calls_keep_root_frame(void) {
  /*
    Two keyword calls through one Demand return their values, the
    wrapper holds a root frame after each, and dropping the wrapper
    releases it.
  */
  PyObject *int_fn = get_int_builtin();
  TEST_ASSERT_NOT_NULL(int_fn, "builtins.int");

  PyObject *demand = make_demand_over_partial(int_fn);
  Py_DECREF(int_fn);
  TEST_ASSERT_NOT_NULL(demand, "Demand(KosPartial(int))");

  PyObject *first = call_with_base(demand, "ff", 16);
  TEST_ASSERT_NOT_NULL(first, "first keyword call");
  TEST_ASSERT_EQ(PyLong_AsLong(first), 255, "first call result");
  Py_DECREF(first);
  TEST_ASSERT_NOT_NULL(((KosDemandObject *) demand)->cached_frame,
                       "root frame kept after the first call");

  PyObject *second = call_with_base(demand, "777", 8);
  TEST_ASSERT_NOT_NULL(second, "second keyword call");
  TEST_ASSERT_EQ(PyLong_AsLong(second), 511, "second call result");
  Py_DECREF(second);
  TEST_ASSERT_NOT_NULL(((KosDemandObject *) demand)->cached_frame,
                       "root frame kept after the second call");

  TEST_ASSERT_REFCNT(demand, 1, "Demand refcount before drop");
  Py_DECREF(demand);

  return 0;
}


static int test_boundmethod_vectorcall_positional(void) {
  /*
    BoundMethod(Demand(pow), 2) called with one positional and no
    offset slot: pow(2, 10).
  */
  PyObject *bm = make_bound_over_builtin("pow", 2);
  TEST_ASSERT_NOT_NULL(bm, "BoundMethod over pow");

  PyObject *argv[1];
  argv[0] = PyLong_FromLong(10);
  TEST_ASSERT_NOT_NULL(argv[0], "argument");

  PyObject *result = PyObject_Vectorcall(bm, argv, 1, NULL);
  Py_DECREF(argv[0]);
  TEST_ASSERT_NOT_NULL(result, "positional call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 1024, "pow(2, 10)");
  Py_DECREF(result);

  TEST_ASSERT_REFCNT(bm, 1, "BoundMethod refcount before drop");
  Py_DECREF(bm);

  return 0;
}


static int test_boundmethod_vectorcall_offset_slot(void) {
  /*
    With PY_VECTORCALL_ARGUMENTS_OFFSET the slot below the vector is
    lent for the call: the result is right and the slot's previous
    value is back afterward, its refcount untouched; once positional
    only, once with a keyword.
  */
  PyObject *bm = make_bound_over_builtin("pow", 2);
  TEST_ASSERT_NOT_NULL(bm, "BoundMethod over pow");

  PyObject *sentinel = PyObject_CallNoArgs((PyObject *) &PyBaseObject_Type);
  TEST_ASSERT_NOT_NULL(sentinel, "sentinel object");
  Py_ssize_t sentinel_refs = Py_REFCNT(sentinel);

  PyObject *exp = PyLong_FromLong(10);
  PyObject *mod = PyLong_FromLong(1000);
  PyObject *kwnames = Py_BuildValue("(s)", "mod");
  TEST_ASSERT_NOT_NULL(exp, "exp");
  TEST_ASSERT_NOT_NULL(mod, "mod");
  TEST_ASSERT_NOT_NULL(kwnames, "kwnames");

  PyObject *buf[3];
  buf[0] = sentinel;
  buf[1] = exp;
  buf[2] = mod;

  PyObject *result = PyObject_Vectorcall(
      bm, buf + 1, 1 | PY_VECTORCALL_ARGUMENTS_OFFSET, NULL);
  TEST_ASSERT_NOT_NULL(result, "offset positional call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 1024, "pow(2, 10)");
  Py_DECREF(result);
  TEST_ASSERT_SAME(buf[0], sentinel, "slot restored after the call");
  TEST_ASSERT_SAME(buf[1], exp, "argument slot unchanged");

  result = PyObject_Vectorcall(
      bm, buf + 1, 1 | PY_VECTORCALL_ARGUMENTS_OFFSET, kwnames);
  TEST_ASSERT_NOT_NULL(result, "offset keyword call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 24, "pow(2, 10, mod=1000)");
  Py_DECREF(result);
  TEST_ASSERT_SAME(buf[0], sentinel, "slot restored after the keyword call");
  TEST_ASSERT_SAME(buf[2], mod, "keyword value slot unchanged");

  TEST_ASSERT_EQ(Py_REFCNT(sentinel), sentinel_refs,
                 "sentinel refcount unchanged");

  Py_DECREF(kwnames);
  Py_DECREF(mod);
  Py_DECREF(exp);
  Py_DECREF(sentinel);
  TEST_ASSERT_REFCNT(bm, 1, "BoundMethod refcount before drop");
  Py_DECREF(bm);

  return 0;
}


static int test_boundmethod_vectorcall_kwnames(void) {
  /*
    BoundMethod(Demand(pow), 2) called with a positional and a keyword
    and no offset slot: pow(2, 10, mod=1000).
  */
  PyObject *bm = make_bound_over_builtin("pow", 2);
  TEST_ASSERT_NOT_NULL(bm, "BoundMethod over pow");

  PyObject *argv[2];
  argv[0] = PyLong_FromLong(10);
  argv[1] = PyLong_FromLong(1000);
  PyObject *kwnames = Py_BuildValue("(s)", "mod");
  TEST_ASSERT_NOT_NULL(argv[0], "exp");
  TEST_ASSERT_NOT_NULL(argv[1], "mod");
  TEST_ASSERT_NOT_NULL(kwnames, "kwnames");

  PyObject *result = PyObject_Vectorcall(bm, argv, 1, kwnames);
  Py_DECREF(kwnames);
  Py_DECREF(argv[0]);
  Py_DECREF(argv[1]);
  TEST_ASSERT_NOT_NULL(result, "keyword call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 24, "pow(2, 10, mod=1000)");
  Py_DECREF(result);

  TEST_ASSERT_REFCNT(bm, 1, "BoundMethod refcount before drop");
  Py_DECREF(bm);

  return 0;
}


static int test_boundmethod_vectorcall_many_positionals(void) {
  /*
    BoundMethod(Demand(max), 0) called with nine positionals and no
    offset slot, more than the stack buffer holds: max(0, 1, ..., 9).
  */
  PyObject *bm = make_bound_over_builtin("max", 0);
  TEST_ASSERT_NOT_NULL(bm, "BoundMethod over max");

  PyObject *argv[9];
  for (int i = 0; i < 9; i++) {
    argv[i] = PyLong_FromLong(i + 1);
    TEST_ASSERT_NOT_NULL(argv[i], "argument");
  }

  PyObject *result = PyObject_Vectorcall(bm, argv, 9, NULL);
  for (int i = 0; i < 9; i++)
    Py_DECREF(argv[i]);
  TEST_ASSERT_NOT_NULL(result, "nine-positional call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 9, "max(0, 1, ..., 9)");
  Py_DECREF(result);

  TEST_ASSERT_REFCNT(bm, 1, "BoundMethod refcount before drop");
  Py_DECREF(bm);

  return 0;
}


static int test_boundmethod_vectorcall_no_arguments(void) {
  /*
    BoundMethod(Demand(abs), -5) called with an empty, NULL vector:
    abs(-5).
  */
  PyObject *bm = make_bound_over_builtin("abs", -5);
  TEST_ASSERT_NOT_NULL(bm, "BoundMethod over abs");

  PyObject *result = PyObject_Vectorcall(bm, NULL, 0, NULL);
  TEST_ASSERT_NOT_NULL(result, "no-argument call");
  TEST_ASSERT_EQ(PyLong_AsLong(result), 5, "abs(-5)");
  Py_DECREF(result);

  TEST_ASSERT_REFCNT(bm, 1, "BoundMethod refcount before drop");
  Py_DECREF(bm);

  return 0;
}


/* === suite entry point === */


int run_demand_tests(void) {

  BEGIN_TEST_SUITE("Demand Memory Tests");

  RUN_TEST(test_demand_keyword_calls_keep_root_frame);
  RUN_TEST(test_boundmethod_vectorcall_positional);
  RUN_TEST(test_boundmethod_vectorcall_offset_slot);
  RUN_TEST(test_boundmethod_vectorcall_kwnames);
  RUN_TEST(test_boundmethod_vectorcall_many_positionals);
  RUN_TEST(test_boundmethod_vectorcall_no_arguments);

  return 0;
}


// The end.
