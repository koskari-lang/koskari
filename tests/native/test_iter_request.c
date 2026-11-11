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

   C-level tests for the iteration request API and the code-less
   frame helpers: iterator acquisition and close,
   KosResumeFrame_Install with and without a cached frame, the inert
   caching, the request-next node lifecycle over a sync iterator, and
   KosContext_Recycle.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* a kind that never delivers: the install and cache tests only read
   its name */
KOS_RESUME_KIND(iterreq_kind, "iterreq-test", NULL, NULL, NULL);


/* a kind whose clear records the state it was handed */
static void *cleared_state = NULL;
static int cleared_calls = 0;

static void recording_clear(void *state) {
  cleared_state = state;
  cleared_calls++;
}

KOS_RESUME_KIND(recording_kind, "recording-test", NULL, recording_clear,
                NULL);


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


/* === kos_dynframe_cache_inert === */


static int test_cache_inert_strips_and_caches(void) {
  KosDynFrame *cache = NULL;

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "create code-less frame");
  KosDynFrame_CACHE_SLOT(frame) = (void *) frame;
  KosDynFrame_RESUME_KIND(frame) = &iterreq_kind;

  int cached = kos_dynframe_cache_inert(frame, &cache);
  TEST_ASSERT_EQ(cached, 1, "frame is cached into the empty cache");
  TEST_ASSERT_SAME(cache, frame, "cache holds the frame");
  TEST_ASSERT_NULL(KosDynFrame_CACHE_SLOT(frame), "cache slot stripped");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_NAME(frame), "resume name stripped");
  TEST_ASSERT_NULL(KosDynNode_PARENT(frame), "parent cleared");
  TEST_ASSERT_EQ(KosDynFrame_FLAGS(frame), 0, "frame flags cleared");

  /* an occupied cache refuses a second frame */
  KosDynFrame *other = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(other, "create second frame");
  cached = kos_dynframe_cache_inert(other, &cache);
  TEST_ASSERT_EQ(cached, 0, "occupied cache refuses");
  kos_dynframe_free(other);

  kos_dynframe_free(cache);
  return 0;
}


static int test_cache_inert_refuses_code(void) {
  KosDynFrame *cache = NULL;

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns dict");
  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame with ns");

  /* an ns alone is only the fill and is released by the caching; a
     cache-participation fn refuses */
  Py_INCREF(Py_None);
  KosDynFrame_FN(frame) = Py_None;
  int cached = kos_dynframe_cache_inert(frame, &cache);
  TEST_ASSERT_EQ(cached, 0, "fn-tagged frame is refused");
  Py_CLEAR(KosDynFrame_FN(frame));

  cached = kos_dynframe_cache_inert(frame, &cache);
  TEST_ASSERT_EQ(cached, 1, "fn-less frame is cached");
  TEST_ASSERT_NULL(KosDynFrame_NS(frame), "the ns fill was released");

  kos_dynframe_free(cache);
  return 0;
}


/* === KosResumeFrame_Install === */


static int test_native_install_fresh_and_cached(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  /* a base frame with a namespace, so the fill has a live value */
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create base ns");
  Py_INCREF(ns);
  KosDynFrame *base = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(base, "push base frame");

  PyObject *staged = PyLong_FromLong(42);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");

  KosDynFrame *frame = KosResumeFrame_Install(
      ctx, &iterreq_kind, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "install code-less frame");
  TEST_ASSERT(KosDynFrame_RESUME_NAME(frame)
              && ! strcmp(KosDynFrame_RESUME_NAME(frame), "iterreq-test"),
              "resume name set");
  TEST_ASSERT(KosContext_StackPushRef(ctx, staged) >= 0,
              "stage a value on the new frame");
  TEST_ASSERT_SAME(KosDynFrame_NS(frame), KosNamespace_Empty(),
                   "frame carries the empty namespace");
  TEST_ASSERT_SAME((KosDynFrame *) KosContext_DYN_TOP(ctx), frame,
                   "native frame is dyn_top");

  {
    PyObject *popped = KosContext_StackPopRef(ctx);
    TEST_ASSERT_SAME(popped, staged, "staged value is pushed on the stack");
    Py_XDECREF(popped);
  }
  Py_DECREF(staged);

  /* reinstall from a cache: the cached frame is reused */
  {
    KosDynFrame *cache = NULL;
    KosDynFrame *spare = kos_dynframe_new(NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(spare, "create spare frame");
    TEST_ASSERT_EQ(kos_dynframe_cache_inert(spare, &cache), 1, "cache spare");

    KosDynFrame *reused = KosResumeFrame_Install(
        ctx, &iterreq_kind, NULL, &cache);
    TEST_ASSERT_SAME(reused, spare, "cached frame is reinstalled");
    TEST_ASSERT_NULL(cache, "cache emptied by the reinstall");
    TEST_ASSERT_SAME(KosDynFrame_NS(reused), KosNamespace_Empty(),
                     "reinstall attaches the empty namespace");
  }

  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  Py_DECREF(ns);
  return 0;
}


/* === KosNativeFrame_RequestNext over a sync iterator === */


/* An installed frame carries its kind and its state together: a
   teardown at any point after the install hands the kind's clear the
   state the install was given, never NULL. */
static int test_native_install_clear_sees_state(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create base ns");
  Py_INCREF(ns);
  KosDynFrame *base = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(base, "push base frame");

  int marker = 0;
  cleared_state = NULL;
  cleared_calls = 0;

  KosDynFrame *frame = KosResumeFrame_Install(
      ctx, &recording_kind, &marker, NULL);
  TEST_ASSERT_NOT_NULL(frame, "install code-less frame");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_KIND(frame), &recording_kind,
                   "the install set the kind");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_STATE(frame), &marker,
                   "the install set the state with the kind");

  /* tear the frame down as the step abort would, before any delivery */
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);

  TEST_ASSERT_EQ(cleared_calls, 1, "the kind's clear ran once");
  TEST_ASSERT_SAME(cleared_state, &marker,
                   "the clear was handed the install's state");

  Py_DECREF(ns);
  return 0;
}


static int test_request_next_sync_lifecycle(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  KosDynFrame *frame = KosResumeFrame_Install(
      ctx, &iterreq_kind, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "install code-less frame");

  PyObject *lst = make_list123();
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  for (long expect = 1; expect <= 3; expect++) {
    PyObject *out = NULL;
    KosIterRequestStatus st = KosNativeFrame_RequestNext(ctx, it, &out);
    TEST_ASSERT_EQ(st, KOS_IREQ_DELIVERED, "request delivers");
    TEST_ASSERT_NOT_NULL(out, "element out");
    TEST_ASSERT_EQ(PyLong_AsLong(out), expect, "element value");
    Py_DECREF(out);
    TEST_ASSERT_SAME((KosDynFrame *) KosContext_DYN_TOP(ctx), frame,
                     "tracking node cleaned up after delivery");
  }

  {
    PyObject *out = NULL;
    KosIterRequestStatus st = KosNativeFrame_RequestNext(ctx, it, &out);
    TEST_ASSERT_EQ(st, KOS_IREQ_EXHAUSTED, "request reports exhaustion");
    TEST_ASSERT_NULL(out, "no element on exhaustion");
    TEST_ASSERT(! PyErr_Occurred(), "exhaustion sets no exception");
    TEST_ASSERT_SAME((KosDynFrame *) KosContext_DYN_TOP(ctx), frame,
                     "tracking node cleaned up after exhaustion");
  }

  Py_DECREF(it);
  Py_DECREF(lst);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* === KosContext_Recycle === */


static int test_context_recycle(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  {
    KosContext_EvalEnter(ctx, NULL);
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push a frame");
    KosContext_EvalExit(ctx);
  }

  TEST_ASSERT_NOT_NULL(KosContext_DYN_TOP(ctx), "chain holds the frame");
  TEST_ASSERT_EQ(KosContext_Recycle(ctx), 0, "recycle succeeds");
  TEST_ASSERT_NULL(KosContext_DYN_TOP(ctx), "chain dropped");
  TEST_ASSERT_NULL(KosContext_FRAME(ctx), "frame_top cleared");

  /* an active transfer goal refuses the recycle */
  KosContext_GOAL(ctx) = (KosXferGoal *) ctx;  /* sentinel */
  TEST_ASSERT_EQ(KosContext_Recycle(ctx), -1, "active goal refuses");
  KosContext_GOAL(ctx) = NULL;

  Py_DECREF(ctx);
  return 0;
}


/* === Suite === */


int run_iter_request_tests(void) {
  printf("\n=== Iteration request API tests ===\n");

  RUN_TEST(test_iter_acquire_sync);
  RUN_TEST(test_iter_close_quiet);
  RUN_TEST(test_cache_inert_strips_and_caches);
  RUN_TEST(test_cache_inert_refuses_code);
  RUN_TEST(test_native_install_fresh_and_cached);
  RUN_TEST(test_native_install_clear_sees_state);
  RUN_TEST(test_request_next_sync_lifecycle);
  RUN_TEST(test_context_recycle);

  return 0;
}


// The end.
