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


/* a step kind (state is the probe's own, capture refuses) and a kind
   whose clone shares the state (kos_resume_clone_shared, as the
   procedure and demand kinds use) over the same probe */
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

  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL, NULL);

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
  KosOpResult r = KosContext_NextResume(ctx, it, &resume_probe_kind, &probe);

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


static int test_next_resume_exhausted_sets_the_resume(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  PyObject *lst = PyList_New(0);
  TEST_ASSERT_NOT_NULL(lst, "create list");
  PyObject *it = kos_iter_acquire(lst);
  TEST_ASSERT_NOT_NULL(it, "acquire iterator");

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosOpResult r = KosContext_NextResume(ctx, it, &resume_probe_kind, &probe);

  int stopped = (PyErr_Occurred()
                 && PyErr_ExceptionMatches(PyExc_StopIteration));
  const KosResumeKind *kind = KosDynFrame_RESUME_KIND(frame);
  void *state = KosDynFrame_RESUME_STATE(frame);

  PyErr_Clear();
  KosDynFrame_RESUME_KIND(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;
  Py_XDECREF(r.val);
  Py_DECREF(it);
  Py_DECREF(lst);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);

  TEST_ASSERT_EQ(r.kind, KOS_RES_RESUME,
                 "exhaustion is answered as a delivery to the resume");
  TEST_ASSERT_NULL(r.val, "nothing rides the result");
  TEST_ASSERT(stopped, "StopIteration is pending for the delivery");
  TEST_ASSERT(kind == &resume_probe_kind,
              "the resume kind is set on the calling frame");
  TEST_ASSERT(state == (void *) &probe,
              "the resume state is set on the calling frame");
  TEST_ASSERT_EQ(probe.calls, 0, "nothing invoked the resume inline");
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
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL, NULL);

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


/* A resume set on the frame, a value staged in the slot, and an
   exception pending: the failure supersedes the delivery. The resume
   sees the NULL with the error, the staged value is dropped, and the
   decline propagates the error. */
static int test_prologue_drops_staged_under_pending_exception(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  /* a fresh object, so its reference count is observable */
  PyObject *staged = PyList_New(0);
  TEST_ASSERT_NOT_NULL(staged, "create staged value");
  Py_INCREF(staged);

  ResumeProbe probe = { 0, 0, 1, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;
  KosContext_PENDING_VALUE(ctx) = staged;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(probe.calls, 1, "the resume was offered the failure");
  TEST_ASSERT_EQ(probe.seen_null, 1, "as a NULL, not the staged value");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "the decline propagates");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the pending exception is the one propagating");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx),
                   "the staged delivery was dropped");
  TEST_ASSERT_EQ(Py_REFCNT(staged), 1, "and released");

  PyErr_Clear();
  Py_DECREF(staged);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* The same with the delivery stored on the code-less frame's stack,
   as a drive exit leaves it: the stacked value is dropped too. */
static int test_prologue_drops_stacked_under_pending_exception(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  /* a fresh object, so its reference count is observable */
  PyObject *stacked = PyList_New(0);
  TEST_ASSERT_NOT_NULL(stacked, "create stacked value");
  TEST_ASSERT_EQ(KosContext_StackPushRef(ctx, stacked), 0,
                 "store the delivery on the frame's stack");

  ResumeProbe probe = { 0, 0, 1, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(probe.calls, 1, "the resume was offered the failure");
  TEST_ASSERT_EQ(probe.seen_null, 1, "as a NULL, not the stacked value");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "the decline propagates");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the pending exception is the one propagating");
  TEST_ASSERT_EQ(KosContext_StackLen(ctx), 0, "the stacked value was dropped");
  TEST_ASSERT_EQ(Py_REFCNT(stacked), 1, "and released");

  PyErr_Clear();
  Py_DECREF(stacked);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* A pending exception with a resume set and nothing staged: the
   prologue offers the exception to the resume as a NULL delivery,
   clearing the resume first. A declining resume propagates the
   exception. */
static int test_prologue_offers_pending_exception_to_resume(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "push codeless frame");

  ResumeProbe probe = { 0, 0, 1, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  int raised = PyErr_ExceptionMatches(PyExc_ValueError);
  PyErr_Clear();

  TEST_ASSERT_EQ(probe.calls, 1, "the prologue offered the error");
  TEST_ASSERT_EQ(probe.seen_null, 1, "as a NULL delivery");
  TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "the decline propagates");
  TEST_ASSERT(raised, "the original exception propagates");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame),
                   "the delivery cleared the resume");

  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* The same offer on a bytecode frame whose resume handles the
   exception: the resume clears it and asks for another pass, and the
   next pass, as the trampoline runs it, threads into the frame's code
   to its return. */
static int test_prologue_resume_handles_pending_exception(void) {
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

  ResumeProbe probe = { 0, 0, 0, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;
  PyErr_SetString(PyExc_ValueError, "pending");

  KosOpResult r = kos_eval_prologue(ctx, NULL, NULL);
  int still_pending = PyErr_Occurred() != NULL;
  PyErr_Clear();

  TEST_ASSERT_EQ(probe.calls, 1, "the prologue offered the error");
  TEST_ASSERT_EQ(probe.seen_null, 1, "as a NULL delivery");
  TEST_ASSERT_EQ(r.kind, KOS_RES_REPROLOGUE,
                 "the handling resume asks for another pass");
  TEST_ASSERT(! still_pending, "the resume handled the exception");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame),
                   "the delivery cleared the resume");

  r = kos_eval_prologue(ctx, NULL, NULL);
  TEST_ASSERT_EQ(r.kind, KOS_RES_RETURN,
                 "the frame threaded on to its return");
  TEST_ASSERT_NOT_NULL(r.val, "a value came back");
  TEST_ASSERT_EQ(PyLong_AsLong(r.val), 42, "the body's return value");
  TEST_ASSERT_EQ(probe.calls, 1, "the resume ran once");

  Py_XDECREF(r.val);
  Py_DECREF(forty_two);
  Py_DECREF(code);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* === the raise walk defers to the live frame's resume ===

   The walk never calls a resume: a pending exception with a resume on
   the live frame is handed back as RESUME, resume and exception left
   in place, for the caller to deliver as a NULL by tail call or
   through the prologue (test_prologue_offers_pending_exception_to_resume,
   test_prologue_resume_handles_pending_exception). That holds at the
   walk's entry and again when a frame pop reveals a frame with a
   resume, before any node above that frame is unwound. */


static int test_raise_walk_defers_to_the_live_frame_resume(void) {
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

  ResumeProbe probe = { 0, 0, 1, NULL };
  KosDynFrame_RESUME_KIND(frame) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) &probe;

  KosContext_FRAME(ctx) = frame;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(ctx);

  TEST_ASSERT_EQ(ev.kind, KOS_RES_RESUME,
                 "the walk hands the delivery to the resume");
  TEST_ASSERT_EQ(probe.calls, 0, "the walk itself called no resume");
  TEST_ASSERT(KosDynFrame_RESUME_KIND(frame) == &resume_probe_kind,
              "the resume stays on the frame for the prologue");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the exception stays pending for the delivery");
  TEST_ASSERT(KosContext_DYN_TOP(ctx) == (KosDynNode *) frame,
              "nothing was unwound ahead of the resume");

  PyErr_Clear();
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


static int test_raise_walk_defers_after_a_pop_reveals_a_resume(void) {
  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  KosContext_EvalEnter(ctx, NULL);

  /* two pushed frames, so the pop's resync reveals the lower one */
  PyObject *bottom_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(bottom_ns, "create bottom ns");
  KosDynFrame *bottom = kos_dyn_push_frame(ctx, NULL, bottom_ns);
  TEST_ASSERT_NOT_NULL(bottom, "push bottom frame");

  PyObject *top_ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(top_ns, "create top ns");
  KosDynFrame *frame = kos_dyn_push_frame(ctx, NULL, top_ns);
  TEST_ASSERT_NOT_NULL(frame, "push plain top frame");

  /* the resume waits on the frame below the live one */
  ResumeProbe probe = { 0, 0, 1, NULL };
  KosDynFrame_RESUME_KIND(bottom) = &resume_probe_kind;
  KosDynFrame_RESUME_STATE(bottom) = (void *) &probe;

  PyErr_SetString(PyExc_ValueError, "boom");

  KosOpResult ev = kos_dyn_unwind_raise(ctx);

  TEST_ASSERT_EQ(ev.kind, KOS_RES_RESUME,
                 "the pop revealed a resume: the walk hands over");
  TEST_ASSERT(KosContext_FRAME(ctx) == bottom,
              "the top frame was popped first");
  TEST_ASSERT(KosContext_DYN_TOP(ctx) == (KosDynNode *) bottom,
              "the revealed frame is the live one");
  TEST_ASSERT_EQ(probe.calls, 0, "the walk itself called no resume");
  TEST_ASSERT(KosDynFrame_RESUME_KIND(bottom) == &resume_probe_kind,
              "the revealed resume stays set for the prologue");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError),
              "the exception stays pending for the delivery");

  PyErr_Clear();
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);
  return 0;
}


/* === kos_dynframe_clone and the resume triple === */


/* A resume whose kind shares its state on clone copies with the
   frame; a NULL state stays NULL. */
static int test_clone_copies_node_resume(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  KosDynFrame_RESUME_KIND(frame) = &resume_probe_node_kind;
  KosDynFrame_RESUME_STATE(frame) = NULL;

  KosDynFrame *clone = kos_dynframe_clone(frame);
  TEST_ASSERT_NOT_NULL(clone, "clone copies a shared-state resume frame");
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
                                        NULL, NULL, 0);
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


/* A kind whose clear records what the observed frame names while the
   clear runs: a clear can run code that reaches the frame, so the
   frame must already have let go of the resume it is releasing. */
static KosDynFrame *observed_frame = NULL;
static int observed_clear_calls = 0;
static void *observed_clear_in = NULL;
static const KosResumeKind *observed_kind_seen = NULL;
static void *observed_state_seen = NULL;

static void observing_clear(void *state) {

  observed_clear_calls++;
  observed_clear_in = state;
  observed_kind_seen = KosDynFrame_RESUME_KIND(observed_frame);
  observed_state_seen = KosDynFrame_RESUME_STATE(observed_frame);
}

KOS_RESUME_KIND(resume_observing_kind, "observing", resume_probe,
                observing_clear, recording_clone);

static void observing_reset(KosDynFrame *frame) {

  observed_frame = frame;
  observed_clear_calls = 0;
  observed_clear_in = NULL;
  observed_kind_seen = &resume_observing_kind;
  observed_state_seen = &observed_clear_calls;
}


/* kos_dynframe_clear_resume hands the clear the frame's state with
   both resume fields already NULL. */
static int test_clear_resume_unnames_state_before_clear(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  int marker = 0;
  observing_reset(frame);
  KosDynFrame_RESUME_KIND(frame) = &resume_observing_kind;
  KosDynFrame_RESUME_STATE(frame) = &marker;

  kos_dynframe_clear_resume(frame);
  TEST_ASSERT_EQ(observed_clear_calls, 1, "the kind's clear ran once");
  TEST_ASSERT_SAME(observed_clear_in, &marker,
                   "the clear was handed the frame's state");
  TEST_ASSERT_NULL(observed_kind_seen,
                   "the frame named no kind while the clear ran");
  TEST_ASSERT_NULL(observed_state_seen,
                   "the frame named no state while the clear ran");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_KIND(frame), "the kind is gone");
  TEST_ASSERT_NULL(KosDynFrame_RESUME_STATE(frame), "the state is gone");

  observed_frame = NULL;
  kos_dynframe_free(frame);
  Py_DECREF(ns);
  return 0;
}


/* A frame freed with its resume still set runs the clear with both
   resume fields already NULL. */
static int test_free_unnames_resume_before_clear(void) {
  PyObject *ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");

  int marker = 0;
  observing_reset(frame);
  KosDynFrame_RESUME_KIND(frame) = &resume_observing_kind;
  KosDynFrame_RESUME_STATE(frame) = &marker;

  kos_dynframe_free(frame);
  observed_frame = NULL;
  TEST_ASSERT_EQ(observed_clear_calls, 1, "the kind's clear ran once");
  TEST_ASSERT_SAME(observed_clear_in, &marker,
                   "the clear was handed the frame's state");
  TEST_ASSERT_NULL(observed_kind_seen,
                   "the frame named no kind while the clear ran");
  TEST_ASSERT_NULL(observed_state_seen,
                   "the frame named no state while the clear ran");

  Py_DECREF(ns);
  return 0;
}


/* A landing that applies a restore record clears the resume the live
   frame held while the context still names that frame; the clear runs
   with the frame's resume fields already NULL. */
static int test_capture_restore_apply_unnames_prior_before_clear(void) {
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
  KosDynFrame_RESUME_KIND(tmpl) = &resume_observing_kind;
  KosDynFrame_RESUME_STATE(tmpl) = &marker;

  PyObject *exc = PyObject_CallNoArgs(PyExc_ValueError);
  TEST_ASSERT_NOT_NULL(exc, "create payload");
  KosXferGoal *goal = kos_xfer_goal_new(NULL, (KosDynNode *) live, exc,
                                        NULL, NULL, 0);
  TEST_ASSERT_NOT_NULL(goal, "create goal");
  TEST_ASSERT_EQ(kos_xfer_goal_capture_restore(
                     goal, (KosDynNode *) live, (KosDynNode *) tmpl),
                 0, "the record is captured");

  /* the live frame carries a resume of its own the landing displaces */
  observing_reset(live);
  KosDynFrame_RESUME_KIND(live) = &resume_observing_kind;
  KosDynFrame_RESUME_STATE(live) = &prior;

  kos_xfer_goal_push(ctx, goal);
  {
    int boundary = 0;
    KosOpResult r = kos_xfer_goal_drive(ctx, &boundary);
    TEST_ASSERT_EQ(r.kind, (int) KOS_RES_NORMAL, "the goal drives through");
  }
  observed_frame = NULL;
  TEST_ASSERT_NULL(KosContext_GOAL(ctx), "the goal retired");
  TEST_ASSERT_EQ(observed_clear_calls, 1,
                 "the live frame's prior resume was cleared once");
  TEST_ASSERT_SAME(observed_clear_in, &prior,
                   "the clear was handed the prior state");
  TEST_ASSERT_NULL(observed_kind_seen,
                   "the live frame named no kind while the clear ran");
  TEST_ASSERT_NULL(observed_state_seen,
                   "the live frame named no state while the clear ran");
  TEST_ASSERT_SAME(KosDynFrame_RESUME_STATE(live), &recorded_clone_out,
                   "the live frame carries the record's clone");
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
  KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL, NULL);

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


/* === a transform step kind's traverse === */


static PyObject *traverse_target = NULL;
static int traverse_visits = 0;


static int count_target_visit(PyObject *obj, void *arg) {

  (void) arg;
  if (obj == traverse_target)
    traverse_visits++;
  return 0;
}


/**
   A callable answering every element with an awaitable that suspends
   once, yielding 'tick', and then returns 1. Returns the callable, or
   NULL with an error set.
*/
static PyObject *resume_ticking_callable(void) {

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


/**
   A map's step suspended on its callable's awaitable leaves the step
   frame on top of the map's own context, naming the map as its state.
   A stage on the transform's own context holds no reference to it (the
   map holds the context, so one would be a cycle), and the kind's
   traverse visits nothing, staged or finished; the finished step
   leaves the map as it was before the step.
*/
static int test_own_context_transform_stage_holds_no_reference(void) {

  PyObject *tick = resume_ticking_callable();
  PyObject *items, *t, *stepper, *res;
  KosDynNode *top;
  const KosResumeKind *kind = NULL;
  void *state = NULL;
  Py_ssize_t unheld, baseline, staged_refs, finished_refs;
  int staged_visits, finished_visits, delivered;

  TEST_ASSERT_NOT_NULL(tick, "build the ticking callable");
  items = Py_BuildValue("[i]", 1);
  TEST_ASSERT_NOT_NULL(items, "build the source list");

  t = PyObject_CallFunction((PyObject *) &KosMapType, "OO", tick, items);
  Py_DECREF(tick);
  Py_DECREF(items);
  TEST_ASSERT_NOT_NULL(t, "build the map");
  unheld = Py_REFCNT(t);

  stepper = PyObject_CallMethod(t, "__anext__", NULL);
  TEST_ASSERT_NOT_NULL(stepper, "take the map's __anext__");
  baseline = Py_REFCNT(t);

  res = PyObject_CallMethod(stepper, "send", "O", Py_None);
  TEST_ASSERT_NOT_NULL(res, "the step suspends on the awaitable");
  Py_DECREF(res);

  top = KosContext_DYN_TOP(kos_transform_pyctx(t));
  if (top && KosDynNode_IS_FRAME(top)) {
    kind = KosDynFrame_RESUME_KIND((KosDynFrame *) top);
    state = KosDynFrame_RESUME_STATE((KosDynFrame *) top);
  }
  staged_refs = Py_REFCNT(t);

  traverse_target = t;
  traverse_visits = 0;
  (void) kos_resume_traverse(kind, state, count_target_visit, NULL);
  staged_visits = traverse_visits;

  /* the awaitable returns: the element is delivered and the step
     finished */
  res = PyObject_CallMethod(stepper, "send", "O", Py_None);
  delivered = (! res && PyErr_ExceptionMatches(PyExc_StopIteration));
  Py_XDECREF(res);
  PyErr_Clear();

  traverse_visits = 0;
  (void) kos_resume_traverse(kind, t, count_target_visit, NULL);
  finished_visits = traverse_visits;
  finished_refs = Py_REFCNT(t);
  traverse_target = NULL;

  Py_DECREF(stepper);
  Py_DECREF(t);

  TEST_ASSERT_NOT_NULL(kind, "the suspended top frame carries a resume");
  TEST_ASSERT_SAME(state, t, "the step frame's state is the map");
  TEST_ASSERT_EQ(staged_refs, baseline,
                 "a stage on the map's own context holds no reference");
  TEST_ASSERT_EQ(staged_visits, 0,
                 "the kind's traverse visits no unheld map");
  TEST_ASSERT(delivered, "the second send delivers the element");
  TEST_ASSERT_EQ(finished_visits, 0, "a finished step visits nothing");
  /* the step ended, so the spent stepper let go of the map as well */
  TEST_ASSERT_EQ(finished_refs, unheld, "the finish releases the map");
  return 0;
}


/* === Suite === */


int run_resume_tests(void) {
  printf("\n=== CPS resume plumbing tests ===\n");

  RUN_TEST(test_cg_prologue_delivers_staged_resume);
  RUN_TEST(test_dispatch_delivers_through_prologue);
  RUN_TEST(test_next_resume_delivered_reports_in_hand);
  RUN_TEST(test_next_resume_exhausted_sets_the_resume);
  RUN_TEST(test_return_into_resume_parent);
  RUN_TEST(test_prologue_idle_threads_into_code);
  RUN_TEST(test_prologue_delivers_staged);
  RUN_TEST(test_prologue_runs_pending_exception);
  RUN_TEST(test_prologue_drops_staged_under_pending_exception);
  RUN_TEST(test_prologue_drops_stacked_under_pending_exception);
  RUN_TEST(test_prologue_offers_pending_exception_to_resume);
  RUN_TEST(test_prologue_resume_handles_pending_exception);
  RUN_TEST(test_raise_walk_defers_to_the_live_frame_resume);
  RUN_TEST(test_raise_walk_defers_after_a_pop_reveals_a_resume);
  RUN_TEST(test_clone_copies_node_resume);
  RUN_TEST(test_capture_restore_clones_and_clears_unapplied);
  RUN_TEST(test_capture_restore_apply_hands_over_the_clone);
  RUN_TEST(test_clear_resume_unnames_state_before_clear);
  RUN_TEST(test_free_unnames_resume_before_clear);
  RUN_TEST(test_capture_restore_apply_unnames_prior_before_clear);
  RUN_TEST(test_capture_restore_refuses_uncloneable_kind);
  RUN_TEST(test_clone_refuses_step_resume_frame);
  RUN_TEST(test_cache_inert_clears_resume_triple);
  RUN_TEST(test_reset_for_tail_clears_resume_triple);
  RUN_TEST(test_call_resume_null_returns_value_in_hand);
  RUN_TEST(test_drive_restores_thread_exc_info);
  RUN_TEST(test_own_context_transform_stage_holds_no_reference);

  return 0;
}


// The end.
