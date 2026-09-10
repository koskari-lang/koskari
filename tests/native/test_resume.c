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
   cg_prologue delivery path, the raise walk reaching a resume, and clone/park/
   tail-reset clearing. These tests set the frame fields directly,
   below the KosEvalState_CallResult / NextResume entries.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === helpers === */


typedef struct {
  int calls;
  int seen_null;
  int propagate;       /* return EVAL_RAISE() instead of EVAL_REPROLOGUE() */
  PyObject *returned;  /* borrowed: what to hand back via EVAL_RETURN */
} ResumeProbe;


static KOS_PRESERVE_NONE KosOpResult resume_probe(
    KosEvalState *es, void *state, PyObject *value) {

  (void) es;
  ResumeProbe *p = (ResumeProbe *) state;
  p->calls++;

  if (! value) {
    p->seen_null = 1;
    if (p->propagate)
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
  Py_DECREF(ctx);
  return 0;
}


/* === DISPATCH stages into the pending slot, not the frame stack === */


static KosOpResult stage_via_dispatch(
    KosEvalState *es, KosResumeFn fn, void *state, PyObject *value) {

  DISPATCH(fn, es, state, value);
}


static int test_dispatch_stages_into_pending_slot(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(11);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  Py_INCREF(staged);  /* DISPATCH consumes one reference */

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult r = stage_via_dispatch(&es, resume_probe, &probe, staged);

  TEST_ASSERT_EQ(r.kind, KOS_RES_REPROLOGUE, "DISPATCH bounces to the prologue");
  TEST_ASSERT_SAME(KosEvalState_PENDING_VALUE(&es), staged,
                   "the value is staged in the pending slot");
  TEST_ASSERT(! KosEvalState_StackHead(&es)
              || KosEvalState_StackLen(&es) == 0,
              "the frame stack was not touched");
  TEST_ASSERT(KosDynFrame_RESUME_FN(frame) == (void *) resume_probe,
              "the resume is set on the current frame");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_STATE(frame), &probe,
                   "the resume state is set");
  TEST_ASSERT_EQ(probe.calls, 0, "nothing invoked the resume inline");

  /* freeze converts the live slot to storage on the frame stack */
  KosEvalState_FreezeToContext(&es);
  TEST_ASSERT_NULL(KosEvalState_PENDING_VALUE(&es), "freeze empties the slot");
  TEST_ASSERT_EQ(KosSegment_LEN(KosDynNode_STACK((KosDynNode *) frame)), 1,
                 "the staged value sits on the frame stack across the freeze");

  /* the next trip takes it back and delivers it */
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

  TEST_ASSERT_EQ(probe.calls, 1, "resume fired exactly once");
  TEST_ASSERT_EQ(probe.seen_null, 0, "delivered a real value, not NULL");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RETURN, "resume's result reaches the caller");
  TEST_ASSERT_SAME(ev.val, staged, "delivered value is the staged value");

  Py_XDECREF(ev.val);
  Py_DECREF(staged);
  Py_DECREF(ctx);
  return 0;
}


static int test_next_resume_delivered_reports_in_hand(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *lst = PyList_New(1);
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyList_SET_ITEM(lst, 0, PyLong_FromLong(42));
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult r = KosEvalState_NextResume(&es, it, NULL, resume_probe, &probe);

  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN,
                 "a delivered, settled element is reported in hand");
  TEST_ASSERT_NOT_NULL(r.val, "the element rides the result");
  TEST_ASSERT_EQ(PyLong_AsLong(r.val), 42, "element value");
  TEST_ASSERT_NULL(KosEvalState_PENDING_VALUE(&es), "nothing staged");
  TEST_ASSERT(! KosEvalState_StackHead(&es)
              || KosEvalState_StackLen(&es) == 0,
              "the frame stack was not touched");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame),
                   "nothing armed: the caller tail-calls the resume itself");
  TEST_ASSERT_EQ(probe.calls, 0, "nothing invoked the resume inline");

  Py_DECREF(r.val);
  Py_DECREF(it);
  Py_DECREF(lst);
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


/* === a bytecode child's RETURN delivers into a resume-carrying parent === */


static PyObject *resume_test_nspec(void) {
  PyObject *name = PyUnicode_FromString("test");
  PyObject *locals = PyTuple_New(0);
  PyObject *closures = PyTuple_New(0);
  PyObject *nspec = KosNamespaceSpec_New(name, locals, closures, NULL);

  Py_DECREF(name);
  Py_DECREF(locals);
  Py_DECREF(closures);
  return nspec;
}


static PyObject *resume_test_instr(int op, PyObject *arg) {
  PyObject *pair = PyTuple_New(2);

  PyTuple_SET_ITEM(pair, 0, PyLong_FromLong(op));
  Py_INCREF(arg);
  PyTuple_SET_ITEM(pair, 1, arg);
  return pair;
}


static int test_return_into_resume_parent(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  /* the parent: a codeless frame waiting on a resume */
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *parent = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(parent, "push codeless parent frame");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_FN(parent) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(parent) = (void *) &probe;

  /* the child: bytecode that returns the constant 42 */
  PyObject *forty_two = PyLong_FromLong(42);
  PyObject *one = PyLong_FromLong(1);
  PyObject *instr = PyList_New(2);
  PyList_SET_ITEM(instr, 0, resume_test_instr(KOS_OP_CONST, forty_two));
  PyList_SET_ITEM(instr, 1, resume_test_instr(KOS_OP_RETURN, one));
  Py_DECREF(one);

  PyObject *nspec = resume_test_nspec();
  TEST_ASSERT_NOT_NULL(nspec, "create nspec");
  PyObject *code = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
  Py_DECREF(nspec);
  Py_DECREF(instr);
  TEST_ASSERT_NOT_NULL(code, "create child code");

  PyObject *child_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(child_ns, "create child ns");
  KosDynFrame *child = kos_dyn_push_frame(&es, (KosCode *) code, child_ns);
  TEST_ASSERT_NOT_NULL(child, "push bytecode child frame");

  KosEvalState_FreezeToContext(&es);

  /* run: the child returns 42; the parent's resume receives it and
     hands it back as the eval's result */
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

  TEST_ASSERT_EQ(probe.calls, 1, "resume fired exactly once");
  TEST_ASSERT_EQ(probe.seen_null, 0, "with the returned value, not NULL");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RETURN, "resume's result reaches the caller");
  TEST_ASSERT_NOT_NULL(ev.val, "a value came back");
  TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "the child's return value");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(parent),
                   "the resume was consumed off the parent");

  Py_XDECREF(ev.val);
  Py_DECREF(forty_two);
  Py_DECREF(code);
  Py_DECREF(ctx);
  return 0;
}


/* === kos_prologue_reenter: the trampoline's re-entry === */


static int test_prologue_reenter_idle_proceeds(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  KosOpResult r = kos_prologue_reenter(&es);
  TEST_ASSERT_EQ(r.kind, KOS_RES_NORMAL,
                 "nothing staged, pending, or armed: dispatch may proceed");

  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


static int test_prologue_reenter_delivers_staged(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(23);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  Py_INCREF(staged);

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult st = stage_via_dispatch(&es, resume_probe, &probe, staged);
  TEST_ASSERT_EQ(st.kind, KOS_RES_REPROLOGUE, "staged");

  /* the probe returns EVAL_RETURN(value); with a root frame that is
     the terminal result the re-entry hands back */
  KosOpResult r = kos_prologue_reenter(&es);
  TEST_ASSERT_EQ(probe.calls, 1, "the staged delivery reached the resume");
  TEST_ASSERT_EQ(probe.seen_null, 0, "with its value");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN, "resume's terminal result relayed");
  TEST_ASSERT_SAME(r.val, staged, "delivered value");
  TEST_ASSERT_NULL(KosEvalState_PENDING_VALUE(&es), "slot consumed");

  Py_XDECREF(r.val);
  Py_DECREF(staged);
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


static int test_prologue_reenter_runs_pending_exception(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  /* a staged delivery under a pending exception is dropped, and the
     raise walk runs; with no handler on the chain it propagates */
  PyObject *staged = PyLong_FromLong(5);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  KosEvalState_PENDING_VALUE(&es) = staged;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_prologue_reenter(&es);
  TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "unhandled exception propagates");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the pending exception is the one propagating");
  TEST_ASSERT_NULL(KosEvalState_PENDING_VALUE(&es),
                   "the staged delivery was dropped");

  PyErr_Clear();
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


/* === the raise walk propagates a pending exception through a resume ===

   The walk calls the resume at dyn_walk.c only for a FRAME that is
   dyn_top but is NOT also the currently active frame_top (the common
   single-frame case takes the frame-crossing branch instead and never
   calls the resume at all -- true of the pre-existing native_prog path
   too, not a resume-specific quirk). Building that shape directly: a resume-
   carrying frame installed as dyn_top, parented under a distinct
   frame_top, mirrors how a frame that isn't the dispatch-active one
   sits on the chain (e.g. flattened generator delegation). */


static int test_raise_walk_resume_propagates(void) {
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

  ResumeProbe probe = { 0, 0, 1, NULL };  /* propagate */
  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosEvalState_FRAME(&es) = bottom;
  KosEvalState_DYN_TOP(&es) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(&es);

  TEST_ASSERT_EQ(probe.calls, 1, "the walk called the resume");
  TEST_ASSERT_EQ(probe.seen_null, 1, "the resume received NULL (an exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RAISE, "propagated: the walk pops and the exception continues");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the original exception is the one propagating");

  /* frame was popped and freed by the walk's own pop path */
  PyErr_Clear();
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


static int test_raise_walk_resume_handles(void) {
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

  TEST_ASSERT_EQ(probe.calls, 1, "the walk called the resume");
  TEST_ASSERT_EQ(probe.seen_null, 1, "the resume received NULL (an exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_REPROLOGUE, "handled: execution resumes normally");
  TEST_ASSERT(! PyErr_Occurred(), "the resume cleared the exception");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame),
                   "resume is cleared before invocation, stays cleared");

  /* frame is still on the chain (not popped); context teardown frees
     the whole chain, frame and bottom together */
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


/* === kos_dynframe_clone and the resume triple === */


/* A node resume (NULL state: the node it serves is dyn_top above the
   frame, cloned with the chain) copies with the frame. */
static int test_clone_copies_node_resume(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = NULL;
  KosDynFrame_RESUME_NAME(frame) = "test-node-resume";

  KosDynFrame *clone = kos_dynframe_clone(frame);
  TEST_ASSERT_NOT_NULL(clone, "clone copies a node resume frame");
  TEST_ASSERT(! PyErr_Occurred(), "no error raised");
  TEST_ASSERT(KosDynFrame_RESUME_FN(clone) == (void *) resume_probe,
              "the clone carries the resume function");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(clone),
                   "the clone's state stays NULL");
  TEST_ASSERT(KosDynFrame_RESUME_NAME(clone) == KosDynFrame_RESUME_NAME(frame),
              "the clone carries the resume name");

  kos_dynframe_free(clone);
  kos_dynframe_free(frame);
  return 0;
}


/* A step resume's state is the step's own and has no clone story, so
   capture across that frame refuses by name. */
static int test_clone_refuses_step_resume_frame(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  KosDynFrame_RESUME_FN(frame) = (void *) resume_probe;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;
  KosDynFrame_RESUME_NAME(frame) = "test-resume";

  KosDynFrame *clone = kos_dynframe_clone(frame);
  TEST_ASSERT_NULL(clone, "clone refuses a step resume frame");
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


/* === resume == NULL: opcode-shaped default === */


static int test_call_resume_null_pushes_normal_result(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *builtins = PyImport_ImportModule("builtins");
  TEST_ASSERT_NOT_NULL(builtins, "import builtins");
  PyObject *abs_fn = PyObject_GetAttrString(builtins, "abs");
  Py_DECREF(builtins);
  TEST_ASSERT_NOT_NULL(abs_fn, "get abs()");

  PyObject *neg5 = PyLong_FromLong(-5);
  TEST_ASSERT_NOT_NULL(neg5, "create -5");

  /* a plain Python callable's result is already pushed by
     KosEvalState_Call itself (push_or_await_plain); resume == NULL
     relays that NORMAL untouched -- no separate push needed here. */
  KosOpResult r = KosEvalState_CallResult(
      &es, abs_fn, &neg5, 1, NULL, 0, NULL, NULL);

  Py_DECREF(neg5);
  Py_DECREF(abs_fn);

  TEST_ASSERT_EQ(r.kind, KOS_RES_NORMAL,
                 "plain callable's NORMAL relays untouched");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame), "no resume set on the caller");

  {
    PyObject *popped = KosEvalState_StackPopRef(&es);
    TEST_ASSERT_NOT_NULL(popped, "value landed on the caller's stack");
    TEST_ASSERT_EQ(PyLong_AsLong(popped), 5, "abs(-5) == 5");
    Py_DECREF(popped);
  }

  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


static int test_next_resume_null_pushes_and_continues(void) {
  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(&es, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *lst = PyList_New(1);
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyList_SET_ITEM(lst, 0, PyLong_FromLong(42));
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  KosOpResult r = KosEvalState_NextResume(&es, it, NULL, NULL, NULL);

  TEST_ASSERT_EQ(r.kind, KOS_RES_NORMAL,
                 "delivered element with no resume: NORMAL, matching"
                 " OBJECT_NEXT's own default");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_FN(frame), "no resume set");

  {
    PyObject *popped = KosEvalState_StackPopRef(&es);
    TEST_ASSERT_NOT_NULL(popped, "element landed on the caller's stack");
    TEST_ASSERT_EQ(PyLong_AsLong(popped), 42, "element value");
    Py_DECREF(popped);
  }

  Py_DECREF(it);
  Py_DECREF(lst);
  KosEvalState_FreezeToContext(&es);
  Py_DECREF(ctx);
  return 0;
}


/* === Suite === */


int run_resume_tests(void) {
  printf("\n=== CPS resume plumbing tests ===\n");

  RUN_TEST(test_cg_prologue_delivers_staged_resume);
  RUN_TEST(test_dispatch_stages_into_pending_slot);
  RUN_TEST(test_next_resume_delivered_reports_in_hand);
  RUN_TEST(test_return_into_resume_parent);
  RUN_TEST(test_prologue_reenter_idle_proceeds);
  RUN_TEST(test_prologue_reenter_delivers_staged);
  RUN_TEST(test_prologue_reenter_runs_pending_exception);
  RUN_TEST(test_raise_walk_resume_propagates);
  RUN_TEST(test_raise_walk_resume_handles);
  RUN_TEST(test_clone_copies_node_resume);
  RUN_TEST(test_clone_refuses_step_resume_frame);
  RUN_TEST(test_park_inert_clears_resume_triple);
  RUN_TEST(test_reset_for_tail_clears_resume_triple);
  RUN_TEST(test_call_resume_null_pushes_normal_result);
  RUN_TEST(test_next_resume_null_pushes_and_continues);

  return 0;
}


// The end.
