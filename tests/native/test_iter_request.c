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
   tests/native/test_iter_request.c

   C-level tests for the iteration request API and the native-frame
   helpers: iterator acquisition and close, KosNativeFrame_Install
   with and without a parked frame, the inert park, the request-next
   node lifecycle over a sync iterator, and KosContext_Recycle.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* A descriptor whose handler must never run in these tests. */
static KosOpResult iterreq_dummy_handler(
    KosEvalState *es,
    KosNativeFrameEvent ev,
    PyObject *value) {

  (void) es;
  (void) ev;
  Py_XDECREF(value);
  PyErr_SetString(PyExc_SystemError, "dummy native handler invoked");
  return EVAL_RAISE();
}


static const KosNativeProgram iterreq_dummy_program = {
  .np_name = "iterreq-test",
  .np_handler = iterreq_dummy_handler,
  .np_clone_state = NULL,
  .np_free_state = NULL,
};


static PyObject *make_list123(void) {
  PyObject *lst = PyList_New(0);
  if (! lst)
    return NULL;
  for (long i = 1; i <= 3; i++) {
    PyObject *n = PyLong_FromLong(i);
    if (! n || PyList_Append(lst, n) < 0) {
      Py_XDECREF(n);
      Py_DECREF(lst);
      return NULL;
    }
    Py_DECREF(n);
  }
  return lst;
}


/* === kos_iter_acquire / kos_iter_close === */


static int test_iter_acquire_sync(void) {
  PyObject *lst = make_list123();
  TEST_ASSERT_NOT_NULL(lst, "create list");

  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire list iterator");
  TEST_ASSERT(PyIter_Check(it), "acquired object is a sync iterator");

  /* acquiring an iterator passes it through as itself */
  PyObject *again = kos_iter_acquire(it);
  TEST_ASSERT_NOT_NULL(again, "acquire of an iterator");
  TEST_ASSERT_SAME(again, it, "iterator passes through as itself");

  Py_DECREF(again);
  Py_DECREF(it);
  Py_DECREF(lst);
  return 0;
}


static int test_iter_close_quiet(void) {
  PyObject *lst = make_list123();
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  /* NULL is a no-op */
  kos_iter_close(NULL);
  TEST_ASSERT(! PyErr_Occurred(), "close(NULL) sets no error");

  /* a sync iterator with no close method closes quietly */
  kos_iter_close(it);
  TEST_ASSERT(! PyErr_Occurred(), "close without close() sets no error");

  /* a pending exception survives the close */
  PyErr_SetString(PyExc_ValueError, "pending");
  kos_iter_close(it);
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "pending exception preserved across close");
  PyErr_Clear();

  Py_DECREF(it);
  Py_DECREF(lst);
  return 0;
}


/* === kos_dynframe_park_inert === */


static int test_park_inert_strips_and_parks(void) {
  KosDynFrame *cache = NULL;

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "create code-less frame");
  KosDynFrame_NATIVE_PROG(frame) = (void *) &iterreq_dummy_program;
  KosDynFrame_NATIVE_STATE(frame) = (void *) frame;

  int parked = kos_dynframe_park_inert(frame, &cache);
  TEST_ASSERT_EQ(parked, 1, "frame parks into the empty cache");
  TEST_ASSERT_SAME(cache, frame, "cache holds the frame");
  TEST_ASSERT_NULL(KosDynFrame_NATIVE_PROG(frame), "program stripped");
  TEST_ASSERT_NULL(KosDynFrame_NATIVE_STATE(frame), "state stripped");
  TEST_ASSERT_NULL(KosDynNode_PARENT(frame), "parent cleared");
  TEST_ASSERT_EQ(KosDynFrame_FLAGS(frame), 0, "frame flags cleared");

  /* an occupied cache refuses a second frame */
  KosDynFrame *other = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(other, "create second frame");
  parked = kos_dynframe_park_inert(other, &cache);
  TEST_ASSERT_EQ(parked, 0, "occupied cache refuses");
  kos_dynframe_free(other);

  kos_dynframe_free(cache);
  return 0;
}


static int test_park_inert_refuses_code(void) {
  KosDynFrame *cache = NULL;

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns dict");
  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame with ns");

  /* an ns alone is only the fill and is released by the park; a
     cache-participation fn refuses */
  Py_INCREF(Py_None);
  KosDynFrame_FN(frame) = Py_None;
  int parked = kos_dynframe_park_inert(frame, &cache);
  TEST_ASSERT_EQ(parked, 0, "fn-tagged frame refuses to park");
  Py_CLEAR(KosDynFrame_FN(frame));

  parked = kos_dynframe_park_inert(frame, &cache);
  TEST_ASSERT_EQ(parked, 1, "fn-less frame parks");
  TEST_ASSERT_NULL(KosDynFrame_NS(frame), "the ns fill was released");

  kos_dynframe_free(cache);
  return 0;
}


/* === KosNativeFrame_Install === */


static int test_native_install_fresh_and_cached(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  /* a base frame with a namespace, so the fill has a live value */
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create base ns");
  Py_INCREF(ns);
  KosDynFrame *base = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(base, "push base frame");

  PyObject *staged = PyLong_FromLong(42);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");

  KosDynFrame *frame = KosNativeFrame_Install(
      &es, &iterreq_dummy_program, (void *) ctx, staged, NULL);
  TEST_ASSERT_NOT_NULL(frame, "install native frame");
  TEST_ASSERT_SAME(KosDynFrame_NATIVE_PROG(frame),
                   (void *) &iterreq_dummy_program, "program set");
  TEST_ASSERT_SAME(KosDynFrame_NATIVE_STATE(frame), (void *) ctx,
                   "state set");
  TEST_ASSERT_SAME(KosDynFrame_NS(frame), ns,
                   "frame carries the owned fill of the live namespace");
  TEST_ASSERT_SAME((KosDynFrame *) KosEvalState_DYN_TOP(&es), frame,
                   "native frame is dyn_top");

  {
    PyObject *popped = KosEvalState_StackPopRef(&es);
    TEST_ASSERT_SAME(popped, staged, "staged value lands on the stack");
    Py_XDECREF(popped);
  }
  Py_DECREF(staged);

  /* reinstall from a cache: the parked frame is reused */
  {
    KosDynFrame *cache = NULL;
    KosDynFrame *spare = kos_dynframe_new(NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(spare, "create spare frame");
    TEST_ASSERT_EQ(kos_dynframe_park_inert(spare, &cache), 1, "park spare");

    KosDynFrame *reused = KosNativeFrame_Install(
        &es, &iterreq_dummy_program, (void *) ctx, NULL, &cache);
    TEST_ASSERT_SAME(reused, spare, "cached frame is reinstalled");
    TEST_ASSERT_NULL(cache, "cache emptied by the reinstall");
    TEST_ASSERT_SAME(KosDynFrame_NS(reused), ns,
                     "reinstall attaches a fresh namespace fill");
  }

  KosEvalState_FreezeToContext(&es);
  KosContext_Free(ctx);
  Py_DECREF(ns);
  return 0;
}


/* === KosNativeFrame_RequestNext over a sync iterator === */


static int test_request_next_sync_lifecycle(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  KosDynFrame *frame = KosNativeFrame_Install(
      &es, &iterreq_dummy_program, NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "install native frame");

  PyObject *lst = make_list123();
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  for (long expect = 1; expect <= 3; expect++) {
    PyObject *out = NULL;
    KosIterRequestStatus st = KosNativeFrame_RequestNext(&es, it, &out);
    TEST_ASSERT_EQ(st, KOS_IREQ_DELIVERED, "request delivers");
    TEST_ASSERT_NOT_NULL(out, "element out");
    TEST_ASSERT_EQ(PyLong_AsLong(out), expect, "element value");
    Py_DECREF(out);
    TEST_ASSERT_SAME((KosDynFrame *) KosEvalState_DYN_TOP(&es), frame,
                     "tracking node cleaned up after delivery");
  }

  {
    PyObject *out = NULL;
    KosIterRequestStatus st = KosNativeFrame_RequestNext(&es, it, &out);
    TEST_ASSERT_EQ(st, KOS_IREQ_EXHAUSTED, "request reports exhaustion");
    TEST_ASSERT_NULL(out, "no element on exhaustion");
    TEST_ASSERT(! PyErr_Occurred(), "exhaustion sets no exception");
    TEST_ASSERT_SAME((KosDynFrame *) KosEvalState_DYN_TOP(&es), frame,
                     "tracking node cleaned up after exhaustion");
  }

  Py_DECREF(it);
  Py_DECREF(lst);
  KosEvalState_FreezeToContext(&es);
  KosContext_Free(ctx);
  return 0;
}


/* === KosContext_Recycle === */


static int test_context_recycle(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  {
    KosEvalState es;
    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(&es), "push a frame");
    KosEvalState_FreezeToContext(&es);
  }

  TEST_ASSERT_NOT_NULL(KosContext_DYN_TOP(ctx), "chain holds the frame");
  TEST_ASSERT_EQ(KosContext_Recycle(ctx), 0, "recycle succeeds");
  TEST_ASSERT_NULL(KosContext_DYN_TOP(ctx), "chain dropped");
  TEST_ASSERT_NULL(KosContext_FRAME_TOP(ctx), "frame_top cleared");

  /* an armed transfer plan refuses the recycle */
  KosContext_PLAN(ctx) = (KosTransferPlan *) ctx;  /* sentinel */
  TEST_ASSERT_EQ(KosContext_Recycle(ctx), -1, "armed plan refuses");
  KosContext_PLAN(ctx) = NULL;

  KosContext_Free(ctx);
  return 0;
}


/* === Suite === */


int run_iter_request_tests(void) {
  printf("\n=== Iteration request API tests ===\n");

  RUN_TEST(test_iter_acquire_sync);
  RUN_TEST(test_iter_close_quiet);
  RUN_TEST(test_park_inert_strips_and_parks);
  RUN_TEST(test_park_inert_refuses_code);
  RUN_TEST(test_native_install_fresh_and_cached);
  RUN_TEST(test_request_next_sync_lifecycle);
  RUN_TEST(test_context_recycle);

  return 0;
}


// The end.
