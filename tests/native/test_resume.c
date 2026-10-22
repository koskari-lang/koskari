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
   resume_kind/resume_state fields on KosDynFrame, the prologue's
   delivery path, the raise walk reaching a resume, and clone/cache/
   tail-reset clearing. These tests set the frame fields directly,
   below the KosContext_Call / NextResume entries.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"
#include "../../koskari/lib/xfer_goal.h"


/* === helpers === */


typedef struct {
  int calls;
  int seen_null;
  int propagate;       /* return EVAL_RAISE() instead of EVAL_REPROLOGUE() */
  PyObject *returned;  /* borrowed: what to hand back via EVAL_RETURN */
} ResumeProbe;


static KOS_PRESERVE_NONE KosOpResult resume_probe(
    KosContext *ctx, void *state, PyObject *value) {

  (void) ctx;
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


/* a step kind (state is the probe's own, capture refuses) and a node
   kind (state travels with the chain, capture copies) over the same
   probe */
KOS_RESUME_KIND(resume_probe_kind, "test-resume", resume_probe, NULL, NULL);
KOS_RESUME_KIND(resume_probe_node_kind, "test-node-resume", resume_probe,
                NULL, kos_resume_clone_shared);


/* === cg_prologue delivers a staged resume === */


static int test_cg_prologue_delivers_staged_resume(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(7);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  TEST_ASSERT(KosContext_StackPushRef(ctx, staged) >= 0,
              "push staged value onto the frame's own stack");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosContext_EvalExit(ctx);

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


/* === DISPATCH stages into the pending slot and delivers === */


/* a thread function (the KosThreadFn prototype DISPATCH requires) that
   dispatches the probe kind with the given state */
static KOS_PRESERVE_NONE KosOpResult stage_via_dispatch(
    KosContext *ctx, void *state, PyObject *value) {

  DISPATCH(&resume_probe_kind, ctx, state, value);
}


/* DISPATCH sets the resume, stages the value in the pending slot and
   tails into the prologue, which takes the slot and delivers it in the
   same thread; the resume's terminal result on a root frame comes
   back as the prologue's result. */
static int test_dispatch_delivers_through_prologue(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(11);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  Py_INCREF(staged);  /* DISPATCH consumes one reference */

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult r = stage_via_dispatch(ctx, &probe, staged);

  TEST_ASSERT_EQ(probe.calls, 1, "the prologue delivered to the resume");
  TEST_ASSERT_EQ(probe.seen_null, 0, "delivered a real value, not NULL");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN, "resume's terminal result relayed");
  TEST_ASSERT_SAME(r.val, staged, "delivered value is the staged value");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx), "slot consumed");
  TEST_ASSERT(! KosContext_StackHead(ctx)
              || KosContext_StackLen(ctx) == 0,
              "the frame stack was not touched");

  Py_XDECREF(r.val);
  Py_DECREF(staged);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


static int test_next_resume_delivered_reports_in_hand(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *lst = PyList_New(1);
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyList_SET_ITEM(lst, 0, PyLong_FromLong(42));
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult r = KosContext_NextResume(ctx, it, NULL, &resume_probe_kind, &probe);

  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN,
                 "a delivered, settled element is reported in hand");
  TEST_ASSERT_NOT_NULL(r.val, "the element rides the result");
  TEST_ASSERT_EQ(PyLong_AsLong(r.val), 42, "element value");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx), "nothing staged");
  TEST_ASSERT(! KosContext_StackHead(ctx)
              || KosContext_StackLen(ctx) == 0,
              "the frame stack was not touched");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame),
                   "no resume set: the caller tail-calls the resume itself");
  TEST_ASSERT_EQ(probe.calls, 0, "nothing invoked the resume inline");

  Py_DECREF(r.val);
  Py_DECREF(it);
  Py_DECREF(lst);
  KosContext_EvalExit(ctx);
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
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  /* the parent: a codeless frame waiting on a resume */
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *parent = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(parent, "push codeless parent frame");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_KIND(parent) = &resume_probe_kind;
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
  KosDynFrame *child = kos_dyn_push_frame(ctx, (KosCode *) code, child_ns);
  TEST_ASSERT_NOT_NULL(child, "push bytecode child frame");

  KosContext_EvalExit(ctx);

  /* run: the child returns 42; the parent's resume receives it and
     hands it back as the eval's result */
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

  TEST_ASSERT_EQ(probe.calls, 1, "resume fired exactly once");
  TEST_ASSERT_EQ(probe.seen_null, 0, "with the returned value, not NULL");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RETURN, "resume's result reaches the caller");
  TEST_ASSERT_NOT_NULL(ev.val, "a value came back");
  TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "the child's return value");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(parent),
                   "the resume was consumed off the parent");

  Py_XDECREF(ev.val);
  Py_DECREF(forty_two);
  Py_DECREF(code);
  Py_DECREF(ctx);
  return 0;
}


/* === kos_eval_prologue: the threaded re-entry === */


/* Nothing staged, pending, or set: the prologue threads into the
   frame's instruction stream, here a body returning the constant 42,
   and the root return is the prologue's result. */
static int test_prologue_idle_threads_into_code(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

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
  TEST_ASSERT_NOT_NULL(code, "create code");

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, (KosCode *) code, ns);
  TEST_ASSERT_NOT_NULL(frame, "push bytecode frame");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN, "the body ran to its return");
  TEST_ASSERT_NOT_NULL(r.val, "a value came back");
  TEST_ASSERT_EQ(PyLong_AsLong(r.val), 42, "the body's return value");

  Py_XDECREF(r.val);
  Py_DECREF(forty_two);
  Py_DECREF(code);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* A resume set on the frame with a value staged in the slot: the
   prologue takes the slot and delivers it. */
static int test_prologue_delivers_staged(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(23);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  Py_INCREF(staged);

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;
  KosContext_PENDING_VALUE(ctx) = staged;

  /* the probe returns EVAL_RETURN(value); with a root frame that is
     the terminal result the re-entry hands back */
  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(probe.calls, 1, "the staged delivery reached the resume");
  TEST_ASSERT_EQ(probe.seen_null, 0, "with its value");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN, "resume's terminal result relayed");
  TEST_ASSERT_SAME(r.val, staged, "delivered value");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx), "slot consumed");

  Py_XDECREF(r.val);
  Py_DECREF(staged);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


static int test_prologue_runs_pending_exception(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  /* a staged delivery under a pending exception is dropped, and the
     raise walk runs; with no handler on the chain it propagates */
  PyObject *staged = PyLong_FromLong(5);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  KosContext_PENDING_VALUE(ctx) = staged;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "unhandled exception propagates");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the pending exception is the one propagating");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx),
                   "the staged delivery was dropped");

  PyErr_Clear();
  KosContext_EvalExit(ctx);
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
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *bottom_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(bottom_ns, "create bottom ns");
  KosDynFrame *bottom = kos_dynframe_new(NULL, NULL, bottom_ns);
  TEST_ASSERT_NOT_NULL(bottom, "create bottom frame");

  PyObject *top_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(top_ns, "create top ns");
  KosDynFrame *frame = kos_dynframe_new(bottom, NULL, top_ns);
  TEST_ASSERT_NOT_NULL(frame, "create resume-carrying frame");

  ResumeProbe probe = { 0, 0, 1, NULL };  /* propagate */
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosContext_FRAME(ctx) = bottom;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(ctx);

  TEST_ASSERT_EQ(probe.calls, 1, "the walk called the resume");
  TEST_ASSERT_EQ(probe.seen_null, 1, "the resume received NULL (an exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RAISE, "propagated: the walk pops and the exception continues");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the original exception is the one propagating");

  /* frame was popped and freed by the walk's own pop path */
  PyErr_Clear();
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


static int test_raise_walk_resume_handles(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *bottom_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(bottom_ns, "create bottom ns");
  KosDynFrame *bottom = kos_dynframe_new(NULL, NULL, bottom_ns);
  TEST_ASSERT_NOT_NULL(bottom, "create bottom frame");

  PyObject *top_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(top_ns, "create top ns");
  KosDynFrame *frame = kos_dynframe_new(bottom, NULL, top_ns);
  TEST_ASSERT_NOT_NULL(frame, "create resume-carrying frame");

  ResumeProbe probe = { 0, 0, 0, NULL };  /* recover */
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosContext_FRAME(ctx) = bottom;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(ctx);

  TEST_ASSERT_EQ(probe.calls, 1, "the walk called the resume");
  TEST_ASSERT_EQ(probe.seen_null, 1, "the resume received NULL (an exception, not a value)");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_REPROLOGUE, "handled: execution resumes normally");
  TEST_ASSERT(! PyErr_Occurred(), "the resume cleared the exception");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame),
                   "resume is cleared before invocation, stays cleared");

  /* frame is still on the chain (not popped); context teardown frees
     the whole chain, frame and bottom together */
  KosContext_EvalExit(ctx);
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

  KosDynFrame_RESUME_KIND(frame) = &resume_probe_node_kind;
  KosDynFrame_RESUME_STATE(frame) = NULL;

  KosDynFrame *clone = kos_dynframe_clone(frame);
  TEST_ASSERT_NOT_NULL(clone, "clone copies a node resume frame");
  TEST_ASSERT(! PyErr_Occurred(), "no error raised");
  TEST_ASSERT(KosDynFrame_RESUME_KIND(clone) == &resume_probe_node_kind,
              "the clone carries the resume function");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(clone),
                   "the clone's state stays NULL");
  TEST_ASSERT(KosDynFrame_RESUME_NAME(clone) == KosDynFrame_RESUME_NAME(frame),
              "the clone carries the resume name");

  kos_dynframe_free(clone);
  kos_dynframe_free(frame);
  return 0;
}


/* A kind whose clone and clear record what they are handed, for the
   transfer restore tests: the clone answers a distinct pointer so the
   record's state can be told from the template's. */
static void *recorded_clone_in = NULL;
static void *recorded_clear_in = NULL;
static int recorded_clone_calls = 0;
static int recorded_clear_calls = 0;
static int recorded_clone_out;

static void *recording_clone(void *state) {
  recorded_clone_in = state;
  recorded_clone_calls++;
  return &recorded_clone_out;
}

static void recording_clear(void *state) {
  recorded_clear_in = state;
  recorded_clear_calls++;
}

KOS_RESUME_KIND(resume_recording_kind, "recording", resume_probe,
                recording_clear, recording_clone);

static void recording_reset(void) {
  recorded_clone_in = NULL;
  recorded_clear_in = NULL;
  recorded_clone_calls = 0;
  recorded_clear_calls = 0;
}


/* A transfer restore record takes the kind's clone of the template's
   state, and a record freed unapplied runs the kind's clear on that
   clone; the template keeps its own state throughout. */
static int test_capture_restore_clones_and_clears_unapplied(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *live = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(live, "create live frame");
  KosDynFrame *tmpl = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(tmpl, "create template frame");
  KosDynNode_NODE_ID(tmpl) = kos_dynnode_id(live);

  int marker = 0;
  recording_reset();
  KosDynFrame_RESUME_KIND(tmpl) = &resume_recording_kind;
  KosDynFrame_RESUME_STATE(tmpl) = &marker;

  KosXferGoal *goal = kos_xfer_goal_new(NULL, (KosDynNode *) live, NULL,
                                        0, NULL, 0);
  TEST_ASSERT_NOT_NULL(goal, "create goal");

  TEST_ASSERT_EQ(kos_xfer_goal_capture_restore(
                     goal, (KosDynNode *) live, (KosDynNode *) tmpl),
                 0, "the record is captured");
  TEST_ASSERT_EQ(recorded_clone_calls, 1, "the kind's clone ran once");
  TEST_ASSERT_SAME(recorded_clone_in, &marker,
                   "the clone was handed the template's state");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_STATE(tmpl), &marker,
                   "the template keeps its own state");
  TEST_ASSERT_EQ(recorded_clear_calls, 0, "nothing cleared yet");

  /* freed unapplied: the clone is the goal's to clear */
  kos_xfer_goal_free(goal);
  TEST_ASSERT_EQ(recorded_clear_calls, 1, "the kind's clear ran once");
  TEST_ASSERT_SAME(recorded_clear_in, &recorded_clone_out,
                   "the clear was handed the record's clone");

  KosDynFrame_RESUME_KIND(tmpl) = NULL;
  KosDynFrame_RESUME_STATE(tmpl) = NULL;
  kos_dynframe_free(tmpl);
  kos_dynframe_free(live);
  Py_DECREF(ns);
  return 0;
}


/* Applying a record clears the resume the live frame held and hands
   the clone to the frame, so the goal no longer owns it. The landing
   frame then carries the record's resume, so the delivery stages the
   payload for that resume and touches no frame stack. */
static int test_capture_restore_apply_hands_over_the_clone(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *live = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(live, "push live frame");
  KosDynFrame *tmpl = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(tmpl, "create template frame");
  KosDynNode_NODE_ID(tmpl) = kos_dynnode_id(live);

  int marker = 0;
  int prior = 0;
  recording_reset();
  KosDynFrame_RESUME_KIND(tmpl) = &resume_recording_kind;
  KosDynFrame_RESUME_STATE(tmpl) = &marker;

  PyObject *exc = PyObject_CallNoArgs(PyExc_ValueError);
  TEST_ASSERT_NOT_NULL(exc, "create payload");
  KosXferGoal *goal = kos_xfer_goal_new(NULL, (KosDynNode *) live, exc,
                                        (uint32_t) -1, NULL, 0);
  TEST_ASSERT_NOT_NULL(goal, "create goal");
  TEST_ASSERT_EQ(kos_xfer_goal_capture_restore(
                     goal, (KosDynNode *) live, (KosDynNode *) tmpl),
                 0, "the record is captured");

  /* the live frame carries a resume of its own the landing displaces */
  KosDynFrame_RESUME_KIND(live) = &resume_recording_kind;
  KosDynFrame_RESUME_STATE(live) = &prior;

  kos_xfer_goal_push(ctx, goal);
  {
    int boundary = 0;
    KosOpResult r = kos_xfer_goal_drive(ctx, &boundary);
    TEST_ASSERT_EQ(r.kind, (int) KOS_RES_NORMAL, "the goal drives through");
  }
  TEST_ASSERT_NULL(KosContext_GOAL(ctx), "the goal retired");
  TEST_ASSERT_EQ(recorded_clear_calls, 1,
                 "the live frame's prior resume was cleared once");
  TEST_ASSERT_SAME(recorded_clear_in, &prior,
                   "the clear was handed the prior state");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_KIND(live), &resume_recording_kind,
                   "the live frame carries the record's kind");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_STATE(live), &recorded_clone_out,
                   "the live frame carries the record's clone");
  TEST_ASSERT_SAME(KosContext_PENDING_VALUE(ctx), exc,
                   "the payload is staged for the record's resume");
  Py_CLEAR(KosContext_PENDING_VALUE(ctx));

  KosDynFrame_RESUME_KIND(live) = NULL;
  KosDynFrame_RESUME_STATE(live) = NULL;
  KosDynFrame_RESUME_KIND(tmpl) = NULL;
  KosDynFrame_RESUME_STATE(tmpl) = NULL;
  kos_dynframe_free(tmpl);
  Py_DECREF(exc);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  Py_DECREF(ns);
  return 0;
}


/* A value delivered by a transfer to a landing frame carrying a resume
   is staged for that resume, not pushed on the frame's stack: the
   resume was waiting on a delivery when the frame was captured, and
   the continuation's argument is it. */
static int test_transfer_delivers_to_a_landing_frame_resume(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *live = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(live, "push live frame");

  int marker = 0;
  KosDynFrame_RESUME_KIND(live) = &resume_recording_kind;
  KosDynFrame_RESUME_STATE(live) = &marker;

  PyObject *payload = PyLong_FromLong(99);
  TEST_ASSERT_NOT_NULL(payload, "create payload");
  KosXferGoal *goal = kos_xfer_goal_new(NULL, (KosDynNode *) live, payload,
                                        (uint32_t) -1, NULL, 0);
  TEST_ASSERT_NOT_NULL(goal, "create goal");

  kos_xfer_goal_push(ctx, goal);
  {
    int boundary = 0;
    KosOpResult r = kos_xfer_goal_drive(ctx, &boundary);
    TEST_ASSERT_EQ(r.kind, (int) KOS_RES_NORMAL, "the goal drives through");
  }
  TEST_ASSERT_NULL(KosContext_GOAL(ctx), "the goal retired");
  TEST_ASSERT_SAME(KosContext_PENDING_VALUE(ctx), payload,
                   "the payload is staged for the resume");
  TEST_ASSERT(! KosContext_StackHead(ctx)
              || KosContext_StackLen(ctx) == 0,
              "the frame stack was not touched");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_KIND(live), &resume_recording_kind,
                   "the resume is still set for the delivery");

  Py_CLEAR(KosContext_PENDING_VALUE(ctx));
  KosDynFrame_RESUME_KIND(live) = NULL;
  KosDynFrame_RESUME_STATE(live) = NULL;
  Py_DECREF(payload);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  Py_DECREF(ns);
  return 0;
}


/* A transfer restore record clones a template frame's resume state
   through its kind; a template whose kind cannot clone is refused
   with an error in every build, never called through. The template
   is hand-built here, since kos_dynframe_clone refuses to make one. */
static int test_capture_restore_refuses_uncloneable_kind(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *live = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(live, "create live frame");
  KosDynFrame *tmpl = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(tmpl, "create template frame");
  KosDynNode_NODE_ID(tmpl) = kos_dynnode_id(live);

  KosDynFrame_RESUME_KIND(tmpl) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(tmpl) = (void *) tmpl;

  KosXferGoal *goal = kos_xfer_goal_new(NULL, (KosDynNode *) live, NULL,
                                        0, NULL, 0);
  TEST_ASSERT_NOT_NULL(goal, "create goal");

  int r = kos_xfer_goal_capture_restore(
      goal, (KosDynNode *) live, (KosDynNode *) tmpl);
  TEST_ASSERT_EQ(r, -1, "a template resume without a clone is refused");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_SystemError),
              "the refusal is a SystemError");
  TEST_ASSERT_NULL(goal->restore, "no records were kept");
  PyErr_Clear();

  kos_xfer_goal_free(goal);
  kos_dynframe_free(tmpl);
  kos_dynframe_free(live);
  Py_DECREF(ns);
  return 0;
}


/* A step resume's state is the step's own and has no clone story, so
   capture across that frame refuses by name. */
static int test_clone_refuses_step_resume_frame(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;

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


/* === cache_inert / reset_for_tail clear the resume triple === */


static int test_cache_inert_clears_resume_triple(void) {
  KosDynFrame *cache = NULL;

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, NULL);
  TEST_ASSERT_NOT_NULL(frame, "create code-less frame");
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;

  int cached = kos_dynframe_cache_inert(frame, &cache);
  TEST_ASSERT_EQ(cached, 1, "frame is cached into the empty cache");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame), "resume_fn stripped");
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
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) frame;

  kos_dynframe_reset_for_tail(frame);

  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame), "resume_fn cleared");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(frame), "resume_state cleared");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_NAME(frame), "resume_name cleared");

  kos_dynframe_free(frame);
  return 0;
}


/* === resume == NULL: opcode-shaped default === */


static int test_call_resume_null_returns_value_in_hand(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *builtins = PyImport_ImportModule("builtins");
  TEST_ASSERT_NOT_NULL(builtins, "import builtins");
  PyObject *abs_fn = PyObject_GetAttrString(builtins, "abs");
  Py_DECREF(builtins);
  TEST_ASSERT_NOT_NULL(abs_fn, "get abs()");

  PyObject *neg5 = PyLong_FromLong(-5);
  TEST_ASSERT_NOT_NULL(neg5, "create -5");

  /* a plain Python callable's result comes back in hand; resume == NULL
     relays that RETURN untouched, and nothing lands on the stack */
  KosOpResult r = KosContext_Call(
      ctx, abs_fn, &neg5, 1, NULL, 0, NULL, NULL);

  Py_DECREF(neg5);
  Py_DECREF(abs_fn);

  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN,
                 "plain callable's RETURN relays untouched");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame), "no resume set on the caller");
  TEST_ASSERT_NOT_NULL(r.val, "value in hand");
  TEST_ASSERT_EQ(PyLong_AsLong(r.val), 5, "abs(-5) == 5");
  Py_DECREF(r.val);
  TEST_ASSERT(! KosDynFrame_STACK(frame)
              || KosSegment_LEN(KosDynFrame_STACK(frame)) == 0,
              "nothing landed on the caller's stack");

  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


static int test_next_resume_null_pushes_and_continues(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *lst = PyList_New(1);
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyList_SET_ITEM(lst, 0, PyLong_FromLong(42));
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  KosOpResult r = KosContext_NextResume(ctx, it, NULL, NULL, NULL);

  TEST_ASSERT_EQ(r.kind, KOS_RES_NORMAL,
                 "delivered element with no resume: NORMAL, matching"
                 " OBJECT_NEXT's own default");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame), "no resume set");

  {
    PyObject *popped = KosContext_StackPopRef(ctx);
    TEST_ASSERT_NOT_NULL(popped, "element landed on the caller's stack");
    TEST_ASSERT_EQ(PyLong_AsLong(popped), 42, "element value");
    Py_DECREF(popped);
  }

  Py_DECREF(it);
  Py_DECREF(lst);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* === the drive's handled-exception swap === */


typedef struct {
  int calls;
  PyObject *seen;        /* owned: handled exception seen inside, or NULL */
  PyObject *set_inside;  /* borrowed: made the handled exception inside */
} ExcInfoProbe;


/* The thread's handled exception as a new reference, or NULL when it
   handles none (NULLs on some versions, None on others) */
static PyObject *thread_handled(void) {
  PyObject *typ;
  PyObject *val;
  PyObject *tb;

  PyErr_GetExcInfo(&typ, &val, &tb);
  Py_XDECREF(typ);
  Py_XDECREF(tb);
  if (val == Py_None)
    Py_CLEAR(val);
  return val;
}


/* Records the handled exception visible inside the drive, then makes
   set_inside the handled one, as a context left inside a handler
   would, and returns the delivered value. */
static KOS_PRESERVE_NONE KosOpResult exc_info_probe(
    KosContext *ctx, void *state, PyObject *value) {

  ExcInfoProbe *p = (ExcInfoProbe *) state;

  (void) ctx;
  p->calls++;
  Py_XSETREF(p->seen, thread_handled());
  if (p->set_inside)
    PyErr_SetHandledException(p->set_inside);

  if (! value)
    return EVAL_RAISE();
  return EVAL_RETURN(value);
}


KOS_RESUME_KIND(exc_info_probe_kind, "test-exc-info", exc_info_probe,
                NULL, NULL);


/* One drive of a fresh context whose root frame delivers a staged
   value to the probe. 0 when the drive returned that value and, with
   set_inside, the context kept set_inside as its handled exception;
   -1 otherwise. */
static int drive_exc_info_probe(ExcInfoProbe *probe) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *staged = PyLong_FromLong(5);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  TEST_ASSERT(KosContext_StackPushRef(ctx, staged) >= 0,
              "push staged value onto the frame's own stack");

  KosDynFrame_RESUME_KIND(frame) = &exc_info_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) probe;

  KosContext_EvalExit(ctx);

  probe->calls = 0;
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

  TEST_ASSERT_EQ(probe->calls, 1, "probe ran once");
  TEST_ASSERT_EQ(ev.kind, KOS_RES_RETURN, "drive returned");
  TEST_ASSERT_SAME(ev.val, staged, "drive returned the staged value");
  if (probe->set_inside)
    TEST_ASSERT_SAME(KosContext_EXC_INFO(ctx)->value, probe->set_inside,
                     "context kept the exception it was handling");

  Py_XDECREF(ev.val);
  Py_DECREF(staged);
  Py_DECREF(ctx);
  return 0;
}


/* A drive neither shows the thread's handled exception to the context
   nor leaves the context's behind: after each drive the thread handles
   what it handled before, whether the thread held one at entry and
   whether the context was handling one when the drive ended. */
static int test_drive_restores_thread_exc_info(void) {
  PyObject *saved_typ;
  PyObject *saved_val;
  PyObject *saved_tb;
  PyObject *host_exc;
  PyObject *inner_exc;
  PyObject *handled;
  ExcInfoProbe probe = { 0, NULL, NULL };
  int i;

  PyErr_GetExcInfo(&saved_typ, &saved_val, &saved_tb);
  PyErr_SetExcInfo(NULL, NULL, NULL);

  host_exc = PyObject_CallFunction(PyExc_KeyError, "s", "host");
  TEST_ASSERT_NOT_NULL(host_exc, "create host exception");
  inner_exc = PyObject_CallFunction(PyExc_ValueError, "s", "inner");
  TEST_ASSERT_NOT_NULL(inner_exc, "create inner exception");

  /* the thread handles nothing, twice */
  for (i = 0; i < 2; i++) {
    TEST_ASSERT_EQ(drive_exc_info_probe(&probe), 0, "drive");
    TEST_ASSERT_NULL(probe.seen, "nothing handled inside");
    handled = thread_handled();
    TEST_ASSERT_NULL(handled, "thread handles nothing after the drive");
  }

  /* the thread handles nothing; the context ends inside a handler */
  probe.set_inside = inner_exc;
  TEST_ASSERT_EQ(drive_exc_info_probe(&probe), 0, "drive");
  TEST_ASSERT_NULL(probe.seen, "nothing handled inside");
  handled = thread_handled();
  TEST_ASSERT_NULL(handled, "context's exception not left on the thread");

  /* the thread handles the host's exception */
  PyErr_SetHandledException(host_exc);
  probe.set_inside = NULL;
  TEST_ASSERT_EQ(drive_exc_info_probe(&probe), 0, "drive");
  TEST_ASSERT_NULL(probe.seen, "host's exception not visible inside");
  handled = thread_handled();
  TEST_ASSERT_SAME(handled, host_exc, "host's exception back after drive");
  Py_DECREF(handled);

  /* the thread handles the host's; the context ends inside a handler */
  probe.set_inside = inner_exc;
  TEST_ASSERT_EQ(drive_exc_info_probe(&probe), 0, "drive");
  TEST_ASSERT_NULL(probe.seen, "host's exception not visible inside");
  handled = thread_handled();
  TEST_ASSERT_SAME(handled, host_exc, "host's exception back after drive");
  Py_DECREF(handled);

  PyErr_SetExcInfo(saved_typ, saved_val, saved_tb);
  Py_DECREF(inner_exc);
  Py_DECREF(host_exc);
  return 0;
}


/* === Suite === */


int run_resume_tests(void) {
  printf("\n=== CPS resume plumbing tests ===\n");

  RUN_TEST(test_cg_prologue_delivers_staged_resume);
  RUN_TEST(test_dispatch_delivers_through_prologue);
  RUN_TEST(test_next_resume_delivered_reports_in_hand);
  RUN_TEST(test_return_into_resume_parent);
  RUN_TEST(test_prologue_idle_threads_into_code);
  RUN_TEST(test_prologue_delivers_staged);
  RUN_TEST(test_prologue_runs_pending_exception);
  RUN_TEST(test_raise_walk_resume_propagates);
  RUN_TEST(test_raise_walk_resume_handles);
  RUN_TEST(test_clone_copies_node_resume);
  RUN_TEST(test_capture_restore_clones_and_clears_unapplied);
  RUN_TEST(test_capture_restore_apply_hands_over_the_clone);
  RUN_TEST(test_transfer_delivers_to_a_landing_frame_resume);
  RUN_TEST(test_capture_restore_refuses_uncloneable_kind);
  RUN_TEST(test_clone_refuses_step_resume_frame);
  RUN_TEST(test_cache_inert_clears_resume_triple);
  RUN_TEST(test_reset_for_tail_clears_resume_triple);
  RUN_TEST(test_call_resume_null_returns_value_in_hand);
  RUN_TEST(test_next_resume_null_pushes_and_continues);
  RUN_TEST(test_drive_restores_thread_exc_info);

  return 0;
}


// The end.
