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
   Native tests for the per-thread record behind the ambient context
   stack: one OS thread swapping between two thread states keeps a
   separate active-context stack for each, and clearing one thread
   state from the other leaves the survivor answering.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


static int test_domain_two_thread_states_on_one_os_thread(void) {
  PyThreadState *first = PyThreadState_Get();
  PyThreadState *second;
  KosContext *a;
  KosContext *b;

  second = PyThreadState_New(PyThreadState_GetInterpreter(first));
  TEST_ASSERT_NOT_NULL(second, "second thread state");

  a = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(a, "context a");
  b = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(b, "context b");

  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a, "a is current on the first state");

  PyThreadState_Swap(second);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state starts empty");
  kos_domain_push_scope(b);
  TEST_ASSERT_SAME(kos_ctx_current(), b, "b is current on the second state");

  PyThreadState_Swap(first);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current again after the swap back");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the first state is empty after pop");

  PyThreadState_Swap(second);
  TEST_ASSERT_SAME(kos_ctx_current(), b,
                   "b is current again after the swap back");
  kos_domain_pop_scope(b);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state is empty after pop");

  /* clearing the second state from the first frees its record; the
     first state's record still answers */
  PyThreadState_Swap(first);
  PyThreadState_Clear(second);
  PyThreadState_Delete(second);

  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current after the other state is gone");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the first state is empty at the end");

  Py_DECREF(a);
  Py_DECREF(b);
  return 0;
}


/* Last in the suite: it clears the domain unit, after which records
   are owned by the thread's slot alone. */
static int test_domain_record_survives_module_clear(void) {
  PyThreadState *first = PyThreadState_Get();
  PyThreadState *second;
  KosContext *a;

  a = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(a, "context a");

  koskari_domain_clear(NULL);

  /* a thread state with no record yet allocates one after the clear */
  second = PyThreadState_New(PyThreadState_GetInterpreter(first));
  TEST_ASSERT_NOT_NULL(second, "second thread state");
  PyThreadState_Swap(second);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state starts empty");
  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current on a record allocated after the clear");
  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a, "a is current after a second push");
  kos_domain_pop_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a, "a is current after one pop");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state is empty after pop");

  PyThreadState_Swap(first);
  PyThreadState_Clear(second);
  PyThreadState_Delete(second);
  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current on the first state after the clear");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the first state is empty at the end");

  Py_DECREF(a);
  return 0;
}


/* The ambient cell table, as a boundary entry on this thread sees it. */
static PyObject *ambient(KosTlsState *s) {
  return kos_param_ambient_cells(s, kos_tls_active_context(s));
}


/* A Python parameterize span binding one name, entered. New reference. */
static PyObject *span_enter(const char *name, long value) {
  PyObject *bindings = Py_BuildValue("{s:l}", name, value);
  PyObject *span;
  PyObject *entered;

  if (! bindings)
    return NULL;
  span = PyObject_CallFunction((PyObject *) &KosParamSpanType, "O", bindings);
  Py_DECREF(bindings);
  if (! span)
    return NULL;
  entered = PyObject_CallMethod(span, "__enter__", NULL);
  if (! entered) {
    Py_DECREF(span);
    return NULL;
  }
  Py_DECREF(entered);
  return span;
}


static int span_exit(PyObject *span) {
  PyObject *r = PyObject_CallMethod(span, "__exit__", "OOO",
                                    Py_None, Py_None, Py_None);
  if (! r)
    return -1;
  Py_DECREF(r);
  return 0;
}


/**
   The ambient cells order a Python span against the active context: a
   span opened before the context is entered is outer to it and the
   context's own table answers; a span opened while it is active is
   inner and the span's table answers; each exit uncovers the view
   beneath.
*/
static int test_domain_span_orders_against_the_active_context(void) {
  KosTlsState *s = kos_tls_current();
  PyObject *outer;
  PyObject *inner;
  KosContext *a;
  PyObject *cells;
  PyObject *mark;

  TEST_ASSERT_NULL(ambient(s), "nothing ambient at start");

  outer = span_enter("span_k", 1);
  TEST_ASSERT_NOT_NULL(outer, "outer span entered");
  cells = ambient(s);
  TEST_ASSERT_NOT_NULL(cells, "the outer span's table is ambient");
  TEST_ASSERT_NOT_NULL(PyDict_GetItemString(cells, "span_k"),
                       "the outer span's cell is in it");

  /* a context created inside the span takes the span's table */
  a = KosContext_New(NULL, s);
  TEST_ASSERT_NOT_NULL(a, "context a");
  TEST_ASSERT_SAME(KosContext_PARAM_REFS(a), cells,
                   "a took the span's table at creation");

  /* active, with a table of its own: the span is outer to it */
  kos_domain_push_scope(a);
  cells = kos_context_param_refs(a);
  TEST_ASSERT_NOT_NULL(cells, "a's own table");
  mark = PyLong_FromLong(2);
  TEST_ASSERT_EQ(PyDict_SetItemString(cells, "span_mark", mark), 0,
                 "mark a's table");
  Py_DECREF(mark);
  TEST_ASSERT_SAME(ambient(s), cells,
                   "a's table is ambient while a is active");

  /* a span opened while a is active is inner to it */
  inner = span_enter("span_inner", 3);
  TEST_ASSERT_NOT_NULL(inner, "inner span entered");
  cells = ambient(s);
  TEST_ASSERT_NOT_SAME(cells, KosContext_PARAM_REFS(a),
                       "the inner span's table is ambient");
  TEST_ASSERT_NOT_NULL(PyDict_GetItemString(cells, "span_inner"),
                       "it holds the inner span's cell");
  TEST_ASSERT_NOT_NULL(PyDict_GetItemString(cells, "span_mark"),
                       "it was built over a's table");

  TEST_ASSERT_EQ(span_exit(inner), 0, "inner span exited");
  TEST_ASSERT_SAME(ambient(s), KosContext_PARAM_REFS(a),
                   "a's table is ambient again");

  kos_domain_pop_scope(a);
  cells = ambient(s);
  TEST_ASSERT_NOT_NULL(cells, "the outer span answers once a is gone");
  TEST_ASSERT_NULL(PyDict_GetItemString(cells, "span_mark"),
                   "without a's mark");

  TEST_ASSERT_EQ(span_exit(outer), 0, "outer span exited");
  TEST_ASSERT_NULL(ambient(s), "nothing ambient at the end");

  Py_DECREF(inner);
  Py_DECREF(outer);
  Py_DECREF(a);
  return 0;
}


int run_domain_tests(void) {
  BEGIN_TEST_SUITE("Domain");
  RUN_TEST(test_domain_two_thread_states_on_one_os_thread);
  RUN_TEST(test_domain_span_orders_against_the_active_context);
  RUN_TEST(test_domain_record_survives_module_clear);
  return 0;
}


// The end.
