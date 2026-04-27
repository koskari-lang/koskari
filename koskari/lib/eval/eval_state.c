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

   Derived-cache and stack-owner helpers for KosEvalState; included  into
   eval.c.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
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


/**
   Refresh FRAME_TOP and BLOCK after DYN_TOP / chain changes, then apply
   them to es without a full KosEvalState_SyncFromContext traversal.
*/
void KosEvalState_RefreshDynCaches(KosEvalState *es) {
    KosContext *ctx;
    KosDynNode *node;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  ctx = KosEvalState_CONTEXT(es);
  if (unlikely(! ctx))
    goto done;

  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;

  node = KosContext_DYN_TOP(ctx);
  while (node) {
    if (! bl && KosDynNode_STACK(node))
      bl = &KosDynNode_STACK(node);

    if (! ns && KosDynNode_NS(node))
      ns = KosDynNode_NS(node);

    if (! ft && KosDynNode_IS_FRAME(node)) {
      ft = (KosDynFrame *) node;
      SAYF("[gen] refresh_dyn_caches dyn_top=%p -> frame_top=%p (%s)\n",
           (void *) KosContext_DYN_TOP(ctx), (void *) node,
           kos_dynnode_kind_name(KosDynNode_KIND(node)));
    }

    if (ft && bl && ns)
      break;

    node = KosDynNode_PARENT(node);
  }

  KosContext_FRAME_TOP(ctx) = ft;
  KosContext_BLOCK(ctx) = bl;
  KosContext_NAMESPACE(ctx) = ns;

  /*
   It is normal for one or more of ft, bl, or ns to remain NULL after the walk
   (namespace-only extent, detached-gen topology, prompt-only head). Missing a
   FRAME when one exists deeper on the chain would be a bug in node linking,
   not something Refresh alone can assert without a full chain audit.
  */

  if (! ft)
    SAYF("[gen] refresh_dyn_caches dyn_top=%p -> frame_top=NULL (no frame)\n",
         (void *) KosContext_DYN_TOP(ctx));

  KosEvalState_ApplyDynCaches(es);

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_refresh_dyn_caches);
}


/**
   Push-only derived-cache patch; see eval.h. new_node must already be dyn_top.
*/
void KosEvalState_PatchDynCaches(
    KosEvalState *es,
    KosDynNode *new_node) {

  KosContext *ctx;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  ctx = KosEvalState_CONTEXT(es);
  if (unlikely(! es || ! ctx || ! new_node))
    goto done;

  if (KosDynNode_IS_FRAME(new_node)) {
    KosDynFrame *ft = (KosDynFrame *) new_node;

    KosContext_FRAME_TOP(ctx) = ft;
    KosContext_BLOCK(ctx) = &KosDynNode_STACK(new_node);
    KosContext_NAMESPACE(ctx) = KosDynNode_NS(new_node);

  } else {
    if (KosDynNode_NS(new_node))
      KosContext_NAMESPACE(ctx) = KosDynNode_NS(new_node);

    if (KosDynNode_STACK(new_node))
      KosContext_BLOCK(ctx) = &KosDynNode_STACK(new_node);
  }

  KosEvalState_ApplyDynCaches(es);

done:
#if KOS_DEBUG_PATCH_DYN_CACHES
  if (es && new_node) {
    KosContext *ctx_d = KosEvalState_CONTEXT(es);
    if (ctx_d) {
      KosDynFrame *ft_dbg = KosContext_FRAME_TOP(ctx_d);
      KosStackBlock **bl_dbg = KosContext_BLOCK(ctx_d);
      PyObject *ns_dbg = KosContext_NAMESPACE(ctx_d);

      KosEvalState_RefreshDynCaches(es);
      if (ft_dbg != KosContext_FRAME_TOP(ctx_d)
          || bl_dbg != KosContext_BLOCK(ctx_d)
          || ns_dbg != KosContext_NAMESPACE(ctx_d)) {
        PyErr_SetString(PyExc_SystemError,
                        "KosEvalState_PatchDynCaches: cache drift vs refresh");
      }
    }
  }
#endif
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_patch_dyn_caches);
}


void KosEvalState_ApplyDynCaches(KosEvalState *es) {

  KosContext *ctx;
  KosDynFrame *ft;
  KosStackBlock **bl;
  PyObject *ns;
  KosDynFrame *ef;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es)
    goto done;

  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    goto done;

  ft = KosContext_FRAME_TOP(ctx);
  bl = KosContext_BLOCK(ctx);
  ns = KosContext_NAMESPACE(ctx);
  ef = KosEvalState_FRAME(es);

  if (ef != ft) {
    if (ef)
      KosEvalState_FlushLiveRegsToFrame(es);
    KosEvalState_FRAME(es) = ft;
    KosEvalState_BLOCK(es) = bl;
    KosEvalState_NAMESPACE(es) = ns;
    if (ft) {
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      es->pc = KosDynFrame_PC(ft);
      es->acting_pc = KosDynFrame_ACTING_PC(ft);
    } else {
      KosEvalState_CODE(es) = NULL;
      es->pc = 0;
      es->acting_pc = 0;
    }
    goto done;
  }

  KosEvalState_BLOCK(es) = bl;
  KosEvalState_NAMESPACE(es) = ns;

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_apply_dyn_caches);
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


// The end.
