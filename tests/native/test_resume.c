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
   tests/native/test_resume.c

   C-level tests for the per-frame CPS resume plumbing: the
   resume_fn/resume_state/resume_name fields on KosDynFrame, the
   cg_prologue delivery path, the raise-walk offer, and clone/park/
   tail-reset clearing. No public setter exists yet at this phase, so
   these tests set the fields directly, the same way
   test_iter_request.c pokes native_prog/native_state directly.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === helpers === */


typedef struct {
  int calls;
  int seen_null;
  int decline;         /* return EVAL_RAISE() instead of EVAL_REPROLOGUE() */
  PyObject *returned;  /* borrowed: what to hand back via EVAL_RETURN */
} ResumeProbe;


static KosOpResult resume_probe(
    KosEvalState *es, void *state, PyObject *value) {

  (void) es;
  ResumeProbe *p = (ResumeProbe *) state;
  p->calls++;

  if (! value) {
    p->seen_null = 1;
    if (p->decline)
      return EVAL_RAISE();
    PyErr_Clear();
    return EVAL_REPROLOGUE();
  }

  return EVAL_RETURN(value);
}


/* === cg_prologue delivers a staged resume === */


static int test_cg_prologue_delivers_staged_resume(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(7);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  TEST_ASSERT(KosEvalState_StackPushRef(&es, staged) >= 0,
              "push staged value onto the frame's own stack");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosEvalState_FreezeToContext(&es);

  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

  TEST_ASSERT_EQ(probe.calls, 1, "resume fired exactly once");
  TEST_ASSERT_EQ(probe.seen_null, 0, "delivered a real value, not NULL");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RETURN, "resume's result reaches the caller");
  TEST_ASSERT_SAME(ev.val, staged, "delivered value is the staged value");

  Py_XDECREF(ev.val);
  Py_DECREF(staged);
  KosContext_Free(ctx);
  return 0;
}


/* === raise-walk offers a pending exception to a set resume ===

   The offer at dyn_walk.c only fires for a FRAME that is dyn_top but
   is NOT also the currently active frame_top (the common single-frame
   case takes the frame-crossing branch instead and never reaches the
   offer at all -- true of the pre-existing native_prog offer too, not
   a resume-specific quirk). Building that shape directly: a resume-
   carrying frame installed as dyn_top, parented under a distinct
   frame_top, mirrors how a frame that isn't the dispatch-active one
   sits on the chain (e.g. flattened generator delegation). */


static int test_raise_walk_offers_resume_declines(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *bottom_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(bottom_ns, "create bottom ns");
  KosDynFrame *bottom = kos_dynframe_new(NULL, NULL, bottom_ns);
  TEST_ASSERT_NOT_NULL(bottom, "create bottom frame");

  PyObject *top_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(top_ns, "create top ns");
  KosDynFrame *frame = kos_dynframe_new(bottom, NULL, top_ns);
  TEST_ASSERT_NOT_NULL(frame, "create resume-carrying frame");

  ResumeProbe probe = { 0, 0, 1, NULL };  /* decline */
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosEvalState_FRAME(&es) = bottom;
  KosEvalState_DYN_TOP(&es) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(&es);

  TEST_ASSERT_EQ(probe.calls, 1, "resume was offered the raise");
  TEST_ASSERT_EQ(probe.seen_null, 1, "offer delivered NULL (exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RAISE, "declined offer: walk pops and propagates");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the original exception survives a declined offer");

  /* frame was popped and freed by the walk's own pop path */
  PyErr_Clear();
  KosEvalState_FreezeToContext(&es);
  KosContext_Free(ctx);
  return 0;
}


static int test_raise_walk_offers_resume_recovers(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *bottom_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(bottom_ns, "create bottom ns");
  KosDynFrame *bottom = kos_dynframe_new(NULL, NULL, bottom_ns);
  TEST_ASSERT_NOT_NULL(bottom, "create bottom frame");

  PyObject *top_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(top_ns, "create top ns");
  KosDynFrame *frame = kos_dynframe_new(bottom, NULL, top_ns);
  TEST_ASSERT_NOT_NULL(frame, "create resume-carrying frame");

  ResumeProbe probe = { 0, 0, 0, NULL };  /* recover */
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosEvalState_FRAME(&es) = bottom;
  KosEvalState_DYN_TOP(&es) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(&es);

  TEST_ASSERT_EQ(probe.calls, 1, "resume was offered the raise");
  TEST_ASSERT_EQ(probe.seen_null, 1, "offer delivered NULL (exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_REPROLOGUE, "recovering offer resumes normally");
  TEST_ASSERT(! PyErr_Occurred(), "the resume cleared the exception");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame),
                   "resume is cleared before invocation, stays cleared");

  /* frame is still on the chain (not popped); context teardown frees
     the whole chain, frame and bottom together */
  KosEvalState_FreezeToContext(&es);
  KosContext_Free(ctx);
  return 0;
}


/* === kos_dynframe_clone refuses a resume-carrying frame === */


static int test_clone_refuses_resume_frame(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = NULL;
  KosDynFrame_RESUME_NAME(frame) = "test-resume";

  KosDynFrame *clone = kos_dynframe_clone(frame);
  TEST_ASSERT_NULL(clone, "clone refuses a resume-carrying frame");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
              "refusal raises RuntimeError");

  {
    PyObject *typ, *val, *tb;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    PyObject *msg = PyObject_Str(val);
    TEST_ASSERT_NOT_NULL(msg, "stringify the error");
    TEST_ASSERT(PyUnicode_Check(msg) &&
                strstr(PyUnicode_AsUTF8(msg), "native frame 'test-resume'"),
                "message names the resume's installer, like a native program's");
    Py_DECREF(msg);
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
  }

  kos_dynframe_free(frame);
  return 0;
}


/* === park_inert / reset_for_tail clear the resume triple === */


static int test_park_inert_clears_resume_triple(void) {
  KosDynFrame *cache = NULL;

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "create code-less frame");
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;
  KosDynFrame_RESUME_NAME(frame) = "test-resume";

  int parked = kos_dynframe_park_inert(frame, &cache);
  TEST_ASSERT_EQ(parked, 1, "frame parks into the empty cache");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame), "resume_fn stripped");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(frame), "resume_state stripped");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_NAME(frame), "resume_name stripped");

  kos_dynframe_free(cache);
  return 0;
}


static int test_reset_for_tail_clears_resume_triple(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;
  KosDynFrame_RESUME_NAME(frame) = "test-resume";

  kos_dynframe_reset_for_tail(frame);

  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame), "resume_fn cleared");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(frame), "resume_state cleared");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_NAME(frame), "resume_name cleared");

  kos_dynframe_free(frame);
  return 0;
}


/* === native_prog / resume_fn mutual exclusion ===

   Only meaningful under -DKOS_EVAL_SAFETYNET=1; the default native
   test build does not set it (see tests.mk), so this test is compiled
   out rather than silently skipped at run time. */


#if KOS_EVAL_SAFETYNET


static int test_resume_native_prog_conflict_is_fatal(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  /* Never dereferenced: the safetynet check fires on non-NULL alone. */
  KosDynFrame_NATIVE_PROG(frame) = (void *) 0x1;
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;

  KosEvalState_FreezeToContext(&es);

  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
  TEST_ASSERT_EQ(ev.kind, KOS_RES_FATAL,
                 "native_prog and resume_fn together is a fatal invariant"
                 " violation");
  PyErr_Clear();

  KosContext_Free(ctx);
  return 0;
}


#endif  /* KOS_EVAL_SAFETYNET */


/* === Suite === */


int run_resume_tests(void) {
  printf("\n=== CPS resume plumbing tests ===\n");

  RUN_TEST(test_cg_prologue_delivers_staged_resume);
  RUN_TEST(test_raise_walk_offers_resume_declines);
  RUN_TEST(test_raise_walk_offers_resume_recovers);
  RUN_TEST(test_clone_refuses_resume_frame);
  RUN_TEST(test_park_inert_clears_resume_triple);
  RUN_TEST(test_reset_for_tail_clears_resume_triple);
  #if KOS_EVAL_SAFETYNET
  RUN_TEST(test_resume_native_prog_conflict_is_fatal);
  #endif

  return 0;
}


// The end.
