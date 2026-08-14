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
   koskari/lib/eval/eval_state.c

   KosEvalState init, thaw-steal, freeze-restore, sync repair, and stack-owner
   helpers; included into eval.c.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


void KosEvalState_Init(KosEvalState *es, KosContext *ctx) {

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (es)
    memset(es, 0, sizeof(KosEvalState));
  KosEvalState_CONTEXT(es) = ctx;

  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosEvalState_ThawFromContext(KosEvalState *es) {

  KosContext *ctx;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es)
    goto done;
  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    goto done;

  KosEvalState_DYN_TOP(es) = KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = NULL;

  KosEvalState_PLAN(es) = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;

  KosEvalState_PLAN_STACK(es) = KosContext_PLAN_STACK(ctx);
  KosContext_PLAN_STACK(ctx) = NULL;

  KosEvalState_FRAME(es) = KosContext_FRAME_TOP(ctx);
  KosContext_FRAME_TOP(ctx) = NULL;

  KosEvalState_BLOCK(es) = KosContext_BLOCK(ctx);
  KosContext_BLOCK(ctx) = NULL;

  KosEvalState_NAMESPACE(es) = KosContext_NAMESPACE(ctx);
  KosContext_NAMESPACE(ctx) = NULL;

  if (! KosEvalState_NAMESPACE(es) && KosEvalState_FRAME(es))
    KosEvalState_NAMESPACE(es) = KosDynFrame_NS(KosEvalState_FRAME(es));

  if (KosEvalState_FRAME(es)) {
    KosEvalState_CODE(es) = KosDynFrame_CODE(KosEvalState_FRAME(es));
    (es)->pc = KosDynFrame_PC(KosEvalState_FRAME(es));
    (es)->acting_pc = KosDynFrame_ACTING_PC(KosEvalState_FRAME(es));
  } else {
    KosEvalState_CODE(es) = NULL;
    (es)->pc = 0;
    (es)->acting_pc = 0;
  }

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosEvalState_FreezeToContext(KosEvalState *es) {

  KosContext *ctx;

  if (! es)
    return;
  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    return;

  KosContext_DYN_TOP(ctx) = KosEvalState_DYN_TOP(es);
  KosEvalState_DYN_TOP(es) = NULL;

  KosContext_PLAN(ctx) = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;

  KosContext_PLAN_STACK(ctx) = KosEvalState_PLAN_STACK(es);
  KosEvalState_PLAN_STACK(es) = NULL;

  KosContext_FRAME_TOP(ctx) = KosEvalState_FRAME(es);
  KosEvalState_FRAME(es) = NULL;

  KosContext_BLOCK(ctx) = KosEvalState_BLOCK(es);
  KosEvalState_BLOCK(es) = NULL;

  KosContext_NAMESPACE(ctx) = KosEvalState_NAMESPACE(es);
  KosEvalState_NAMESPACE(es) = NULL;

  KosEvalState_CODE(es) = NULL;
  (es)->pc = 0;
  (es)->acting_pc = 0;

  if (KosContext_PLAN(ctx))
    KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  else
    KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
}


KosDynNode *KosEvalState_InnermostStackOwner(KosEvalState *es) {
  KosDynNode *n = KosEvalState_DYN_TOP(es);
  while (n) {
    if (KosDynNode_STACK(n))
      return n;
    n = KosDynNode_PARENT(n);
  }
  return NULL;
}


/**
   Borrowed constant from the current code object's pool.

   Under KOS_EVAL_SAFETYNET, validates code/index and rejects a NULL slot
   (sets an error, returns NULL). Without the net the pool is trusted as
   finalized at compile time.
*/
static inline PyObject *KosEvalState_GetConstant(
    KosEvalState *es,
    uint32_t index) {

  KosCode *code = KosEvalState_CODE(es);

#if KOS_EVAL_SAFETYNET
  if (unlikely(! code || index >= KosCode_CPOOL_LEN(code))) {
    PyErr_Format(PyExc_IndexError, "constant index out of bounds, %u",
                 index);
    return NULL;
  }
#endif

  PyObject *val = KosCode_GET_CONST(code, index);

#if KOS_EVAL_SAFETYNET
  if (unlikely(! val)) {
    PyErr_SetString(PyExc_RuntimeError, "NULL constant in pool");
    return NULL;
  }
#endif

  return val;
}


/* Hot-path constant load: direct pool index when the safety net is off;
   guarded GetConstant when it is on. */
#if KOS_EVAL_SAFETYNET
# define KosEvalState_CONST(es, index) \
    KosEvalState_GetConstant((es), (index))
#else
# define KosEvalState_CONST(es, index) \
    KosCode_GET_CONST(KosEvalState_CODE(es), (index))
#endif


static KosSegment **kos_evalstate_parent_stack_slot(
    KosEvalState *es) {

  KosDynNode *owner;
  KosDynNode *node;

  /* Unwind cache: the live segment's back is the enclosing owning
     slot recorded at install. Note the deliberate exception this
     serves: a parent delivery mutates a segment that is not the
     active push target, writing through the owning slot so a grow
     survives. Cache miss (NULL) falls through to the chain walk,
     which stays the semantic definition. */
  {
    KosSegment **cur = KosEvalState_BLOCK(es);
    if (likely(cur && *cur) && KosSegment_BACK(*cur)) {
      KosSegment **cached = KosSegment_BACK(*cur);
#if KOS_DEBUG_PATCH_DYN_CACHES
      KosDynNode *owner_dbg = KosEvalState_InnermostStackOwner(es);
      if (owner_dbg) {
        KosDynNode *n_dbg = KosDynNode_PARENT(owner_dbg);
        for (; n_dbg; n_dbg = KosDynNode_PARENT(n_dbg)) {
          if (KosDynNode_STACK(n_dbg))
            break;
        }
        if (! n_dbg || cached != &KosDynNode_STACK(n_dbg)) {
          PyErr_SetString(PyExc_SystemError,
                          "kos_evalstate_parent_stack_slot: back cache"
                          " drift vs walk");
          return NULL;
        }
      }
#endif
      return cached;
    }
  }

  owner = KosEvalState_InnermostStackOwner(es);
  if (unlikely(! owner)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KosEvalState_ParentStack*: no current stack owner");
    return NULL;
  }

  for (node = KosDynNode_PARENT(owner); node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_STACK(node))
      return &KosDynNode_STACK(node);
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "KosEvalState_ParentStack*: no parent stack owner");
  return NULL;
}


int KosEvalState_ParentStackPushRef(KosEvalState *es, PyObject *val) {
  KosSegment **slot = kos_evalstate_parent_stack_slot(es);
  if (unlikely(! slot))
    return -1;

  return KosStack_PushRef(slot, val);
}


int KosEvalState_ParentStackPUSH(KosEvalState *es, PyObject *val) {
  KosSegment **slot = kos_evalstate_parent_stack_slot(es);
  if (unlikely(! slot)) {
    Py_XDECREF(val);
    return -1;
  }
  return KosStack_PUSH(slot, val);
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
