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


/* === suite entry point === */


int run_demand_tests(void) {

  BEGIN_TEST_SUITE("Demand Memory Tests");

  RUN_TEST(test_demand_keyword_calls_keep_root_frame);

  return 0;
}


// The end.
