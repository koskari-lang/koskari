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
  koskari/lib/eval/eval_context.c

  KosContext: the evaluator execution context object that owns one
  running or suspended computation's dynamic chain, stacks, and
  namespaces. Construction, recycling, GC, and the Python surface
  (snapshot, the read-only properties, repr, the type). Included into
  eval.c as a segment (see the eval/ includes there) at the point the
  code sat, ahead of the frame helpers and opcode handlers.

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/* === KosContext implementation === */


/**
   The dynamic snapshot a new context starts from: the domain and the
   parameterize cells in force at this moment. Forked from the active
   context when there is one (a private copy of its shadow table, so the
   parameterize extents applied at this moment are captured; the cells
   are shared by pointer, and the root scope is reached through the
   domain); otherwise the domain of an enclosing `with Domain():` scope,
   else the thread default domain, with no cells. Both come back as new
   references, *cells NULL when there are none. s is the calling
   thread's record (kos_tls_current). Returns -1 with an error set.
*/
int KosContext_Snapshot(KosTlsState *s, PyObject **domain, PyObject **cells) {

  KosContext *active = kos_tls_active_context(s);

  *domain = NULL;
  *cells = NULL;

  if (active) {
    PyObject *table = KosContext_PARAM_REFS(active);
    if (table && PyDict_GET_SIZE(table) > 0) {
      *cells = PyDict_Copy(table);
      if (! *cells)
        return -1;
    }
    *domain = KosContext_DOMAIN(active);
    Py_XINCREF(*domain);
    return 0;
  }

  *domain = kos_tls_domain(s);
  if (! *domain)
    return -1;
  Py_INCREF(*domain);
  return 0;
}


/**
   A context over top, starting from a snapshot taken earlier with
   KosContext_Snapshot. domain and cells are stolen (cells may be
   NULL); top's ownership transfers to the context on success and stays
   with the caller on failure.
*/
KosContext *KosContext_NewFromSnapshot(
    KosDynFrame *top,
    PyObject *domain,
    PyObject *cells) {

  KosContext *ctx = PyObject_GC_New(KosContext, &KosContextType);
  if (! ctx) {
    Py_XDECREF(domain);
    Py_XDECREF(cells);
    return NULL;
  }

  /* every field after the object header starts cleared, so a failure
     below can release the half-built context through the type */
  memset((char *) ctx + sizeof(PyObject), 0,
         sizeof(KosContext) - sizeof(PyObject));

  ctx->domain = domain;
  ctx->param_refs = cells;

  /* the live registers over the one-frame chain: what a walk of the
     chain from top derives */
  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  if (top) {
    KosDynNode *node = (KosDynNode *) top;

    KosContext_FRAME(ctx) = top;
    KosContext_BLOCK(ctx) = KosDynNode_STACK(node)
        ? &KosDynNode_STACK(node) : NULL;
    KosContext_NAMESPACE(ctx) = KosDynNode_NS(node);
    KosContext_CODE(ctx) = KosDynFrame_CODE(top);
    ctx->pc = KosDynFrame_PC(top);
    ctx->acting_pc = KosDynFrame_ACTING_PC(top);
  }

  KosContext_GOAL(ctx) = NULL;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));

  /* Initialize Python await bridging */
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  KosContext_ENTRY_CALL(ctx) = NULL;

  /* Initialize exception injection */
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_TYPE(ctx) = NULL;
  KosContext_INJECT_VALUE(ctx) = NULL;

  KosContext_FLAGS(ctx) = 0;

  PyObject_GC_Track((PyObject *) ctx);
  return ctx;
}


/**
   Replace a cached context's domain and parameterize cells with the
   snapshot in force now, so a context reused across boundary calls
   sees the dynamic state of each call. s is the calling thread's
   record (kos_tls_current). Returns -1 with an error set.
*/
int KosContext_TakeSnapshot(KosContext *ctx, KosTlsState *s) {

  PyObject *domain;
  PyObject *cells;

  if (KosContext_Snapshot(s, &domain, &cells) < 0)
    return -1;
  Py_XSETREF(ctx->domain, domain);
  Py_XSETREF(ctx->param_refs, cells);
  return 0;
}


KosContext *KosContext_New(KosDynFrame *top, KosTlsState *s) {

  PyObject *domain;
  PyObject *cells;

  if (KosContext_Snapshot(s, &domain, &cells) < 0)
    return NULL;
  return KosContext_NewFromSnapshot(top, domain, cells);
}


/**
   The context's evaluator token, minted here on first use. Only a
   continuation capture needs one: it is the ownership marker a captured
   continuation carries, and its live depth answers whether the owning
   evaluator is on the stack. A context that never captures never
   allocates one. The depth is seeded from the running flag so a token
   minted mid-drive reads as live until that drive exits.
*/
KosEvaluatorToken *KosContext_EnsureToken(KosContext *ctx) {

  KosEvaluatorToken *token = KosContext_TOKEN(ctx);

  if (! token) {
    token = evaluator_token_new();
    if (! token)
      return NULL;
    token->live_depth = (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING) ? 1 : 0;
    KosContext_TOKEN(ctx) = token;
  }
  return token;
}


/**
   Memory teardown only: releases dyn nodes and operand stacks via
   kos_dynchain_free without running the extent protocol (WITH
   __exit__, PARAM restore, WIND hooks). A holder with a frozen async
   generator runs KosPyAsyncGen_Teardown before dropping its reference
   so GEN.active is disposed before the context. Also the type's
   tp_clear, so a context inside a garbage cycle drops what it holds.
*/
static int context_clear(PyObject *self) {

  KosContext *ctx = (KosContext *) self;

  Py_XDECREF(KosContext_TOKEN(ctx));
  KosContext_TOKEN(ctx) = NULL;

  /* the live registers point into the chain freed here; a finalizer
     in the same garbage cycle can still reach the context, so nothing
     on it may point at freed memory */
  if (KosContext_DYN_TOP(ctx))
    kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);
  KosContext_DYN_TOP(ctx) = NULL;
  KosContext_FRAME(ctx) = NULL;
  KosContext_BLOCK(ctx) = NULL;
  KosContext_NAMESPACE(ctx) = NULL;
  KosContext_CODE(ctx) = NULL;
  ctx->pc = 0;
  ctx->acting_pc = 0;

  while (KosContext_GOAL(ctx)) {
    KosXferGoal *goal = KosContext_GOAL(ctx);
    KosContext_GOAL(ctx) = KosXferGoal_OUTER(goal);
    kos_xfer_goal_free(goal);
  }

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
  Py_CLEAR(KosContext_INJECT_TYPE(ctx));
  Py_CLEAR(KosContext_INJECT_VALUE(ctx));
  KosExc_Clear(KosContext_EXC_INFO(ctx));
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;
  Py_XDECREF(KosContext_PARAM_REFS(ctx));
  KosContext_PARAM_REFS(ctx) = NULL;

  return 0;
}


static void context_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  context_clear(self);
  Py_TYPE(self)->tp_free(self);
}


/**
   Make a context reusable after a completion at its root: drop the
   finished frame chain and clear the completion residue. A context
   with an active transfer goal is left untouched -- the caller frees
   it instead.
*/
int KosContext_Recycle(KosContext *ctx) {

  if (KosContext_GOAL(ctx))
    return -1;

  /* A continuation captured in the finished run carries this token; the
     next run on the recycled context is a different evaluation and must
     not answer to it. The next capture mints a fresh one. */
  Py_CLEAR(KosContext_TOKEN(ctx));

  /* the live registers point into the chain freed here; the next
     drive derives them again from the frame it installs */
  if (KosContext_DYN_TOP(ctx))
    kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);
  KosContext_DYN_TOP(ctx) = NULL;
  KosContext_FRAME(ctx) = NULL;
  KosContext_BLOCK(ctx) = NULL;
  KosContext_NAMESPACE(ctx) = NULL;
  KosContext_CODE(ctx) = NULL;
  ctx->pc = 0;
  ctx->acting_pc = 0;

  KosContext_FLAGS(ctx) &= ~(KOS_CTX_DEAD | KOS_CTX_RUNNING);
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  /* residue of the finished run that the next one must not inherit:
     an unconsumed entry call, a pending throw injection, and the
     handler-visible exception state */
  KosContext_ENTRY_CALL(ctx) = NULL;
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  Py_CLEAR(KosContext_INJECT_TYPE(ctx));
  Py_CLEAR(KosContext_INJECT_VALUE(ctx));
  KosExc_Clear(KosContext_EXC_INFO(ctx));

  return 0;
}


static int context_traverse(PyObject *self, visitproc visit, void *arg) {

  KosContext *ctx = (KosContext *) self;
  KosDynNode *dyn_top;
  KosXferGoal *goal;
  int res;

  Py_VISIT(KosContext_TOKEN(ctx));
  Py_VISIT(KosContext_DOMAIN(ctx));
  Py_VISIT(KosContext_PARAM_REFS(ctx));
  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_TYPE(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));
  if (KosExc_Traverse(KosContext_EXC_INFO(ctx), visit, arg))
    return -1;

  Py_VISIT(KosContext_PENDING_VALUE(ctx));
  dyn_top = KosContext_DYN_TOP(ctx);
  goal = KosContext_GOAL(ctx);

  res = kos_dynchain_traverse(dyn_top, NULL, visit, arg);
  if (res)
    return res;

  /* the active goal and every waiting outer behind it */
  for (; goal; goal = KosXferGoal_OUTER(goal)) {
    res = kos_xfer_goal_traverse(goal, visit, arg);
    if (res)
      return res;
  }

  return 0;
}


/* === KosContext Python surface === */


static int context_snapshot_set(
    PyObject *dict,
    const char *key,
    PyObject *value) {

  if (! value)
    return -1;
  if (PyDict_SetItemString(dict, key, value) < 0) {
    Py_DECREF(value);
    return -1;
  }
  Py_DECREF(value);
  return 0;
}


static PyObject *context_get_namespace(PyObject *self, void *unused) {

  PyObject *ns = KosContext_NAMESPACE(self);
  if (! ns)
    Py_RETURN_NONE;
  return KosNamespace_Reveal(ns);
}


static PyObject *context_snapshot(PyObject *self, PyObject *unused) {

  KosContext *ctx = (KosContext *) self;
  PyObject *out;

  (void) unused;

  out = PyDict_New();
  if (! out)
    return NULL;

  if (context_snapshot_set(out, "schema_version", PyLong_FromLong(1)) < 0
      || context_snapshot_set(out, "context_id",
                              PyLong_FromVoidPtr((void *) ctx)) < 0
      || context_snapshot_set(out, "flags",
                              PyLong_FromUnsignedLong(
                                  KosContext_FLAGS(ctx))) < 0
      || context_snapshot_set(out, "suspension_event",
                              PyLong_FromUnsignedLong(
                                  KosContext_SUSPENSION_EVENT(ctx))) < 0
      || context_snapshot_set(out, "domain",
                              Py_NewRef(KosContext_DOMAIN(ctx)
                                        ? KosContext_DOMAIN(ctx)
                                        : Py_None)) < 0
      || context_snapshot_set(out, "namespace",
                              context_get_namespace(self, NULL)) < 0
      || context_snapshot_set(out, "dynnodes",
                              kos_dynchain_snapshot_to_list(
                                  KosContext_DYN_TOP(ctx))) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  return out;
}


static PyObject *context_get_domain(PyObject *self, void *unused) {

  PyObject *domain = KosContext_DOMAIN(self);
  return Py_NewRef(domain ? domain : Py_None);
}


static PyObject *context_get_running(PyObject *self, void *unused) {

  return PyBool_FromLong((KosContext_FLAGS(self) & KOS_CTX_RUNNING) != 0);
}


static PyObject *context_get_dead(PyObject *self, void *unused) {

  return PyBool_FromLong((KosContext_FLAGS(self) & KOS_CTX_DEAD) != 0);
}


static PyObject *context_get_suspension_event(PyObject *self, void *unused) {

  return PyLong_FromUnsignedLong(KosContext_SUSPENSION_EVENT(self));
}


static PyObject *context_repr(PyObject *self) {

  KosContext *ctx = (KosContext *) self;
  const char *state =
      (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING) ? "running"
      : (KosContext_FLAGS(ctx) & KOS_CTX_DEAD) ? "dead"
      : KosContext_DYN_TOP(ctx) ? "suspended"
      : "idle";

  return PyUnicode_FromFormat("<koskari.lib.Context %s at %p>", state, ctx);
}


static PyMethodDef context_methods[] = {
  { "snapshot", (PyCFunction) context_snapshot, METH_NOARGS,
    "snapshot() -> dict\n"
    "A picture of the context now: flags, suspension event, domain, the"
    " acting namespace, and one entry per dynamic chain node with its"
    " kind, namespace, operand stack, and payload." },
  { NULL, NULL, 0, NULL },
};


static PyGetSetDef context_getset[] = {
  { "domain", context_get_domain, NULL,
    "The Domain this context resolves parameters against", NULL },
  { "namespace", context_get_namespace, NULL,
    "The namespace the context is acting in, or None", NULL },
  { "running", context_get_running, NULL,
    "bool; a drive is running on this context", NULL },
  { "dead", context_get_dead, NULL,
    "bool; the context completed or failed at its root", NULL },
  { "suspension_event", context_get_suspension_event, NULL,
    "int; why the last drive left the evaluator", NULL },
  { NULL, NULL, NULL, NULL, NULL },
};


PyTypeObject KosContextType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Context",
  .tp_basicsize = sizeof(KosContext),
  .tp_itemsize = 0,
  .tp_dealloc = context_dealloc,
  .tp_repr = context_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "An evaluator execution context: the dynamic chain, operand"
            " stacks, and namespaces of one running or suspended"
            " computation. Created by the runtime, never from Python.",
  .tp_traverse = context_traverse,
  .tp_clear = context_clear,
  .tp_methods = context_methods,
  .tp_getset = context_getset,
};


// The end.
