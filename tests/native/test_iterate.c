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


/* === a provider whose context cannot be had === */


static int failing_pyctx_calls = 0;
static int failing_step_calls = 0;


/* the durable context of the provider, never obtainable */
static KosContext *failing_pyctx(PyObject *self) {

  (void) self;
  failing_pyctx_calls++;
  PyErr_SetString(PyExc_RuntimeError, "no context for this provider");
  return NULL;
}


/* a step the adapters must never reach */
static KosIterStepResult failing_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  (void) ctx;
  (void) self;
  (void) send_val;
  (void) out;
  failing_step_calls++;
  return KOS_ITER_EXHAUSTED;
}


static KosIterStepResult failing_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  (void) ctx;
  (void) self;
  (void) throw_typ;
  (void) throw_exc;
  (void) out;
  failing_step_calls++;
  return KOS_ITER_EXHAUSTED;
}


static int failing_close(PyObject *self) {

  (void) self;
  return 0;
}


static void failing_finish(PyObject *self, int exhausted) {

  (void) self;
  (void) exhausted;
}


/* the suite with a durable context: the adapters ask for it before
   the drive begins */
static KosEvaluatedTypeObject FailingDurableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "test.failing_durable",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = failing_step,
    .ki_throw = failing_throw,
    .ki_close = failing_close,
    .ki_finish = failing_finish,
    .ki_pyctx = failing_pyctx,
    .ki_busy = NULL,
    .ki_pyctx_recyclable = 0,
  },
};


/* the suite with a recyclable context: the adapters ask for it as
   they acquire the step's context */
static KosEvaluatedTypeObject FailingRecyclableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "test.failing_recyclable",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = failing_step,
    .ki_throw = failing_throw,
    .ki_close = failing_close,
    .ki_finish = failing_finish,
    .ki_pyctx = failing_pyctx,
    .ki_busy = NULL,
    .ki_pyctx_recyclable = 1,
  },
};


/* An instance of one of the failing provider types, which
   ready_iterate_test_types readied. Returns a new reference, or NULL
   with an error set. */
static PyObject *make_failing_provider(KosEvaluatedTypeObject *type) {

  PyTypeObject *pytype = KOS_EVALUATED_PYTYPE(*type);

  failing_pyctx_calls = 0;
  failing_step_calls = 0;
  return PyObject_CallNoArgs((PyObject *) pytype);
}


/* Drive the provider once through the async stepper's send. Returns 1
   when the step failed with the provider's RuntimeError and the suite
   step was never reached, 0 otherwise; clears the error. */
static int stepper_send_fails_with_provider_error(PyObject *provider) {

  PyObject *stepper = KosIterAdapt_Anext(provider);
  PyObject *res;
  int failed;

  if (! stepper)
    return 0;
  res = PyObject_CallMethod(stepper, "send", "O", Py_None);
  failed = (! res && PyErr_ExceptionMatches(PyExc_RuntimeError)
            && failing_pyctx_calls == 1 && failing_step_calls == 0);
  Py_XDECREF(res);
  PyErr_Clear();
  Py_DECREF(stepper);
  return failed;
}


/* The same through the sync adapter's next. */
static int pyiter_next_fails_with_provider_error(PyObject *provider) {

  PyObject *adapter = KosIterAdapt_PyIter(provider);
  PyObject *res;
  int failed;

  if (! adapter)
    return 0;
  res = PyIter_Next(adapter);
  failed = (! res && PyErr_ExceptionMatches(PyExc_RuntimeError)
            && failing_pyctx_calls == 1 && failing_step_calls == 0);
  Py_XDECREF(res);
  PyErr_Clear();
  Py_DECREF(adapter);
  return failed;
}


/* === a provider that records its context at its death === */


/**
   A durable provider owning its context, as the async generator does.
   Its dealloc records how many references its context still has: an
   adapter that releases the context before the provider leaves only
   the provider's own.
*/
typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  int steps;
} OwningProvider;


static Py_ssize_t owning_ctx_refs_at_dealloc = -1;


static KosContext *owning_pyctx(PyObject *self) {

  OwningProvider *p = (OwningProvider *) self;

  if (! p->ctx)
    p->ctx = KosContext_New(NULL, kos_tls_current());
  return p->ctx;
}


/* one element, then exhaustion */
static KosIterStepResult owning_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  OwningProvider *p = (OwningProvider *) self;

  (void) ctx;
  (void) send_val;
  if (p->steps++) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }
  *out = Py_NewRef(Py_None);
  return KOS_ITER_VALUE;
}


static void owning_dealloc(PyObject *self) {

  OwningProvider *p = (OwningProvider *) self;

  owning_ctx_refs_at_dealloc = p->ctx ? Py_REFCNT(p->ctx) : 0;
  Py_CLEAR(p->ctx);
  Py_TYPE(self)->tp_free(self);
}


static KosEvaluatedTypeObject OwningDurableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "test.owning_durable",
    .tp_basicsize = sizeof(OwningProvider),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = owning_dealloc,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = owning_step,
    .ki_throw = failing_throw,
    .ki_close = failing_close,
    .ki_finish = failing_finish,
    .ki_pyctx = owning_pyctx,
    .ki_busy = NULL,
    .ki_pyctx_recyclable = 0,
  },
};


/**
   An owning provider and its sync adapter, stepped once so the
   adapter holds the provider's context, with the caller's reference
   to the provider dropped: the adapter holds the only one. Returns
   the adapter, or NULL with an error set.
*/
static PyObject *make_stepped_owning_adapter(void) {

  PyTypeObject *pytype = KOS_EVALUATED_PYTYPE(OwningDurableType);
  PyObject *provider, *adapter, *res;

  owning_ctx_refs_at_dealloc = -1;

  provider = PyObject_CallNoArgs((PyObject *) pytype);
  if (! provider)
    return NULL;

  adapter = KosIterAdapt_PyIter(provider);
  Py_DECREF(provider);
  if (! adapter)
    return NULL;

  res = PyIter_Next(adapter);
  if (! res) {
    Py_DECREF(adapter);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError, "the first step yielded nothing");
    return NULL;
  }
  Py_DECREF(res);
  return adapter;
}


/* === a source transform with its step in flight === */


/**
   A callable answering every element with an awaitable that suspends
   once, yielding 'tick', and then returns 1. Returns the callable, or
   NULL with an error set.
*/
static PyObject *make_ticking_callable(void) {

  PyObject *globals = PyDict_New();
  PyObject *ran, *fn;

  if (! globals)
    return NULL;

  ran = PyRun_String(
      "class Tick:\n"
      "    def __await__(self):\n"
      "        yield 'tick'\n"
      "        return 1\n"
      "def tick(x):\n"
      "    return Tick()\n",
      Py_file_input, globals, globals);
  if (! ran) {
    Py_DECREF(globals);
    return NULL;
  }
  Py_DECREF(ran);

  fn = PyDict_GetItemString(globals, "tick");
  Py_XINCREF(fn);
  Py_DECREF(globals);
  if (! fn)
    PyErr_SetString(PyExc_RuntimeError, "the prelude defined no tick");
  return fn;
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


static int test_durable_pyctx_failure_fails_stepper(void) {

  PyObject *provider = make_failing_provider(&FailingDurableType);
  int failed;

  TEST_ASSERT_NOT_NULL(provider, "build the durable provider");
  failed = stepper_send_fails_with_provider_error(provider);
  Py_DECREF(provider);

  TEST_ASSERT(failed, "the send fails with the provider's error, unstepped");
  return 0;
}


static int test_durable_pyctx_failure_fails_pyiter(void) {

  PyObject *provider = make_failing_provider(&FailingDurableType);
  int failed;

  TEST_ASSERT_NOT_NULL(provider, "build the durable provider");
  failed = pyiter_next_fails_with_provider_error(provider);
  Py_DECREF(provider);

  TEST_ASSERT(failed, "the next fails with the provider's error, unstepped");
  return 0;
}


static int test_recyclable_pyctx_failure_fails_stepper(void) {

  PyObject *provider = make_failing_provider(&FailingRecyclableType);
  int failed;

  TEST_ASSERT_NOT_NULL(provider, "build the recyclable provider");
  failed = stepper_send_fails_with_provider_error(provider);
  Py_DECREF(provider);

  TEST_ASSERT(failed, "the send fails with the provider's error, unstepped");
  return 0;
}


static int test_recyclable_pyctx_failure_fails_pyiter(void) {

  PyObject *provider = make_failing_provider(&FailingRecyclableType);
  int failed;

  TEST_ASSERT_NOT_NULL(provider, "build the recyclable provider");
  failed = pyiter_next_fails_with_provider_error(provider);
  Py_DECREF(provider);

  TEST_ASSERT(failed, "the next fails with the provider's error, unstepped");
  return 0;
}


static int test_pyiter_dealloc_releases_context_first(void) {

  PyObject *adapter = make_stepped_owning_adapter();

  TEST_ASSERT_NOT_NULL(adapter, "step the owning provider once");
  Py_DECREF(adapter);

  TEST_ASSERT_EQ(owning_ctx_refs_at_dealloc, 1,
                 "the adapter's dealloc drops the context first");
  return 0;
}


static int test_pyiter_clear_releases_context_first(void) {

  PyObject *adapter = make_stepped_owning_adapter();
  Py_ssize_t refs;

  TEST_ASSERT_NOT_NULL(adapter, "step the owning provider once");
  (void) Py_TYPE(adapter)->tp_clear(adapter);
  refs = owning_ctx_refs_at_dealloc;
  Py_DECREF(adapter);

  TEST_ASSERT_EQ(refs, 1, "the adapter's clear drops the context first");
  return 0;
}


static int test_pyiter_exhaustion_releases_context_first(void) {

  PyObject *adapter = make_stepped_owning_adapter();
  PyObject *res;
  Py_ssize_t refs;
  int exhausted;

  TEST_ASSERT_NOT_NULL(adapter, "step the owning provider once");
  res = PyIter_Next(adapter);
  exhausted = (! res && ! PyErr_Occurred());
  Py_XDECREF(res);
  PyErr_Clear();
  refs = owning_ctx_refs_at_dealloc;
  Py_DECREF(adapter);

  TEST_ASSERT(exhausted, "the second step exhausts the adapter");
  TEST_ASSERT_EQ(refs, 1, "the adapter's release drops the context first");
  return 0;
}


/**
   A map over a map, its step suspended on the inner map's awaitable,
   so the inner step's frame sits on the outer's context. The outer
   map's clear drops its source, the inner map, while that frame is
   still on the chain; the inner map stays alive until the context's
   clear frees the frame, which is seen through the inner map's source
   list, held by the inner map's iterator over it.
*/
static int test_staged_source_survives_containing_clear(void) {

  PyObject *tick = make_ticking_callable();
  PyObject *items, *inner, *outer, *stepper, *res;
  KosContext *ctx;
  Py_ssize_t held_after_clear, held_at_end;
  int ticked;

  TEST_ASSERT_NOT_NULL(tick, "build the ticking callable");
  items = Py_BuildValue("[ii]", 1, 2);
  TEST_ASSERT_NOT_NULL(items, "build the source list");

  inner = PyObject_CallFunction((PyObject *) &KosMapType, "OO", tick, items);
  Py_DECREF(tick);
  TEST_ASSERT_NOT_NULL(inner, "build the inner map");

  outer = PyObject_CallFunction((PyObject *) &KosMapType, "OO",
                                (PyObject *) &PyLong_Type, inner);
  /* the outer map holds the only reference to the inner one */
  Py_DECREF(inner);
  TEST_ASSERT_NOT_NULL(outer, "build the outer map");

  stepper = PyObject_CallMethod(outer, "__anext__", NULL);
  TEST_ASSERT_NOT_NULL(stepper, "take the outer map's __anext__");

  res = PyObject_CallMethod(stepper, "send", "O", Py_None);
  TEST_ASSERT_NOT_NULL(res, "the step suspends on the awaitable");
  ticked = (PyUnicode_Check(res)
            && PyUnicode_CompareWithASCIIString(res, "tick") == 0);
  Py_DECREF(res);
  TEST_ASSERT(ticked, "the suspension yields the awaitable's tick");

  ctx = kos_transform_pyctx(outer);
  TEST_ASSERT_NOT_NULL(ctx, "the outer map's durable context");
  Py_INCREF(ctx);

  (void) Py_TYPE(outer)->tp_clear(outer);
  held_after_clear = Py_REFCNT(items);

  (void) Py_TYPE(ctx)->tp_clear((PyObject *) ctx);
  held_at_end = Py_REFCNT(items);

  Py_DECREF(stepper);
  Py_DECREF(ctx);
  Py_DECREF(outer);
  Py_DECREF(items);

  TEST_ASSERT_EQ(held_after_clear, 2,
                 "the inner map outlives its holder's clear");
  TEST_ASSERT_EQ(held_at_end, 1,
                 "the inner map goes with its step's frame");
  return 0;
}


/**
   Ready the suite's static provider types. The harness calls this from
   its type initialization, before any suite runs. Returns 0, or -1 with
   an error set.
*/
int ready_iterate_test_types(void) {

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(FailingDurableType)) < 0
      || PyType_Ready(KOS_EVALUATED_PYTYPE(FailingRecyclableType)) < 0
      || PyType_Ready(KOS_EVALUATED_PYTYPE(OwningDurableType)) < 0)
    return -1;
  return 0;
}


int run_iterate_tests(void) {

  BEGIN_TEST_SUITE("Iterate Adapter Tests");

  RUN_TEST(test_busy_adapter_leaves_context_untouched);
  RUN_TEST(test_busy_adapter_throw_leaves_context_untouched);
  RUN_TEST(test_durable_pyctx_failure_fails_stepper);
  RUN_TEST(test_durable_pyctx_failure_fails_pyiter);
  RUN_TEST(test_recyclable_pyctx_failure_fails_stepper);
  RUN_TEST(test_recyclable_pyctx_failure_fails_pyiter);
  RUN_TEST(test_pyiter_dealloc_releases_context_first);
  RUN_TEST(test_pyiter_clear_releases_context_first);
  RUN_TEST(test_pyiter_exhaustion_releases_context_first);
  RUN_TEST(test_staged_source_survives_containing_clear);

  return 0;
}


// The end.
