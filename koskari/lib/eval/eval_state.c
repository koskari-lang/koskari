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

   KosEvalState init, sync-from-context repair, freeze-to-context, and
   stack-owner helpers; included into eval.c.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: claude-4.6-sonnet-high via Cursor
*/


void KosEvalState_Init(KosEvalState *es, KosContext *ctx) {

  KOS_TRACE_EVAL_SYNC_BEGIN();

  KosEvalState_CONTEXT(es) = ctx;
  KosEvalState_FRAME(es) = KosContext_FRAME_TOP(ctx);
  KosEvalState_BLOCK(es) = KosContext_BLOCK(ctx);
  KosEvalState_NAMESPACE(es) = KosContext_NAMESPACE(ctx);
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

  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosEvalState_SyncFromContext(KosEvalState *es) {

  KosContext *ctx;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    goto done;

  {
    KosDynFrame *ft = KosContext_FRAME_TOP(ctx);
    KosStackBlock **bl = KosContext_BLOCK(ctx);
    PyObject *ns = KosContext_NAMESPACE(ctx);
    KosDynFrame *ef = KosEvalState_FRAME(es);
    if (! ns && ft)
      ns = KosDynFrame_NS(ft);

    if (ef == ft &&
        KosEvalState_BLOCK(es) == bl &&
        KosEvalState_NAMESPACE(es) == ns) {
      if (! ft) {
        if (! KosEvalState_CODE(es) && ! (es)->pc && ! (es)->acting_pc)
          goto done;
        KosEvalState_CODE(es) = NULL;
        (es)->pc = 0;
        (es)->acting_pc = 0;
        goto done;
      }
      if (KosEvalState_CODE(es) == KosDynFrame_CODE(ft))
        goto done;
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      (es)->pc = KosDynFrame_PC(ft);
      (es)->acting_pc = KosDynFrame_ACTING_PC(ft);
      goto done;
    }

    KosEvalState_FRAME(es) = ft;
    KosEvalState_BLOCK(es) = bl;
    KosEvalState_NAMESPACE(es) = ns;
    if (ft) {
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      (es)->pc = KosDynFrame_PC(ft);
      (es)->acting_pc = KosDynFrame_ACTING_PC(ft);
    } else {
      KosEvalState_CODE(es) = NULL;
      (es)->pc = 0;
      (es)->acting_pc = 0;
    }
  }

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_sync_from_context);
}


/**
   Snapshot es live frame, block, and namespace onto ctx before leaving RUN.

   Call from koskari_eval_cleanup after KosEvalState_FlushLiveRegsToFrame.
   Copies authoritative es fields into ctx derived snapshots for the next eval
   entry; does not modify pc slots on heap frames beyond the prior flush.
*/
void KosEvalState_FreezeDerivedToContext(KosEvalState *es) {

  KosContext *ctx;

  if (! es)
    return;
  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    return;

  KosContext_FRAME_TOP(ctx) = KosEvalState_FRAME(es);
  KosContext_BLOCK(ctx) = KosEvalState_BLOCK(es);
  KosContext_NAMESPACE(ctx) = KosEvalState_NAMESPACE(es);
}


KosDynNode *KosEvalState_InnermostStackOwner(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *n = KosContext_DYN_TOP(ctx);
  while (n) {
    if (KosDynNode_STACK(n))
      return n;
    n = KosDynNode_PARENT(n);
  }
  return NULL;
}


#define KosEvalState_GetConstant(es, index) \
  KosCode_GET_CONST(KosEvalState_CODE(es), index)


static KosStackBlock **kos_evalstate_parent_stack_slot(
    KosEvalState *es) {

  KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
  KosDynNode *node;
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
  KosStackBlock **slot = kos_evalstate_parent_stack_slot(es);
  if (unlikely(! slot))
    return -1;

  return KosStackBlock_PushRef(slot, val);
}


int KosEvalState_ParentStackPUSH(KosEvalState *es, PyObject *val) {
  KosStackBlock **slot = kos_evalstate_parent_stack_slot(es);
  if (unlikely(! slot)) {
    Py_XDECREF(val);
    return -1;
  }
  return KosStackBlock_PUSH(slot, val);
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
