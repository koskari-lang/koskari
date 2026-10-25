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
   tests/native/test_iterate.c

   The Python adapters over a KosIter provider. Two adapters over one
   transform share its durable drive context; a step or throw through
   the second while the provider is busy is refused without touching
   that context, so the drive in flight keeps its breaker.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* the breaker a drive in flight is polling; any address of our own */
static KosBreakerWord sentinel_breaker = 0;


/* === helpers === */


/**
   A map transform over a short list, its adapter, and its durable
   context. Returns 0 with the three set, or -1 with an error set.
   The context is borrowed from the transform.
*/
static int make_busy_map(
    PyObject **out_t,
    PyObject **out_adapter,
    KosContext **out_ctx) {

  PyObject *items = Py_BuildValue("[iii]", 1, 2, 3);
  PyObject *t;

  if (! items)
    return -1;

  t = PyObject_CallFunction((PyObject *) &KosMapType, "OO",
                            (PyObject *) &PyLong_Type, items);
  Py_DECREF(items);
  if (! t)
    return -1;

  *out_adapter = PyObject_GetIter(t);
  if (! *out_adapter) {
    Py_DECREF(t);
    return -1;
  }

  *out_ctx = kos_transform_pyctx(t);
  if (! *out_ctx) {
    Py_DECREF(*out_adapter);
    Py_DECREF(t);
    PyErr_SetString(PyExc_RuntimeError, "no durable context");
    return -1;
  }

  /* a step in flight elsewhere: the provider is busy and its context
     carries that drive's breaker */
  ((KosTransform *) t)->busy = 1;
  KosContext_BREAKER(*out_ctx) = &sentinel_breaker;

  *out_t = t;
  return 0;
}


static void release_busy_map(PyObject *t, PyObject *adapter) {

  ((KosTransform *) t)->busy = 0;
  Py_DECREF(adapter);
  Py_DECREF(t);
}


/* === tests === */


static int test_busy_adapter_leaves_context_untouched(void) {

  PyObject *t = NULL, *adapter = NULL, *res;
  KosContext *ctx = NULL;
  const volatile KosBreakerWord *seen;
  int refused;

  TEST_ASSERT_EQ(make_busy_map(&t, &adapter, &ctx), 0,
                 "build a busy map with an adapter");

  res = PyIter_Next(adapter);
  refused = (! res && PyErr_Occurred()
             && PyErr_ExceptionMatches(PyExc_ValueError));
  seen = KosContext_BREAKER(ctx);

  Py_XDECREF(res);
  PyErr_Clear();
  release_busy_map(t, adapter);

  TEST_ASSERT(refused, "the busy step raises ValueError");
  TEST_ASSERT_SAME((void *) seen, (void *) &sentinel_breaker,
                   "the refused step leaves the context's breaker");
  return 0;
}


static int test_busy_adapter_throw_leaves_context_untouched(void) {

  PyObject *t = NULL, *adapter = NULL, *res;
  KosContext *ctx = NULL;
  const volatile KosBreakerWord *seen;
  int refused;

  TEST_ASSERT_EQ(make_busy_map(&t, &adapter, &ctx), 0,
                 "build a busy map with an adapter");

  res = PyObject_CallMethod(adapter, "throw", "O",
                            (PyObject *) PyExc_KeyError);
  refused = (! res && PyErr_Occurred()
             && PyErr_ExceptionMatches(PyExc_ValueError));
  seen = KosContext_BREAKER(ctx);

  Py_XDECREF(res);
  PyErr_Clear();
  release_busy_map(t, adapter);

  TEST_ASSERT(refused, "the busy throw raises ValueError");
  TEST_ASSERT_SAME((void *) seen, (void *) &sentinel_breaker,
                   "the refused throw leaves the context's breaker");
  return 0;
}


int run_iterate_tests(void) {

  BEGIN_TEST_SUITE("Iterate Adapter Tests");

  RUN_TEST(test_busy_adapter_leaves_context_untouched);
  RUN_TEST(test_busy_adapter_throw_leaves_context_untouched);

  return 0;
}


// The end.
