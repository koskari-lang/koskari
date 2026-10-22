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
   koskari/lib/eval/eval_drive.c

   The drive begin step, the parent stack slot, the UNPACK sequence
   helper, and the evaluator token; included into eval.c.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


void KosContext_BeginDrive(
    KosContext *ctx,
    const volatile KosBreakerWord *breaker) {

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! breaker)
    breaker = koskari_eval_breaker_locate();
  if (! breaker)
    breaker = &koskari_breaker_always;
  KosContext_BREAKER(ctx) = breaker;

  {
    int limit = Py_GetRecursionLimit();
    if (limit < 1)
      limit = 1;
    else if (limit > KOS_FRAME_DEPTH_MAX)
      limit = KOS_FRAME_DEPTH_MAX;
    KosContext_RECURSION_LIMIT(ctx) = (uint16_t) limit;
  }
  ctx->terminal = 0;

  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosContext_EndDrive(KosContext *ctx) {

  KosContext_FlushLiveRegsToFrame(ctx);

  /* A staged delivery outlives the drive only as storage on the frame
     it was staged for; the delivery step takes it back from the stack
     on the next trip. That storage is a code-less step frame's alone:
     a bytecode frame's stack holds its operands and the delivery step
     never pops it, so every site that stages for a bytecode frame
     delivers in the same trip and no drive exit can see one. */
  if (KosContext_PENDING_VALUE(ctx)) {
    PyObject *staged = KosContext_PENDING_VALUE(ctx);
    KosContext_PENDING_VALUE(ctx) = NULL;
    if (KOS_EVAL_SAFETYNET
        && KosContext_FRAME(ctx)
        && KosDynFrame_CODE(KosContext_FRAME(ctx))) {
      Py_DECREF(staged);
      PyErr_SetString(PyExc_SystemError,
                      "delivery staged for a bytecode frame across a"
                      " drive exit");
    } else if (KosContext_BLOCK(ctx))
      (void) KosContext_StackPUSH(ctx, staged);  /* consumes on failure */
    else
      Py_DECREF(staged);
  }
}


int kos_eval_recursion_error(void) {
  PyErr_SetString(PyExc_RecursionError,
                  "maximum recursion depth exceeded");
  return -1;
}


KosDynNode *KosContext_InnermostStackOwner(KosContext *ctx) {
  KosDynNode *n = KosContext_DYN_TOP(ctx);
  while (n) {
    if (KosDynNode_STACK(n))
      return n;
    n = KosDynNode_PARENT(n);
  }
  return NULL;
}


static KosSegment **kos_context_parent_stack_slot(
    KosContext *ctx) {

  KosDynNode *owner;
  KosDynNode *node;

  /* Unwind cache: the live segment's back is the enclosing owning
     slot recorded at install. Note the deliberate exception this
     serves: a parent delivery mutates a segment that is not the
     active push target, writing through the owning slot so a grow
     survives. Cache miss (NULL) falls through to the chain walk,
     which stays the semantic definition. */
  {
    KosSegment **cur = KosContext_BLOCK(ctx);
    if (likely(cur && *cur && KosSegment_BACK(*cur))) {
      KosSegment **cached = KosSegment_BACK(*cur);
#if KOS_DEBUG_PATCH_DYN_CACHES
      KosDynNode *owner_dbg = KosContext_InnermostStackOwner(ctx);
      if (owner_dbg) {
        KosDynNode *n_dbg = KosDynNode_PARENT(owner_dbg);
        for (; n_dbg; n_dbg = KosDynNode_PARENT(n_dbg)) {
          if (KosDynNode_STACK(n_dbg))
            break;
        }
        if (! n_dbg || cached != &KosDynNode_STACK(n_dbg)) {
          PyErr_SetString(PyExc_SystemError,
                          "kos_context_parent_stack_slot: back cache"
                          " drift vs walk");
          return NULL;
        }
      }
#endif
      return cached;
    }
  }

#if KOS_EVAL_SAFETYNET
  /* Safety net: recompute the parent slot by walk. Unreachable under
     the cache model (the counter measured zero across the benchmark
     battery); kept as a diagnostic for model violations. */
  KOS_TRACE_EVENTS_INC(KOS_TE_PARENT_SLOT_WALK);
  owner = KosContext_InnermostStackOwner(ctx);
  if (unlikely(! owner)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KosContext_ParentStack*: no current stack owner");
    return NULL;
  }

  for (node = KosDynNode_PARENT(owner); node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_STACK(node))
      return &KosDynNode_STACK(node);
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "KosContext_ParentStack*: no parent stack owner");
  return NULL;
#else
  /* The cache model is authoritative; a miss here is a model
     violation. Fail clean; build with -DKOS_EVAL_SAFETYNET=1 to
     diagnose by walk instead. */
  (void) owner;
  (void) node;
  PyErr_SetString(PyExc_RuntimeError,
                  "KosContext_ParentStack*: unwind cache missing"
                  " (KOS_EVAL_SAFETYNET=1 to diagnose)");
  return NULL;
#endif
}


int KosContext_ParentStackPushRef(KosContext *ctx, PyObject *val) {
  KosSegment **slot = kos_context_parent_stack_slot(ctx);
  if (unlikely(! slot))
    return -1;

  return KosStack_PushRef(slot, val);
}


int KosContext_ParentStackPUSH(KosContext *ctx, PyObject *val) {
  KosSegment **slot = kos_context_parent_stack_slot(ctx);
  if (unlikely(! slot)) {
    Py_XDECREF(val);
    return -1;
  }
  return KosStack_PUSH(slot, val);
}


/* Exactly need items from an iterable into a new tuple. One pull past
   need refuses an over-long iterable at its extra item instead of
   draining it. */
static PyObject *unpack_exact_pull(
    PyObject *raw,
    Py_ssize_t need,
    const char *who) {

  PyObject *it = PyObject_GetIter(raw);
  if (unlikely(! it)) {
    if (PyErr_ExceptionMatches(PyExc_TypeError)) {
      PyErr_Format(PyExc_TypeError,
                   "%s: cannot unpack non-iterable %.100s object",
                   who, Py_TYPE(raw)->tp_name);
    }
    return NULL;
  }

  PyObject *seq = PyTuple_New(need);
  if (unlikely(! seq)) {
    Py_DECREF(it);
    return NULL;
  }

  Py_ssize_t got = 0;
  while (got < need) {
    PyObject *item = PyIter_Next(it);
    if (! item) {
      Py_DECREF(it);
      Py_DECREF(seq);
      if (! PyErr_Occurred()) {
        PyErr_Format(PyExc_ValueError,
                     "%s: expected %zd elements, got %zd",
                     who, need, got);
      }
      return NULL;
    }
    PyTuple_SET_ITEM(seq, got, item);
    got++;
  }

  PyObject *extra = PyIter_Next(it);
  Py_DECREF(it);
  if (extra) {
    Py_DECREF(extra);
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "%s: expected %zd elements, got more", who, need);
    return NULL;
  }
  if (unlikely(PyErr_Occurred())) {
    Py_DECREF(seq);
    return NULL;
  }
  return seq;
}


PyObject *KosContext_PopUnpackSequence(
    KosContext *ctx,
    Py_ssize_t need,
    int exact,
    const char *who) {

  PyObject *raw = KosContext_StackPopRef(ctx);
  PyObject *seq;
  if (unlikely(! raw))
    return NULL;

  /* a tuple or list is already the fast sequence: keep the popped
     reference instead of asking libpython to hand it back */
  if (PyTuple_CheckExact(raw) || PyList_CheckExact(raw)) {
    seq = raw;
  } else if (exact) {
    seq = unpack_exact_pull(raw, need, who);
    Py_DECREF(raw);
    return seq;
  } else {
    seq = PySequence_Fast(raw, "");
    if (unlikely(! seq)) {
      if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Format(PyExc_TypeError,
                     "%s: cannot unpack non-iterable %.100s object",
                     who, Py_TYPE(raw)->tp_name);
      }
      Py_DECREF(raw);
      return NULL;
    }
    Py_DECREF(raw);
  }

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (unlikely(exact ? (slen != need) : (slen < need))) {
    PyErr_Format(PyExc_ValueError,
                 "%s: expected %s%zd elements, got %zd",
                 who, exact ? "" : "at least ", need, slen);
    Py_DECREF(seq);
    return NULL;
  }

  return seq;
}


/* === KosEvaluatorToken type === */


static uint64_t token_cookie_counter = 0;


static PyObject *token_repr(PyObject *self) {
  KosEvaluatorToken *t = (KosEvaluatorToken *) self;
  return PyUnicode_FromFormat("<KosEvaluatorToken cookie=%llu live_depth=%d>",
                              (unsigned long long) t->cookie,
                              t->live_depth);
}


PyTypeObject KosEvaluatorTokenType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.EvaluatorToken",
  .tp_basicsize = sizeof(KosEvaluatorToken),
  .tp_itemsize = 0,
  .tp_repr = token_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
};


static KosEvaluatorToken *evaluator_token_new(void) {

  KosEvaluatorToken *token = PyObject_New(
    KosEvaluatorToken,
    &KosEvaluatorTokenType);

  if (! token)
    return NULL;

  token->cookie = (uint64_t) __sync_add_and_fetch(&token_cookie_counter, 1);
  token->live_depth = 0;
  return token;
}


void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token) {
  if (token)
    token->live_depth++;
}


void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token) {
  if (token && token->live_depth > 0)
    token->live_depth--;
}


// The end.
