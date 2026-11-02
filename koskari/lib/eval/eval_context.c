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


/* === KosContext freelist === */


#define CONTEXT_MAX_FREE KOS_FREELIST_CAP(256)  /* cached KosContext cap */

/* Released contexts: cleared, GC-untracked, linked through the domain
   field, which the constructor clears again on revival. */
static KosContext *context_freelist = NULL;
static size_t context_freecount = 0;


/** Free every context on the freelist; the eval unit's clear calls it. */
static void context_freelist_drain(void) {

  while (context_freelist) {
    KosContext *ctx = context_freelist;
    context_freelist = (KosContext *) ctx->domain;
    PyObject_GC_Del(ctx);
  }
  context_freecount = 0;
}


/* === KosContext implementation === */


/**
   The dynamic snapshot a new context starts from: the parameterize
   cells in force at this moment, the ambient table
   (kos_param_ambient_cells): a Python parameterize span open inside
   the active context, else the active context's shadow table. The
   table is shared with its holder so the extents applied at this
   moment are captured; the first writer on either side copies it
   first, see kos_context_param_refs. No domain is part of it: a
   context takes its domain at each eval entry from the thread running
   it. s is the calling thread's record (kos_tls_current). Returns a
   new reference to the cells, or NULL when there are none; it cannot
   fail.
*/
PyObject *KosContext_Snapshot(KosTlsState *s) {

  KosContext *active = kos_tls_active_context(s);
  PyObject *table = kos_param_ambient_cells(s, active);

  if (table && PyDict_GET_SIZE(table) > 0) {
    Py_INCREF(table);
    return table;
  }
  return NULL;
}


/**
   A context over top, starting from a snapshot taken earlier with
   KosContext_Snapshot. cells is stolen (may be NULL); top's ownership
   transfers to the context on success and stays with the caller on
   failure. The context has no domain until an eval enters it.
*/
KosContext *KosContext_NewFromSnapshot(KosDynFrame *top, PyObject *cells) {

  KosContext *ctx = context_freelist;

  if (ctx) {
    context_freelist = (KosContext *) ctx->domain;
    context_freecount--;
    KOS_TRACE_FL_ALLOC("context", 1);
    /* the revival CPython's own type freelists use: a refcount of one,
       and in a debug build the reference total and trace chain the
       dealloc gave up */
    _Py_NewReference((PyObject *) ctx);
    ctx->domain = NULL;

  } else {
    KOS_TRACE_FL_ALLOC("context", 0);
    ctx = PyObject_GC_New(KosContext, &KosContextType);
    if (! ctx) {
      Py_XDECREF(cells);
      return NULL;
    }
    memset((char *) ctx + sizeof(PyObject), 0,
           sizeof(KosContext) - sizeof(PyObject));
  }

  /* every field after the object header starts cleared, by the memset
     for a fresh context and by context_clear for a revived one, so a
     failure below can release the half-built context through the type,
     and a revived context carries nothing of its last use */
  ctx->param_refs = cells;

  /* the live registers over the one-frame chain: what a walk of the
     chain from top derives */
  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  if (top) {
    KosDynNode *node = (KosDynNode *) top;

    /* the root of a one-frame chain; a frame from a function's cache
       arrives with the cache list's length in this field */
    KosDynFrame_DEPTH(top) = 1;
    KosContext_LoadFrame(
        ctx, top,
        KosDynNode_STACK(node) ? &KosDynNode_STACK(node) : NULL,
        KosDynNode_NS(node));
  }

  PyObject_GC_Track((PyObject *) ctx);
  return ctx;
}


KosContext *KosContext_New(KosDynFrame *top, KosTlsState *s) {
  return KosContext_NewFromSnapshot(top, KosContext_Snapshot(s));
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
   A cleared context is empty, every field after the object header
   zero, which the freelist revival relies on, so a field added to
   KosContext is cleared here.
*/
void KosContext_ReleaseChain(KosContext *ctx) {

  KosDynNode *top = KosContext_DYN_TOP(ctx);

  /* Detach first, free second. Freeing a node runs Python: a local's
     __del__, the namespace reset of a frame returning to its cache, a
     delegate's close. Anything reaching this context from there, a
     collection traversing it, a stepper's abandon drive, a recycle,
     must find no register naming a node already freed, freelisted or
     cached, or it walks freelist links as a chain. */
  KosContext_ClearLiveRegs(ctx);
  if (top)
    kos_dynchain_free(top, NULL);
}


static int context_clear(PyObject *self) {

  KosContext *ctx = (KosContext *) self;

  Py_XDECREF(KosContext_TOKEN(ctx));
  KosContext_TOKEN(ctx) = NULL;

  /* a finalizer in the same garbage cycle can still reach the context
     while its chain is freed, so the registers are cleared first */
  KosContext_ReleaseChain(ctx);

  while (KosContext_GOAL(ctx)) {
    KosXferGoal *goal = KosContext_GOAL(ctx);
    KosContext_GOAL(ctx) = KosXferGoal_OUTER(goal);
    kos_xfer_goal_free(goal);
  }

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
  Py_CLEAR(KosContext_PENDING_VALUE(ctx));
  KosExc_Clear(KosContext_EXC_INFO(ctx));
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;
  Py_XDECREF(KosContext_PARAM_REFS(ctx));
  KosContext_PARAM_REFS(ctx) = NULL;

  ctx->breaker = NULL;
  ctx->recursion_limit = 0;
  ctx->terminal = 0;
  KosContext_ENTRY_CALL(ctx) = NULL;
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;
  KosContext_FLAGS(ctx) = 0;

  return 0;
}


/**
   Clear the context and keep it on the freelist while it has room;
   free it otherwise. The type takes no subclasses, so every context is
   the same size.
*/
static void context_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  context_clear(self);

  if (context_freecount < CONTEXT_MAX_FREE) {
    ((KosContext *) self)->domain = (PyObject *) context_freelist;
    context_freelist = (KosContext *) self;
    context_freecount++;
    KOS_TRACE_FL_RETURN("context", 0);
    return;
  }

  KOS_TRACE_FL_RETURN("context", 1);
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

  /* the next drive derives the live registers again from the frame it
     installs */
  KosContext_ReleaseChain(ctx);

  KosContext_FLAGS(ctx) &= ~(KOS_CTX_DEAD | KOS_CTX_RUNNING);
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));

  /* residue of the finished run that the next one must not inherit:
     an unconsumed entry call, a staged delivery, and the
     handler-visible exception state */
  KosContext_ENTRY_CALL(ctx) = NULL;
  Py_CLEAR(KosContext_PENDING_VALUE(ctx));
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
