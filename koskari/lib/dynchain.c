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
   koskari/lib/dynchain.c

   Implements the unified dynamic chain: a single parent-linked stack of
   KosDynNode values (frames, prompts, regions, winds, continuations, etc.)
   replacing separate frame and handler chains. Transfer plans (see
   kos_build_transfer_plan, kos_run_transfer_step) move control for delimited
   and undelimited continuation invoke. Types and macros live in dynchain.h.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: GPT-5.2 via Cursor
*/


#include "lib.h"
#include "debug.h"
#include "tracing.h"
#include "hints.h"

#include <stdint.h>


/* === Forward declarations === */


static int kos_dyn_uninstall_handler(KosEvalState *es, KosDynNode *node,
                                     KosXferEvent event, KosEvent *out_ev);
int kos_dyn_push(KosEvalState *es, KosDynNode *node);
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node);
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top);

static void kos_dyn_apply_live(
    KosEvalState *es,
    KosDynFrame *ft,
    KosStackBlock **bl,
    PyObject *ns);
static void kos_dyn_walk_live_from(
    KosEvalState *es,
    KosDynNode *start,
    KosDynFrame **ft_out,
    KosStackBlock ***bl_out,
    PyObject **ns_out);
static void kos_dyn_retarget_after_push(
    KosEvalState *es,
    KosDynNode *new_node);
static void kos_dyn_retarget_after_pop(
    KosEvalState *es,
    KosDynNode *removed,
    KosDynNode *new_top);


/* === Debug helper functions === */


static const char *kos_dynnode_kind_names[] = {
  "FRAME",
  "PROMPT",
  "DELIM",
  "WIND",
  "TRY",
  "FINALLY",
  "LOOP",
  "FOREACH",
  "NS",
  "COLLECT",
  "CC",
  "YIELD_FROM",
  "PARAM",
  "WITH",
  "SHIFT",
  "CC_RAISE",
  "GEN",
  "NEXT",
  "UNKNOWN",
};


/**
   String name for a KosDynKind (tracing, diagnostics). Unknown kinds map to
   UNKNOWN. Callers: debug output, SAYF traces.
*/
const char *kos_dynnode_kind_name(KosDynKind kind) {
  if (kind < 0 || kind >= KOS_DYN_UNKNOWN) {
    return "UNKNOWN";
  }
  return kos_dynnode_kind_names[kind];
}


/* === DynNode base operations === */


/**
   Initialize base KosDynNode fields (parent, kind, flags, caps). Per-kind
   constructors call this before filling the union payload.
*/
static void kos_dynnode_init(
    KosDynNode *node,
    KosDynKind kind,
    KosDynNode *parent,
    uint32_t caps) {

  KosDynNode_PARENT(node) = parent;
  KosDynNode_KIND(node) = kind;
  KosDynNode_FLAGS(node) = KOS_DYNF_NONE;
  KosDynNode_CAPS(node) = caps;
  KosDynNode_STACK(node) = NULL;
  KosDynNode_NS(node) = NULL;
}


#define DYN_NODE_MAX_FREE 128  /* cap for cached KosDynNode; tunable */

static size_t dyn_freecount = 0;
static KosDynNode *dyn_freelist = NULL;
static uint32_t dyn_node_id_counter = 1;  /* 0 reserved for unset / no graft point */


/**
   Allocate and zero a KosDynNode. Prefer reuse from freelist; else PyMem_Malloc.
   Returns NULL on failure (sets PyErr_NoMemory). Caller must set kind and
   the relevant union member.
*/
static KosDynNode *kos_dynnode_alloc(void) {
  KosDynNode *p = dyn_freelist;

  if (p) {
    dyn_freelist = KosDynNode_PARENT(p);
    dyn_freecount--;
    KOS_TRACE_FL_ALLOC("dyn_node", 1);
    memset(p, 0, sizeof(KosDynNode));
    p->node_id = dyn_node_id_counter++;
    if (! dyn_node_id_counter)
      dyn_node_id_counter = 1;  /* avoid 0 on rollover */
    return p;
  }

  KOS_TRACE_FL_ALLOC("dyn_node", 0);
  p = PyMem_Malloc(sizeof(KosDynNode));
  if (! p) {
    PyErr_NoMemory();
    return NULL;
  }

  memset(p, 0, sizeof(KosDynNode));
  p->node_id = dyn_node_id_counter++;
  if (! dyn_node_id_counter)
    dyn_node_id_counter = 1;  /* avoid 0 on rollover */

  return p;
}


/**
   Return a node to the freelist or free it. Call only after kind-specific
   cleanup (refs released). Node is zeroed before being cached.
*/
static void kos_dynnode_return_to_freelist(KosDynNode *node) {
  if (KosDynNode_STACK(node)) {
    KosStackBlock_Release(KosDynNode_STACK(node));
    KosDynNode_STACK(node) = NULL;
  }

  if (dyn_freecount < DYN_NODE_MAX_FREE) {
    KOS_TRACE_FL_RETURN("dyn_node", 0);
    memset(node, 0, sizeof(KosDynNode));
    KosDynNode_PARENT(node) = dyn_freelist;
    dyn_freelist = node;
    dyn_freecount++;

  } else {
    KOS_TRACE_FL_RETURN("dyn_node", 1);
    PyMem_Free(node);
  }
}


static int snapshot_set_item(PyObject *dict, const char *key, PyObject *value) {

  if (PyDict_SetItemString(dict, key, value) < 0) {
    Py_DECREF(value);
    return -1;
  }
  Py_DECREF(value);
  return 0;
}


static int snapshot_set_uint(PyObject *dict, const char *key, uint32_t value) {

  return snapshot_set_item(dict, key, PyLong_FromUnsignedLong((unsigned long) value));
}


static int snapshot_set_int(PyObject *dict, const char *key, int value) {

  return snapshot_set_item(dict, key, PyLong_FromLong((long) value));
}


static int snapshot_set_size(PyObject *dict, const char *key, size_t value) {

  return snapshot_set_item(dict, key, PyLong_FromSize_t(value));
}


static int snapshot_set_obj(PyObject *dict, const char *key, PyObject *obj) {

  if (! obj) {
    Py_INCREF(Py_None);
    return snapshot_set_item(dict, key, Py_None);
  }
  Py_INCREF(obj);
  return snapshot_set_item(dict, key, obj);
}


static PyObject *snapshot_stack_to_list(KosStackBlock *stack) {

  size_t total = 0;
  for (KosStackBlock *b = stack; b; b = KosStackBlock_PARENT(b))
    total += KosStackBlock_LEN(b);

  if (total > (size_t) PY_SSIZE_T_MAX) {
    PyErr_SetString(PyExc_OverflowError,
                    "stack snapshot too large for Python list");
    return NULL;
  }

  PyObject *out = PyList_New((Py_ssize_t) total);
  if (! out)
    return NULL;

  Py_ssize_t idx = 0;
  for (KosStackBlock *b = stack; b; b = KosStackBlock_PARENT(b)) {
    size_t len = KosStackBlock_LEN(b);
    for (size_t i = 0; i < len; i++) {
      PyObject *item = b->objs[i];
      Py_INCREF(item);
      PyList_SET_ITEM(out, idx++, item);
    }
  }

  return out;
}


#include "dynchain/dyn_frame.c"
#include "dynchain/dyn_prompt.c"
#include "dynchain/dyn_delim.c"
#include "dynchain/dyn_cc.c"
#include "dynchain/dyn_shift.c"
#include "dynchain/dyn_cc_raise.c"
#include "dynchain/dyn_gen.c"
#include "dynchain/dyn_wind.c"
#include "dynchain/dyn_try.c"
#include "dynchain/dyn_finally.c"
#include "dynchain/dyn_loop.c"
#include "dynchain/dyn_foreach.c"
#include "dynchain/dyn_next.c"
#include "dynchain/dyn_ns.c"
#include "dynchain/dyn_collect.c"
#include "dynchain/dyn_yield_from.c"
#include "dynchain/dyn_param.c"
#include "dynchain/dyn_with.c"


/**
   Dispatch post-await resume: WITH phases, WIND phases. Callers: eval after
   KosEvalState_CallArgs returns.
*/
int kos_dynnode_on_control_returned(KosEvalState *es, KosDynNode *node, KosEvent *out_ev) {
  /* Iterator-boundary yield/return uses eval dispatch_boundary_event /
     kos_dyn_deliver_* instead of this hook. */
  (void) out_ev;
  /* When node is a frame, check if parent is WITH in pending phase. */
  if (KosDynNode_IS_FRAME(node)) {
    KosDynNode *parent = KosDynNode_PARENT(node);
    if (parent && KosDynNode_IS_WITH(parent)) {
      uint8_t phase = KosDynWith_PHASE((KosDynWith *) parent);
      if (phase == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        return kos_dyn_with_on_control_returned(es, (KosDynWith *) parent, 1);
      if (phase == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
        return kos_dyn_with_on_enter_control_returned(es, (KosDynWith *) parent);
    }
  }
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return kos_dyn_wind_on_control_returned(es, (KosDynWind *) node);
    case KOS_DYN_WITH:
      if (KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        return kos_dyn_with_on_control_returned(es, (KosDynWith *) node, 1);
      return 0;
    default:
      return 0;
  }
}


/**
   Pop a region node without going through normal opcode POP_* when a push
   must be rolled back (e.g. setup failure). Dispatches to the matching
   kos_dyn_pop_* for TRY, FINALLY, LOOP, FOREACH only.
*/
int kos_dyn_abort_push_region(KosEvalState *es, KosDynNode *region) {

  switch (KosDynNode_KIND(region)) {
    case KOS_DYN_TRY:
      return kos_dyn_pop_try(es, (KosDynTry *) region);
    case KOS_DYN_FINALLY:
      return kos_dyn_pop_finally(es, (KosDynFinally *) region);
    case KOS_DYN_LOOP:
      return kos_dyn_pop_loop(es, (KosDynLoop *) region);
    case KOS_DYN_FOREACH:
      return kos_dyn_pop_foreach(es, (KosDynForeach *) region);
    default:
      return -1;
  }
}


/* === Generic pending-exit APIs === */


/**
   Walk ancestors from node for a WITH awaiting async __exit__.
*/
static KosDynWith *find_pending_exit_with(KosDynNode *node) {
  for (; node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_WITH(node)
        && KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
      return (KosDynWith *) node;
  }
  return NULL;
}


/**
   Exposed form of find_pending_exit_with; returns the node or NULL.
   Callers: eval when resuming after await inside __exit__.
*/
KosDynNode *kos_dyn_find_pending_exit(KosDynNode *node) {
  KosDynWith *w = find_pending_exit_with(node);
  return w ? (KosDynNode *) w : NULL;
}


/**
   Resume control after async WITH exit completes: runs
   kos_dyn_with_on_control_returned for the pending node. Callers: eval loop
   when finishing a suspended unwind.
*/
int kos_dyn_complete_pending_exit(KosEvalState *es, int do_pop_node) {
  KosDynNode *n = kos_dyn_find_pending_exit(KosEvalState_DYN_TOP(es));
  if (! n)
    return 0;
  return kos_dyn_with_on_control_returned(es, (KosDynWith *) n, do_pop_node);
}


/* === Generic DynNode operations === */


/**
   Release kind-specific payload and PyObject refs, then return the struct to
   the freelist or PyMem_Free. Does not walk parent links. Callers:
   kos_dyn_uninstall, kos_dynchain_free, error paths after failed push.
*/
void kos_dynnode_free(KosDynNode *node) {
  if (! node)
    return;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME:
      kos_dynframe_free((KosDynFrame *) node);
      break;
    case KOS_DYN_PROMPT:
      kos_dynprompt_free((KosDynPrompt *) node);
      break;
    case KOS_DYN_DELIM:
      kos_dyndelim_free((KosDynDelim *) node);
      break;
    case KOS_DYN_WIND:
      kos_dynwind_free((KosDynWind *) node);
      break;
    case KOS_DYN_TRY:
      kos_dyntry_free((KosDynTry *) node);
      break;
    case KOS_DYN_FINALLY:
      kos_dynfinally_free((KosDynFinally *) node);
      break;
    case KOS_DYN_LOOP:
      kos_dynloop_free((KosDynLoop *) node);
      break;
    case KOS_DYN_FOREACH:
      kos_dynforeach_free((KosDynForeach *) node);
      break;
    case KOS_DYN_NS:
      kos_dynns_free((KosDynNs *) node);
      break;
    case KOS_DYN_CC:
      kos_dyncc_free((KosDynCC *) node);
      break;
    case KOS_DYN_YIELD_FROM:
      kos_dynyieldfrom_free((KosDynYieldFrom *) node);
      break;
    case KOS_DYN_PARAM:
      kos_dynparam_free((KosDynParam *) node);
      break;
    case KOS_DYN_WITH:
      kos_dynwith_free((KosDynWith *) node);
      break;
    case KOS_DYN_SHIFT:
      kos_dynshift_free((KosDynShift *) node);
      break;
    case KOS_DYN_CC_RAISE:
      kos_dyn_cc_raise_free((KosDynCCRaise *) node);
      break;
    case KOS_DYN_GEN:
      kos_dyngen_release((KosDynGen *) node);
      break;
    case KOS_DYN_NEXT:
      kos_dyn_next_free_payload(node);
      break;
    case KOS_DYN_COLLECT:
      kos_dyncollect_free((KosDynCollect *) node);
      break;
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dynnode_free: unknown node kind %u",
                   (unsigned) KosDynNode_KIND(node));
      break;
  }
}


/**
   Thin wrapper around kos_dyn_clone for symmetry with kos_dynnode_free.
*/
KosDynNode *kos_dynnode_clone(KosDynNode *node) {
  return kos_dyn_clone(node);
}


/**
   Free nodes from node toward the root, stopping before stop (stop is not
   freed). Used to tear down a suffix of chain or a cloned template.
*/
void kos_dynchain_free(KosDynNode *node, KosDynNode *stop) {

  while (node && node != stop) {
    KosDynNode *parent = node->parent;
    kos_dynnode_free(node);
    node = parent;
  }
}


/**
   Deep-clone a chain suffix from top down to (but not including) stop, with
   the clone's innermost parent set to stop. Used for continuation capture and
   INSTALL/PUSH segments. On failure, frees partial clone.
*/
KosDynNode *kos_clone_chain_segment(KosDynNode *top, KosDynNode *stop) {
  SAYF("[xfer] clone_segment enter top=%p stop=%p", (void *) top, (void *) stop);
  if (! top || top == stop)
    return NULL;

  /* Inner-to-outer clone; link cloned_prev->parent = next older clone */
  KosDynNode *cloned_top = NULL;
  KosDynNode *cloned_prev = NULL;

  KosDynNode *node = top;
  while (node && node != stop) {
    if (KOS_XFER_TRACE)
      SAYF("[xfer] clone_segment cloning node %p kind=%u",
           (void *) node, (unsigned) KosDynNode_KIND(node));
    KosDynNode *cloned = kos_dynnode_clone(node);
    if (! cloned) {
      SAYF("[xfer] clone_segment kos_dynnode_clone failed node=%p kind=%u pyerr=%d",
           (void *) node, (unsigned) KosDynNode_KIND(node), PyErr_Occurred() ? 1 : 0);
      /* Clone failed, free what we've built so far */
      kos_dynchain_free(cloned_top, NULL);
      return NULL;
    }
    if (cloned_prev) {
      cloned_prev->parent = cloned;
    } else {
      cloned_top = cloned;
    }

    cloned_prev = cloned;
    node = KosDynNode_PARENT(node);
  }

  /* Set the bottom's parent to stop (or NULL if stop is NULL) */
  if (cloned_prev) {
    cloned_prev->parent = stop;
  }

  return cloned_top;
}


/* === live eval state retarget (KosEvalState authoritative during RUN) === */


/**
   Write frame, operand-stack block pointer, and namespace onto KosEvalState.

   Call only from kos_dyn_retarget_after_push, kos_dyn_retarget_after_pop, or
   kos_dyn_rebuild_live_from_chain after dyn_top is already correct. When the
   frame pointer changes, flushes live pc and acting_pc onto the outgoing frame
   before loading code and PCs from the new frame. When only block or namespace
   change, updates those slots without clobbering live PCs.
*/
static void kos_dyn_apply_live(
    KosEvalState *es,
    KosDynFrame *ft,
    KosStackBlock **bl,
    PyObject *ns) {

  KosDynFrame *ef;

  if (! es)
    return;

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
    return;
  }

  KosEvalState_BLOCK(es) = bl;
  KosEvalState_NAMESPACE(es) = ns;
}


/**
   Load live frame, code, and PCs from a heap frame without flushing or
   changing block/namespace. Call when block/ns were already retargeted.
*/
static void kos_dyn_load_live_regs_from_frame(KosEvalState *es, KosDynFrame *ft) {

  if (! es || ! ft)
    return;

  KosEvalState_FRAME(es) = ft;
  KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
  es->pc = KosDynFrame_PC(ft);
  es->acting_pc = KosDynFrame_ACTING_PC(ft);
}


/**
   Find innermost frame, stack slot, and namespace along the chain from start.

   Walks parent links from start toward older nodes. Stops when all three
   pointers are resolved or the chain ends. Any output pointer may be NULL to
   skip that result. Used by kos_dyn_retarget_after_pop when a popped node
   owned one or more live slots, and by the same walk logic as rebuild.
*/
static void kos_dyn_walk_live_from(
    KosEvalState *es,
    KosDynNode *start,
    KosDynFrame **ft_out,
    KosStackBlock ***bl_out,
    PyObject **ns_out) {

  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;
  KosDynNode *node;

  if (ft_out)
    *ft_out = NULL;
  if (bl_out)
    *bl_out = NULL;
  if (ns_out)
    *ns_out = NULL;

  for (node = start; node; node = KosDynNode_PARENT(node)) {
    if (! bl && KosDynNode_STACK(node))
      bl = &KosDynNode_STACK(node);

    if (! ns && KosDynNode_NS(node))
      ns = KosDynNode_NS(node);

    if (! ft && KosDynNode_IS_FRAME(node))
      ft = (KosDynFrame *) node;

    if (ft && bl && ns)
      break;
  }

  if (ft_out)
    *ft_out = ft;
  if (bl_out)
    *bl_out = bl;
  if (ns_out)
    *ns_out = ns;
}


/**
   O(1) live-field update after a single-node push onto dyn_top.

   Call from kos_dyn_push and kos_dyn_push_noinstall immediately after
   new_node is KosEvalState_DYN_TOP(es) (and kos_dyn_install has run for push).
   Frame nodes replace frame, code, block, and namespace; other nodes update
   only the slots they carry. Frame-boundary flush runs inside kos_dyn_apply_live
   when the frame pointer changes. Do not use after splices, gen install, or
   xfer INSTALL/PUSH; use kos_dyn_rebuild_live_from_chain instead.
*/
static void kos_dyn_retarget_after_push(
    KosEvalState *es,
    KosDynNode *new_node) {

  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es || ! new_node)
    goto done;

  if (KosDynNode_IS_FRAME(new_node)) {
    ft = (KosDynFrame *) new_node;
    bl = &KosDynNode_STACK(new_node);
    ns = KosDynNode_NS(new_node);
  } else {
    ft = KosEvalState_FRAME(es);
    if (KosDynNode_NS(new_node))
      ns = KosDynNode_NS(new_node);
    else
      ns = KosEvalState_NAMESPACE(es);
    if (KosDynNode_STACK(new_node))
      bl = &KosDynNode_STACK(new_node);
    else
      bl = KosEvalState_BLOCK(es);
  }

  kos_dyn_apply_live(es, ft, bl, ns);

#if KOS_DEBUG_PATCH_DYN_CACHES
  {
    KosDynFrame *ft_dbg = KosEvalState_FRAME(es);
    KosStackBlock **bl_dbg = KosEvalState_BLOCK(es);
    PyObject *ns_dbg = KosEvalState_NAMESPACE(es);

    kos_dyn_rebuild_live_from_chain(es);
    if (ft_dbg != KosEvalState_FRAME(es)
        || bl_dbg != KosEvalState_BLOCK(es)
        || ns_dbg != KosEvalState_NAMESPACE(es)) {
      PyErr_SetString(PyExc_SystemError,
                      "kos_dyn_retarget_after_push: live drift vs rebuild");
    }
  }
#endif

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_retarget_after_push);
}


/**
   Live-field update after dyn_top changes to new_top and removed is unlinked.

   Call from kos_dyn_commit_dyn_top only. Caller must pass the previous
   dyn_top as removed and the new head as new_top. Frame-boundary flush runs
   in kos_dyn_apply_live when the removed node owned es->frame. When new_top
   segment replace), delegates to kos_dyn_rebuild_live_from_chain. For a normal
   single-head pop, updates only the live slots the removed node owned; returns
   without work when the pop did not own frame, block, or namespace on es.
*/
static void kos_dyn_retarget_after_pop(
    KosEvalState *es,
    KosDynNode *removed,
    KosDynNode *new_top) {

  int need_frame = 0;
  int need_block = 0;
  int need_ns = 0;
  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;

  if (! es)
    return;

  if (! removed || new_top != KosDynNode_PARENT(removed)) {
    kos_dyn_rebuild_live_from_chain(es);
    return;
  }

  if (KosEvalState_FRAME(es) == (KosDynFrame *) removed)
    need_frame = 1;

  if (KosDynNode_STACK(removed)
      && KosEvalState_BLOCK(es) == &KosDynNode_STACK(removed))
    need_block = 1;

  if (KosDynNode_NS(removed)
      && KosEvalState_NAMESPACE(es) == KosDynNode_NS(removed))
    need_ns = 1;

  if (! need_frame && ! need_block && ! need_ns)
    return;

  if (need_block && ! need_frame && ! need_ns
      && new_top && KosDynNode_STACK(new_top)) {
    kos_dyn_apply_live(es, KosEvalState_FRAME(es), &KosDynNode_STACK(new_top),
                       KosEvalState_NAMESPACE(es));
    return;
  }

  kos_dyn_walk_live_from(es, new_top, &ft, &bl, &ns);

  if (! need_frame)
    ft = KosEvalState_FRAME(es);
  if (! need_block)
    bl = KosEvalState_BLOCK(es);
  if (! need_ns)
    ns = KosEvalState_NAMESPACE(es);

  kos_dyn_apply_live(es, ft, bl, ns);
}


/**
   Full chain walk: recompute live frame, block, and namespace on es from dyn_top.

   Use when incremental push or pop retarget does not apply: transfer plan
   INSTALL or PUSH, kos_dyn_gen_install, KosContext_New bootstrap, tail koscall
   frame replacement, opcode paths that assign a stack block after push, or any
   time dyn_top was published without new_top == parent(removed). Walks from
   ctx->dyn_top toward parents, then kos_dyn_apply_live on es.
   Prefer kos_dyn_retarget_after_push or kos_dyn_retarget_after_pop inside
   kos_dyn_push and kos_dyn_commit_dyn_top when the edit is a single-head push
   or parent pop.
*/
void kos_dyn_rebuild_live_from_chain(KosEvalState *es) {

  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;
  KosDynNode *node;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es)
    goto done;

  node = KosEvalState_DYN_TOP(es);
  while (node) {
    if (! bl && KosDynNode_STACK(node))
      bl = &KosDynNode_STACK(node);

    if (! ns && KosDynNode_NS(node))
      ns = KosDynNode_NS(node);

    if (! ft && KosDynNode_IS_FRAME(node)) {
      ft = (KosDynFrame *) node;
      SAYF("[gen] rebuild_live_from_chain dyn_top=%p -> frame_top=%p (%s)\n",
           (void *) KosEvalState_DYN_TOP(es), (void *) node,
           kos_dynnode_kind_name(KosDynNode_KIND(node)));
    }

    if (ft && bl && ns)
      break;

    node = KosDynNode_PARENT(node);
  }

  if (! ft)
    SAYF("[gen] rebuild_live_from_chain dyn_top=%p -> frame_top=NULL (no frame)\n",
         (void *) KosEvalState_DYN_TOP(es));

  kos_dyn_apply_live(es, ft, bl, ns);

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_rebuild_live_from_chain);
}


/**
   Link node as DYN_TOP, run install, retarget live fields.

   kos_dyn_retarget_after_push updates es; frame-boundary flush runs in
   kos_dyn_apply_live when the pushed node is a frame.
*/
int kos_dyn_push(KosEvalState *es, KosDynNode *node) {
  // CONTEXT: dirty, touches context directly
  KosDynNode *prev;

  if (! node)
    return -1;

  prev = KosEvalState_DYN_TOP(es);
  KosEvalState_DYN_TOP(es) = node;

  if (kos_dyn_install(es, node, NULL) < 0) {
    KosEvalState_DYN_TOP(es) = prev;
    return -1;
  }
  kos_dyn_retarget_after_push(es, node);
  return 0;
}


/**
   Flush live PCs, set dyn_top to new_top, retarget live fields on es.

   Central commit for pops and any dyn_top publish that is not a push. Captures
   the old dyn_top as removed and calls kos_dyn_retarget_after_pop. Use for
   kos_dyn_uninstall, fast kos_dyn_pop_* paths, gen install, and manual splices.
   Do not use for kos_dyn_push; use kos_dyn_retarget_after_push via push instead.
*/
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top) {

  KosDynNode *removed;

  removed = KosEvalState_DYN_TOP(es);
  KosEvalState_DYN_TOP(es) = new_top;
  kos_dyn_retarget_after_pop(es, removed, new_top);
}


/**
   Like kos_dyn_push but skips kos_dyn_install (region nodes, etc.).
*/
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node) {

  if (! node)
    return -1;

  KosEvalState_DYN_TOP(es) = node;
  kos_dyn_retarget_after_push(es, node);
  return 0;
}


static int kos_dyn_uninstall_core(
    KosEvalState *es,
    KosDynNode *node,
    KosXferEvent event,
    KosEvent *out_ev) {

  int r;

  r = kos_dyn_uninstall_handler(es, node, event, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;

  kos_dyn_commit_dyn_top(es, KosDynNode_PARENT(node));
  KosDynNode_PARENT(node) = NULL;
  kos_dynnode_free(node);
  return 0;
}


/**
   Pop dyn_top: run uninstall handler (teardown, winds), unlink, free node,
   refresh FRAME_TOP. Must be the current top. Callers: kos_dyn_pop_top,
   xfer_step_pop_one, explicit unwinds.
*/
int kos_dyn_uninstall(KosEvalState *es, KosDynNode *node, KosXferEvent event,
                      KosEvent *out_ev) {
  if (! node || KosEvalState_DYN_TOP(es) != node) {
    PyErr_SetString(PyExc_RuntimeError, "kos_dyn_uninstall: node is not dyn_top");
    return -1;
  }

  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return -1;
  }

  return kos_dyn_uninstall_core(es, node, event, out_ev);
}


/**
   Clone node then kos_dyn_push the clone. Used when the source node is shared
   or must remain immutable on the template chain.
*/
int kos_dyn_push_clone(KosEvalState *es, KosDynNode *node) {
  if (! node) {
    return -1;
  }

  KosDynNode *clone = kos_dyn_clone(node);
  if (! clone) {
    return -1;
  }

  KosDynNode_PARENT(clone) = KosEvalState_DYN_TOP(es);
  return kos_dyn_push(es, clone);
}


/* === Transfer path wind/unwind helpers === */


/**
   Run INSTALL-phase wind (enter) for a node with NEEDS_INSTALL_WIND.
   Used by xfer_step_install and xfer_step_push.
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dynnode_run_wind(KosEvalState *es, KosDynNode *node,
                                KosEvent *out_ev) {
  return kos_dyn_wind(es, node, out_ev);
}


/**
   Run transfer-plan unwind for a node with NEEDS_UNWIND_ACTION.
   Uses kos_dyn_unwind; teardown (drop block, pop) for nodes with
   NEEDS_TRANSFER_TEARDOWN. Used by kos_run_transfer_step for EXTENT_ACTION.
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dynnode_run_transfer_unwind(KosEvalState *es, KosDynNode *node,
                                           KosXferEvent event,
                                           KosEvent *out_ev) {
  int r = kos_dyn_unwind(es, node, event, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;
  return 0;
}


/* === Generic dispatch: switch-based per-kind handlers === */


/**
   Run install-phase setup for a node that was just pushed.  Types with
   no install action return 0 immediately via the default case.

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
int kos_dyn_install(KosEvalState *es, KosDynNode *node, KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  int r;
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT: {
      /* Operand stack stays on the frame below; PROMPT is a barrier only.
         SHIFT inherits the same stack via KosDynNode_STACK == NULL. */
      (void) out_ev;
      (void) node;
      r = 0;
      break;
    }
    case KOS_DYN_DELIM:
      r = 0;
      break;
    case KOS_DYN_PARAM:
      r = kos_dynparam_enter(ctx, (KosDynParam *) node);
      break;
    case KOS_DYN_WITH:
      /* Push = link + local setup only.  Wind (enter) is invoked explicitly
         by opcodes or transfer plan after push, never in install. */
      r = 0;
      break;
    case KOS_DYN_COLLECT:
      /* PUSH_COLLECT handler installs collector as frame ns before pushing
         the node; no install action needed. */
      r = 0;
      break;
    case KOS_DYN_GEN:
      r = 0;
      break;
    case KOS_DYN_NEXT:
      r = 0;
      break;
    default:
      r = 0;
      break;
  }
  return r;
}


/**
   Run the pop handler for a node being popped off the chain.  Types with
   no pop action return 0 immediately via the default case.

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dyn_uninstall_handler(KosEvalState *es, KosDynNode *node,
                                     KosXferEvent event, KosEvent *out_ev) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT: {
      if (KOS_XFER_TRACE) {
        size_t len_before = KosDynNode_STACK(node)
            ? KosStackBlock_LEN(KosDynNode_STACK(node)) : 0;
        SAYF("[xfer] prompt_pop node=%p: prompt stack len=%zu",
             (void *) node, len_before);
        (void) len_before;
      }
      return 0;
    }
    case KOS_DYN_DELIM:
      return 0;
    case KOS_DYN_WIND:
      return kos_dyn_unwind(es, node, event, out_ev);
    case KOS_DYN_TRY:
    case KOS_DYN_FINALLY:
    case KOS_DYN_LOOP:
    case KOS_DYN_FOREACH:
      (void) out_ev;
      return 0;
    case KOS_DYN_NS:
      (void) es;
      (void) out_ev;
      return 0;
    case KOS_DYN_COLLECT: {
      (void) es;
      (void) out_ev;
      return 0;
    }
    case KOS_DYN_PARAM:
      (void) out_ev;
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      return 0;
    case KOS_DYN_YIELD_FROM:
      (void) out_ev;
      kos_dyn_yieldfrom_unwind(es, (KosDynYieldFrom *) node);
      return 0;
    case KOS_DYN_WITH: {
      return kos_dyn_unwind(es, node, event, out_ev);
    }
    default:
      (void) out_ev;
      return 0;
  }
}


/**
   Run wind (enter) for an extent node during continuation transfer.
   Only called for kinds that have wind semantics; returns error for
   others.

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
int kos_dyn_wind(KosEvalState *es, KosDynNode *node, KosEvent *out_ev) {
  int r;
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PARAM:
      r = kos_dyn_param_wind(es, (KosDynParam *) node);
      break;
    case KOS_DYN_WITH:
      r = kos_dyn_dyn_with_wind(es, (KosDynWith *) node, out_ev);
      break;
    case KOS_DYN_WIND:
      r = kos_dyn_wind_wind(es, (KosDynWind *) node);
      break;
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_wind: node kind %u has no wind",
                   (unsigned) KosDynNode_KIND(node));
      r = -1;
      break;
  }
  return r;
}


/**
   Run unwind (exit) for an extent node.  May start async for WITH/DYN_WITH.

   :param event: transfer event; nodes can distinguish RETURN, RAISE, etc.
   :param out_ev: event output; may be NULL for nodes that do not suspend
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
int kos_dyn_unwind(KosEvalState *es, KosDynNode *node, KosXferEvent event,
                   KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  int r;
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_DELIM:
      (void) out_ev;
      r = 0;
      break;
    case KOS_DYN_SHIFT:
      (void) out_ev;
      r = 0;
      break;
    case KOS_DYN_PARAM:
      (void) out_ev;
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      r = 0;
      break;
    case KOS_DYN_WIND:
      (void) out_ev;
      r = kos_dyn_wind_unwind(es, (KosDynWind *) node);
      break;
    case KOS_DYN_WITH:
      r = kos_dyn_with_unwind(es, (KosDynWith *) node, out_ev);
      break;
    case KOS_DYN_CC_RAISE:
      (void) out_ev;
      r = cc_raise_unwind(ctx, node, NULL);
      break;
    case KOS_DYN_GEN:
      (void) out_ev;
      r = 0;
      break;
    case KOS_DYN_NEXT:
      (void) out_ev;
      r = 0;
      break;
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_unwind: node kind %u has no unwind",
                   (unsigned) KosDynNode_KIND(node));
      r = -1;
      break;
  }
  return r;
}


/**
   Offer a control transit to dyn_top without unlinking: PASS, CONSUMED,
   DEFERRED, or ERROR. Central dispatch for exception/return/yield/shift
   semantics (TRY/FINALLY except/finally PCs, GEN boundary, PROMPT shift).
   Callers: kos_dyn_transition walks, kos_dyn_try_local_raise_body,
   kos_dyn_walk_handle_yield, unwind_to_frame, shift walk.

   TRY: CONSUMED when RAISE, enabled, PyErr set, and extent has except_pc.
   FINALLY: CONSUMED when RAISE or RETURN and finally enabled.
*/
KosHandleResult kos_dyn_handle(KosEvalState *es, KosDynNode *node,
                               KosXferEvent event, KosEvent *out_ev) {

  (void) out_ev;

  KOS_TRACE_EVENTS_INC(KOS_TE_DYN_HANDLE_ENTER);

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_TRY:
      if (event == KOS_XFER_EVENT_RAISE) {
        int en = KosDynTry_ENABLED((KosDynTry *) node);
        int py = PyErr_Occurred() ? 1 : 0;
        KosDynFrame *frame = KosEvalState_FRAME(es);
        if (! en || ! py || ! frame) {
          SAYF("[xfer] kos_dyn_handle TRY RAISE skip enabled=%d pyerr=%d frame=%p\n",
               en, py, (void *) frame);
          return KOS_HANDLE_PASS;
        }
        {
          KosTryHandler th;
          int kh = kos_try_handler_for((PyObject *) KosDynFrame_CODE(frame),
                                       KosEvalState_ACTING_PC(es), &th);
          #if VERBOSE_DEBUG
          unsigned exc_pc_u = (unsigned) KOS_EXTENT_NONE;
          if (kh == 0)
            exc_pc_u = (unsigned) th.except_pc;
          SAYF("[xfer] kos_dyn_handle TRY kos_try_handler_for code=%p "
               "acting_pc=%u ret=%d except_pc=%u\n",
               (void *) KosDynFrame_CODE(frame),
               (unsigned) KosEvalState_ACTING_PC(es),
               kh,
               exc_pc_u);
          #endif

          if (kh == 0 && th.except_pc != KOS_EXTENT_NONE)
            return KOS_HANDLE_CONSUMED;
        }
        SAY("[xfer] kos_dyn_handle TRY -> PASS (not consumed)\n");
      }
      return KOS_HANDLE_PASS;
    case KOS_DYN_FINALLY:
      if (event == KOS_XFER_EVENT_RAISE
          && KosDynFinally_ENABLED((KosDynFinally *) node))
        return KOS_HANDLE_CONSUMED;
      if (event == KOS_XFER_EVENT_RETURN
          && KosDynFinally_ENABLED((KosDynFinally *) node))
        return KOS_HANDLE_CONSUMED;
      return KOS_HANDLE_PASS;
    case KOS_DYN_GEN:
      if (event == KOS_XFER_EVENT_YIELD)
        return KOS_HANDLE_CONSUMED;
      if (event == KOS_XFER_EVENT_RETURN)
        return KOS_HANDLE_CONSUMED;
      if (event == KOS_XFER_EVENT_RAISE)
        return KOS_HANDLE_CONSUMED;
      if (event == KOS_XFER_EVENT_SHIFT) {
        PyErr_SetString(PyExc_RuntimeError,
                        "shift cannot cross generator boundary");
        return KOS_HANDLE_ERROR;
      }
      return KOS_HANDLE_PASS;
    case KOS_DYN_PROMPT:
      if (event == KOS_XFER_EVENT_SHIFT)
        return KOS_HANDLE_CONSUMED;
      return KOS_HANDLE_PASS;
    default:
      return KOS_HANDLE_PASS;
  }
}


/**
   Handler PC for a TRY or FINALLY region, merged with extent table lookup on
   the current frame. Callers: kos_dyn_try_local_raise_body when routing RAISE
   into except/finally.
*/
uint32_t kos_dynnode_raise_handler_pc_for_try_finally(
    KosEvalState *es,
    KosDynNode *region) {

  uint32_t handler_pc;
  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (KosDynNode_IS_TRY(region))
    handler_pc = KosDynTry_HANDLER_PC((KosDynTry *) region);
  else
    handler_pc = KosDynFinally_HANDLER_PC((KosDynFinally *) region);

  if (frame) {
    KosTryHandler th;
    if (kos_try_handler_for((PyObject *) KosDynFrame_CODE(frame),
                            KosEvalState_ACTING_PC(es), &th) == 0) {
      if (KosDynNode_IS_FINALLY(region) && th.finally_pc != KOS_EXTENT_NONE)
        handler_pc = th.finally_pc;
      else if (th.except_pc != KOS_EXTENT_NONE)
        handler_pc = th.except_pc;
    }
  }
  return handler_pc;
}


/**
   Resume PC for nodes that may swallow exceptions (CC, WITH). Used when a pop
   cleared PyErr and execution should continue at a catch label.
*/
uint32_t kos_dynnode_catch_resume_pc(KosDynNode *node) {

  if (! node || ! KosDynNode_HAS_CAP(node, KOS_DYN_CAP_CATCH))
    return (uint32_t) -1;
  if (KosDynNode_IS_CC(node))
    return KosDynCC_RESUME_PC((KosDynCC *) node);
  if (KosDynNode_IS_WITH(node))
    return KosDynWith_RESUME_PC((KosDynWith *) node);
  return (uint32_t) -1;
}


/**
   Detach the chain above GEN into the GEN node for generator resume, set
   ctx dyn_top to GEN's former parent, and wire asyncgen owner if any. Called
   when handle(YIELD) consumes at GEN during kos_dyn_walk_handle_yield.
*/
static int kos_dyn_gen_freeze_and_release(KosEvalState *es, KosDynGen *gen,
                                          KosEvent *out_ev) {
  KosDynNode *saved_parent;
  KosDynNode *ctx_active;
  KosDynNode *node;
  KosDynNode *n;
  PyObject *owner;

  SAYF("[gen] freeze ENTER ctx=%p gen=%p dyn_top=%p (%s) gen_parent=%p\n",
       (void *) KosEvalState_CONTEXT(es), (void *) gen,
       (void *) KosEvalState_DYN_TOP(es),
       KosEvalState_DYN_TOP(es)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_DYN_TOP(es)))
           : "NULL",
       (void *) KosDynNode_PARENT(gen));
  if (KosEvalState_DYN_TOP(es)) {
    SAY("[gen] freeze chain from dyn_top: ");
    for (n = KosEvalState_DYN_TOP(es); n; n = KosDynNode_PARENT(n))
      SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }
  if (! KosEvalState_DYN_TOP(es)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_freeze: no dyn_top");
    return -1;
  }
  for (node = KosEvalState_DYN_TOP(es); node; node = KosDynNode_PARENT(node)) {
    if (node == (KosDynNode *) gen)
      break;
  }
  if (! node) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_freeze: gen not in chain from dyn_top");
    return -1;
  }

  (void) out_ev;

  /* 1. Record GEN's parent, then remove the GEN->parent link */
  saved_parent = KosDynNode_PARENT(gen);
  KosDynNode_PARENT(gen) = NULL;

  /* 2. Remove ctx's active node and store it in GEN */
  ctx_active = KosEvalState_DYN_TOP(es);
  KosDynGen_ACTIVE(gen) = ctx_active;
  SAY("[gen] freeze storing in gen->active: chain ");
  for (n = ctx_active; n; n = KosDynNode_PARENT(n))
    SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
  SAY("(end)");

  /* 3. Put GEN's old parent onto ctx's active */
  KosEvalState_DYN_TOP(es) = saved_parent;
  kos_dyn_rebuild_live_from_chain(es);

  owner = KosDynGen_OWNER(gen);
  if (owner && KosPyAsyncGen_Check(owner))
    ((KosPyAsyncGen *) owner)->gen = (void *) gen;

  /* Release only when owner holds the gen; standalone gen is kept for resume */
  if (owner)
    kos_dyngen_release(gen);
  SAYF("[gen] freeze DONE ctx->dyn_top=%p (%s) frame_top=%p owner=%p gen=%p\n",
       (void *) KosEvalState_DYN_TOP(es),
       KosEvalState_DYN_TOP(es)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_DYN_TOP(es)))
           : "NULL",
       (void *) KosEvalState_FRAME(es),
       (void *) owner,
       (void *) gen);
  return 0;
}


/**
   Walk from dyn_top inward, handle(YIELD) until GEN consumes, then freeze.
   Callers: kos_dyn_transition KOS_TRANS_WALK_HANDLE_YIELD (eval YIELD path).
*/
int kos_dyn_walk_handle_yield(KosEvalState *es, KosEvent *out_ev) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  SAYF("[gen] walk_handle_yield ENTER dyn_top=%p\n", (void *) node);
  while (node) {
    KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_YIELD,
                                        out_ev);
    SAYF("[gen] walk_handle_yield node=%p kind=%s hr=%d\n",
         (void *) node, kos_dynnode_kind_name(KosDynNode_KIND(node)),
         (int) hr);
    if (hr == KOS_HANDLE_CONSUMED) {
      if (KosDynNode_IS_GEN(node)) {
        SAYF("[gen] walk_handle_yield -> freeze at gen %p\n", (void *) node);
        int fr = kos_dyn_gen_freeze_and_release(es, (KosDynGen *) node, out_ev);
        return fr;
      }
      SAY("[gen] walk_handle_yield -> consumed (non-GEN), return 0");
      return 0;
    }
    if (hr == KOS_HANDLE_ERROR) {
      return -1;
    }
    node = KosDynNode_PARENT(node);
  }
  PyErr_SetString(PyExc_RuntimeError,
                  "yield outside generator: no generator boundary on chain");
  return -1;
}


/**
   PROMPT-specific step when unwinding on exception: pop prompt (drop block)
   so RAISE can continue past the delimiter. Callers: try_local raise path.
*/
int kos_dyn_exception_unwind(
    KosEvalState *es,
    KosDynNode *node,
    KosEvent *out_ev) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT:
      (void) out_ev;
      if (KOS_XFER_TRACE)
        SAYF("[xfer] prompt_unwind_exception node=%p: unlinking prompt", (void *) node);
      if (kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_NONE, out_ev) < 0) {
        return -1;
      }
      /* Exception propagates past the prompt boundary; continue unwinding */
      return 0;
    case KOS_DYN_DELIM:
      (void) out_ev;
      return kos_dyn_pop_delim(es, (KosDynDelim *) node);
    default:
      return 0;
  }
}


/**
   Allocate a copy of node.  Returns NULL on error or if the node kind
   does not support cloning.
*/
KosDynNode *kos_dyn_clone(KosDynNode *node) {
  if (! node)
    return NULL;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME:
      return (KosDynNode *) kos_dynframe_clone((KosDynFrame *) node);
    case KOS_DYN_PROMPT:
      return (KosDynNode *) kos_dynprompt_clone((KosDynPrompt *) node);
    case KOS_DYN_DELIM:
      return (KosDynNode *) kos_dyndelim_clone((KosDynDelim *) node);
    case KOS_DYN_WIND:
      return (KosDynNode *) kos_dynwind_clone((KosDynWind *) node);
    case KOS_DYN_TRY:
      return (KosDynNode *) kos_dyntry_clone((KosDynTry *) node);
    case KOS_DYN_FINALLY:
      return (KosDynNode *) kos_dynfinally_clone((KosDynFinally *) node);
    case KOS_DYN_LOOP:
      return (KosDynNode *) kos_dynloop_clone((KosDynLoop *) node);
    case KOS_DYN_FOREACH:
      return (KosDynNode *) kos_dynforeach_clone((KosDynForeach *) node);
    case KOS_DYN_NS:
      return (KosDynNode *) kos_dynns_clone((KosDynNs *) node);
    case KOS_DYN_CC:
      return (KosDynNode *) kos_dyncc_clone((KosDynCC *) node);
    case KOS_DYN_YIELD_FROM:
      return (KosDynNode *) kos_dynyieldfrom_clone((KosDynYieldFrom *) node);
    case KOS_DYN_PARAM:
      return (KosDynNode *) kos_dynparam_clone((KosDynParam *) node);
    case KOS_DYN_WITH:
      return (KosDynNode *) kos_dyn_with_clone((KosDynWith *) node);
    case KOS_DYN_SHIFT:
      return (KosDynNode *) kos_dynshift_clone((KosDynShift *) node);
    case KOS_DYN_CC_RAISE:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot clone CC_RAISE sentinel node");
      return NULL;
    case KOS_DYN_GEN:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot clone generator boundary node");
      return NULL;
    case KOS_DYN_NEXT:
      return (KosDynNode *) kos_dyn_next_clone((KosDynNext *) node);
    case KOS_DYN_COLLECT:
      return (KosDynNode *) kos_dyncollect_clone((KosDynCollect *) node);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_clone: unknown node kind %u",
                   (unsigned) KosDynNode_KIND(node));
      return NULL;
  }
}


/**
   Pop the top node of the dynamic chain.  Uses unified kos_dyn_pop.
   BARRIER nodes (PROMPT) return -1.  Used for unwinding
   (e.g. exception handling) and normal scope exit.
*/
int kos_dyn_pop_nonbarrier_top(
    KosEvalState *es,
    KosXferEvent event,
    KosEvent *out_ev) {

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return -1;
  }

  if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return -1;
  }

  int r = kos_dyn_uninstall_core(es, top, event, out_ev);
  return r;
}


int kos_dyn_pop_top_allow_barrier(
    KosEvalState *es,
    KosXferEvent event,
    KosEvent *out_ev) {

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return -1;
  }

  int r = kos_dyn_uninstall_core(es, top, event, out_ev);
  return r;
}


int kos_dyn_pop_top(KosEvalState *es, KosXferEvent event, KosEvent *out_ev) {

  return kos_dyn_pop_nonbarrier_top(es, event, out_ev);
}


/**
   Pop dyn_top during an unwind walk toward a frame or node.  PROMPT
   nodes use kos_dyn_pop_prompt; others use kos_dyn_pop_top.  Unlike
   kos_dyn_pop_top alone, barriers are popped here because these walks
   deliberately unwind through delimited prompt boundaries.

   :return: same as kos_dyn_pop_top, or 0 after popping a prompt
*/
static int kos_dyn_pop_one_unwind_walk(
    KosEvalState *es,
    KosXferEvent event,
    KosEvent *out_ev) {

  KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_pop_one_unwind_walk: empty dynamic chain");
    return -1;
  }

  return kos_dyn_pop_top_allow_barrier(es, event, out_ev);
}


/**
   Pop until dyn_top == target or empty. PROMPT uses kos_dyn_pop_prompt so
   walks can cross delimiters. Callers: transfer helpers, targeted unwind.
*/
int kos_dyn_unwind_to_node(KosEvalState *es, KosDynNode *target,
                           KosXferEvent event, KosEvent *out_ev) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: target=%p kind=%s",
         (void *) target, target ? kos_dynnode_kind_name(KosDynNode_KIND(target)) : "NULL");

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != target) {
#if KOS_XFER_TRACE
    {
      KosDynNode *trace_top = KosEvalState_DYN_TOP(es);
      SAYF("[xfer] unwind_to_node: popping node=%p kind=%s",
           (void *) trace_top,
           kos_dynnode_kind_name(KosDynNode_KIND(trace_top)));
    }
#endif
    int pop_res = kos_dyn_pop_one_unwind_walk(es, event, out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
      return pop_res;
    }
    if (pop_res != 0) {
      return -1;
    }
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return 0;
}


/**
   Unwind toward frame, offering RETURN to each top (FINALLY can consume and
   branch). Sets async unwind target frame when WITH exits suspend. Callers:
   kos_dyn_transition WALK_RETURN_TO_FRAME, RETURN opcode plumbing.
*/
int kos_dyn_walk_return_to_frame(KosEvalState *es, KosDynFrame *frame,
                                  PyObject *val, KosEvent *out_ev) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] walk_return_to_frame: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    /* Handle phase: offer RETURN before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RETURN,
                                          out_ev);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top)) {
          /* Deliver to finally block: clear stack, push value, set PC */
          KosDynFinally *fin = (KosDynFinally *) top;
          {
            if (KosEvalState_StackClearBlock(es) < 0) {
              Py_DECREF(val);
              return -1;
            }
            if (KosEvalState_StackPushRef(es, val) < 0) {
              Py_DECREF(val);
              return -1;
            }
          }
          Py_DECREF(val);
          if (KosEvalState_FRAME(es)) {
            KosEvalState_SET_PC(es, KosDynFinally_HANDLER_PC(fin));
          }
          KosDynFinally_ENABLED(fin) = 0;
          KosDynFinally_HANDLING_RETURN(fin) = 1;
          if (KOS_XFER_TRACE)
            SAYF("[xfer] walk_return: delivered to FINALLY, pc=%u",
                 (unsigned) KosDynFinally_HANDLER_PC(fin));
          return KOS_DYN_UNWIND_FINALLY_TAKING_RETURN;
        }
        /* Other CONSUMED (e.g. GEN) not expected for RETURN walk */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during return walk: unexpected node");
        Py_DECREF(val);
        return -1;
      }
      if (hr == KOS_HANDLE_ERROR) {
        Py_DECREF(val);
        return -1;
      }
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] walk_return: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    int pop_res = kos_dyn_pop_one_unwind_walk(es, KOS_XFER_EVENT_RETURN,
                                             out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosEvalState_SetUnwindTargetFrame(es, frame);
      Py_DECREF(val);
      return pop_res;
    }
    if (pop_res != 0) {
      Py_DECREF(val);
      return -1;
    }
  }
  KosEvalState_SetUnwindTargetFrame(es, NULL);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] walk_return: done, dyn_top=%p",
         (void *) KosEvalState_DYN_TOP(es));
  return 0;
}


/**
   Pop toward frame with handle(event) before each pop. CONSUMED for non-
   FINALLY is currently an error (see message). Callers: kos_dyn_transition
   UNWIND_TO_FRAME.
*/
int kos_dyn_unwind_to_frame(KosEvalState *es, KosDynFrame *frame,
                            KosXferEvent event, KosEvent *out_ev) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    /* Handle phase: offer event before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, event, out_ev);
      if (hr == KOS_HANDLE_CONSUMED) {
        /* RETURN: use kos_dyn_walk_return_to_frame. RAISE: use try_local. */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during unwind_to_frame not implemented");
        return -1;
      }
      if (hr == KOS_HANDLE_ERROR) {
        return -1;
      }
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_frame: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    int pop_res = kos_dyn_pop_one_unwind_walk(es, event, out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosEvalState_SetUnwindTargetFrame(es, frame);
      return pop_res;
    }
    if (pop_res != 0) {
      return -1;
    }
  }
  KosEvalState_SetUnwindTargetFrame(es, NULL);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return 0;
}


/**
   Nearest enabled TRY or FINALLY from dyn_top for local exception dispatch.
   Callers: kos_dyn_try_local_raise_body, opcode RAISE routing.
*/
KosDynNode *kos_find_node_for_raise(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);
  while (node) {
    if ((KosDynNode_IS_TRY(node) && KosDynTry_ENABLED((KosDynTry *) node)) ||
        (KosDynNode_IS_FINALLY(node) && KosDynFinally_ENABLED((KosDynFinally *) node)))
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   Nearest LOOP or FOREACH from dyn_top. Callers: BREAK, CONTINUE opcodes.
*/
KosDynNode *kos_find_node_for_loop(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);
  while (node) {
    if (KosDynNode_IS_LOOP(node) || KosDynNode_IS_FOREACH(node))
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   Implement break/continue: find loop region in current frame, unwind to it,
   set PC and stack per event. Callers: kos_dyn_transition TRANSFER_TO_LOOP.
*/
int kos_dyn_transfer_to_loop(KosEvalState *es, KosXferEvent event,
                             PyObject *result, KosEvent *out_ev) {

  KosDynNode *region = kos_find_node_for_loop(es);
  if (! region) {
    PyErr_SetString(PyExc_RuntimeError,
                    (event == KOS_XFER_EVENT_BREAK)
                        ? "break outside loop"
                        : "continue outside loop");
    return -1;
  }

  /* Same-frame check: loop must be in current frame (frame is ancestor) */
  {
    KosDynFrame *frame = KosEvalState_FRAME(es);
    int in_frame = 0;
    for (KosDynNode *n = region; n; n = KosDynNode_PARENT(n)) {
      if (n == (KosDynNode *) frame) {
        in_frame = 1;
        break;
      }
    }
    if (! frame || ! in_frame) {
      PyErr_SetString(PyExc_RuntimeError,
                      (event == KOS_XFER_EVENT_BREAK)
                          ? "break outside loop"
                          : "continue outside loop");
      return -1;
    }
  }

  /* Walk from DYN_TOP toward region; unwind each node with event */
  while (KosEvalState_DYN_TOP(es) != (KosDynNode *) region) {
    int pop_res = kos_dyn_pop_nonbarrier_top(es, event, out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosEvalState_SetUnwindTargetFrame(es, KosEvalState_FRAME(es));
      return KOS_DYN_UNWIND_ASYNC_STARTED;
    }
    if (pop_res < 0) {
      return -1;
    }
  }

  if (event == KOS_XFER_EVENT_CONTINUE) {
    /* Update region result with continue value */
    if (KosDynNode_IS_LOOP(region)) {
      Py_XDECREF(KosDynLoop_RESULT((KosDynLoop *) region));
      KosDynLoop_RESULT((KosDynLoop *) region) = result;
    } else {
      Py_XDECREF(KosDynForeach_RESULT((KosDynForeach *) region));
      KosDynForeach_RESULT((KosDynForeach *) region) = result;
    }

    uint32_t continue_pc = (KosDynNode_IS_LOOP(region))
        ? KosDynLoop_CONTINUE_PC((KosDynLoop *) region)
        : KosDynForeach_CONTINUE_PC((KosDynForeach *) region);

    if (KosEvalState_StackClearBlock(es) < 0) {
      return -1;
    }

    KosEvalState_SET_PC(es, continue_pc);
    return 0;
  }

  /* KOS_XFER_EVENT_BREAK: read break_pc before popping, then pop, drop block,
     push result */
  uint32_t break_pc = (KosDynNode_IS_LOOP(region))
      ? KosDynLoop_BREAK_PC((KosDynLoop *) region)
      : KosDynForeach_BREAK_PC((KosDynForeach *) region);

  if (KosDynNode_IS_LOOP(region)) {
    if (kos_dyn_pop_loop(es, (KosDynLoop *) region) < 0)
      return -1;
  } else if (kos_dyn_pop_foreach(es, (KosDynForeach *) region) < 0) {
    return -1;
  }

  if (KosEvalState_StackPushRef(es, result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  KosEvalState_SET_PC(es, break_pc);
  return 0;
}


/**
   Walk dyn_top down toward frame_top, handle(RAISE) then pop until a TRY or
   FINALLY consumes (jump to handler) or no handler remains. Same semantics as
   legacy try_local_exception_handler in eval.c: return 1 if handler applied,
   0 if out_ev gets RAISE, -1 on error. Callers: eval transition
   KOS_TRANS_TRY_LOCAL_RAISE.
*/
int kos_dyn_try_local_raise_body(KosEvalState *es, KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  #if VERBOSE_DEBUG
  if (! ctx) {
    SAY("[try_local] ctx=NULL\n");
  }
  else if (KosEvalState_FRAME(es)) {
    size_t _try_local_stack_len = 0;
    _try_local_stack_len = KosEvalState_StackLen(es);
    SAYF("[try_local] enter dyn_top=%p frame_top=%p acting_pc=%u stack_len=%zu\n",
         (void *) KosEvalState_DYN_TOP(es),
         (void *) KosEvalState_FRAME(es),
         (unsigned) KosEvalState_ACTING_PC(es),
         (size_t) _try_local_stack_len);
  }
  else {
    SAYF("[try_local] enter dyn_top=%p frame_top=NULL\n",
         (void *) KosEvalState_DYN_TOP(es));
  }

  if (KosEvalState_FRAME(es) && KosEvalState_DYN_TOP(es) != NULL
      && KosEvalState_DYN_TOP(es) == (KosDynNode *) KosEvalState_FRAME(es)) {
    SAYF("[try_local] skip_search dyn_top==frame_top %p\n",
         (void *) KosEvalState_DYN_TOP(es));
  }
  #endif

  PyObject *saved_exc_typ = NULL;
  PyObject *saved_exc_val = NULL;
  PyObject *saved_exc_tb = NULL;

  while (KosEvalState_FRAME(es) && KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) KosEvalState_FRAME(es)) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    KOS_TRACE_EVENTS_INC(KOS_TE_RAISE_LOCAL_WALK_ITER);

    SAYF("[try_local] walk top=%p kind=%s\n",
         (void *) top,
         kos_dynnode_kind_name(KosDynNode_KIND(top)));

    /* Handle phase: offer RAISE to node before unwind.  If consumed,
       deliver to handler and return.  Otherwise pop and continue. */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RAISE, out_ev);
      SAYF("[try_local] kos_dyn_handle RAISE hr=%d (%s)\n",
           (int) hr,
           hr == KOS_HANDLE_CONSUMED ? "CONSUMED"
             : hr == KOS_HANDLE_PASS ? "PASS"
             : hr == KOS_HANDLE_DEFERRED ? "DEFERRED"
             : hr == KOS_HANDLE_ERROR ? "ERROR" : "?");

      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_GEN(top)) {
          kos_eval_emit_raise_from_pyerr(ctx, out_ev, es);
          return 0;
        }
        KosDynNode *region = top;
        uint32_t handler_pc =
            kos_dynnode_raise_handler_pc_for_try_finally(es, region);

        if (KosDynNode_IS_TRY(region) || KosDynNode_IS_FINALLY(region)) {
          if (KosEvalState_StackClearBlock(es) < 0) {
            return -1;
          }
        }

        SAYF("[try_local] CONSUMED region=%s handler_pc=%u\n",
             KosDynNode_IS_TRY(region) ? "TRY" : "FINALLY",
             (unsigned) handler_pc);
        KosEvalState_SET_PC(es, handler_pc);

        if (KosDynNode_IS_TRY(region))
          KosDynTry_ENABLED((KosDynTry *) region) = 0;
        else
          KosDynFinally_ENABLED((KosDynFinally *) region) = 0;

        {
          PyObject *typ = NULL, *val = NULL, *tb = NULL;
          PyErr_Fetch(&typ, &val, &tb);
          PyErr_SetExcInfo(typ, val, tb);
          PyErr_Clear();
        }

        if (KosDynNode_IS_FINALLY(region))
          KosDynFinally_HANDLING_RAISE((KosDynFinally *) region) = 1;
        return 1;
      }

      if (hr == KOS_HANDLE_ERROR) {
        return -1;
      }
    }

    /* PASS: unwind and pop */
    {
      int top_may_swallow = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_CATCH));
      uint32_t catch_resume_pc = kos_dynnode_catch_resume_pc(top);
      int needs_preserve = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP))
          || (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER));
      if (needs_preserve && PyErr_Occurred())
        PyErr_Fetch(&saved_exc_typ, &saved_exc_val, &saved_exc_tb);
      int pop_res = kos_dyn_pop_nonbarrier_top(es, KOS_XFER_EVENT_RAISE,
                                               out_ev);
      if (saved_exc_typ != NULL) {
        PyErr_Restore(saved_exc_typ, saved_exc_val, saved_exc_tb);
        saved_exc_typ = saved_exc_val = saved_exc_tb = NULL;
      }
      if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        KosEvalState_SetUnwindTargetFrame(es, KosEvalState_FRAME(es));
        return 1;
      }
      if (pop_res != 0) {
        if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_HANDLES_EXCEPTIONS)) {
          int exc_res = kos_dyn_exception_unwind(es, top, out_ev);
          if (exc_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
            KosEvalState_SetUnwindTargetFrame(es, KosEvalState_FRAME(es));
            return 1;
          }
          if (exc_res == 1) {
            return 1;
          }
          if (exc_res == 0)
            continue;
        }
        SAY("[try_local] pop/unwind path -> KOS_EV_RAISE\n");
        kos_eval_emit_raise_from_pyerr(ctx, out_ev, es);
        return 0;
      }
      if (top_may_swallow && ! PyErr_Occurred()) {
        if (catch_resume_pc != (uint32_t) -1 && KosEvalState_FRAME(es)) {
          KosEvalState_SET_PC(es, catch_resume_pc);
        }
        return 1;
      }
    }
  }

  /* No handler found */
  {
    SAY("[try_local] no handler found -> KOS_EV_RAISE\n");
    kos_eval_emit_raise_from_pyerr(ctx, out_ev, es);
    return 0;
  }
}


/**
   Allocate YIELD_FROM node, set as DYN_TOP, refresh frame_top. Callers: opcode
   YIELD_FROM setup (generator delegation).
*/
KosDynYieldFrom *kos_dyn_push_yield_from(
    KosEvalState *es,
    PyObject *delegate,
    uint8_t kind) {

  KosDynYieldFrom *node = kos_dynyieldfrom_new(
      KosEvalState_DYN_TOP(es), delegate, kind);
  if (! node)
    return NULL;

  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynyieldfrom_free(node);
    return NULL;
  }
  SAYF("[gen] push_yield_from done dyn_top=%p parent=%p (%s) frame_top=%p\n",
       (void *) node, (void *) KosDynNode_PARENT(node),
       KosDynNode_PARENT(node)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosDynNode_PARENT(node)))
           : "NULL",
       (void *) KosEvalState_FRAME(es));
  return node;
}


/**
   Unlink YIELD_FROM from top and free it. node must be dyn_top.
   Callers: YIELD_FROM completion and unwind paths.
*/
int kos_dyn_pop_yield_from(KosEvalState *es, KosDynYieldFrom *node) {
  SAYF("[gen] pop_yield_from node=%p dyn_top=%p\n",
       (void *) node, (void *) KosEvalState_DYN_TOP(es));
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError,
                    "YIELD_FROM node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynyieldfrom_free(node);
  }
  SAYF("[gen] pop_yield_from done dyn_top=%p (%s)\n",
       (void *) KosEvalState_DYN_TOP(es),
       KosEvalState_DYN_TOP(es)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_DYN_TOP(es)))
           : "NULL");
  return 0;
}


/* === Prompt lookup and validation === */


/**
   Find PROMPT with matching id walking from dyn_top. Callers: shift capture,
   POP_PROMPT, reset/shift routing.
*/
KosDynPrompt *kos_find_prompt(KosEvalState *es, uint32_t prompt_id) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node) {
    if (KosDynNode_IS_PROMPT(node)) {
      KosDynPrompt *prompt = (KosDynPrompt *) node;
      if (KosDynPrompt_ID(prompt) == prompt_id) {
        return prompt;
      }
    }
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


/**
   Innermost PROMPT from dyn_top. Callers: delimited continuation edge cases.
*/
KosDynPrompt *kos_find_nearest_prompt(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node) {
    if (KosDynNode_IS_PROMPT(node))
      return (KosDynPrompt *) node;
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


/**
   Ensure prompt lies on the chain from dyn_top (shift capture precondition).
*/
int kos_validate_capture(KosEvalState *es, KosDynPrompt *prompt) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node && node != (KosDynNode *) prompt) {
    node = KosDynNode_PARENT(node);
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found in chain");
    return -1;
  }

  return 0;
}


/**
   Walk from DYN_TOP toward the given prompt, offering handle(SHIFT) to
   each node. GEN denies (returns -1); PROMPT consumes (success).
   Call before building a PUSH_SHIFT unwind plan.

   :return: 0 when we reach prompt and it consumes, -1 on error (GEN denies)
*/
static int kos_dyn_walk_handle_shift_to_prompt(KosEvalState *es,
                                               KosDynPrompt *prompt) {

  KosDynNode *node = KosEvalState_DYN_TOP(es);
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  while (node && node != prompt_node) {
    KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_SHIFT, NULL);
    if (hr == KOS_HANDLE_ERROR) {
      return -1;
    }
    if (hr == KOS_HANDLE_CONSUMED) {
      PyErr_SetString(PyExc_RuntimeError,
                      "handle(SHIFT) consumed before reaching prompt");
      return -1;
    }
    node = KosDynNode_PARENT(node);
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found in chain");
    return -1;
  }

  {
    KosHandleResult hr = kos_dyn_handle(es, prompt_node,
                                        KOS_XFER_EVENT_SHIFT, NULL);
    if (hr != KOS_HANDLE_CONSUMED) {
      PyErr_SetString(PyExc_RuntimeError,
                      "prompt did not consume handle(SHIFT)");
      return -1;
    }
  }
  return 0;
}


/**
   Innermost CC node from dyn_top. Callers: kos_capture_cc.
*/
KosDynCC *kos_find_cc(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node) {
    if (KosDynNode_IS_CC(node))
      return (KosDynCC *) node;
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


/**
   Ensure cc lies on the chain from dyn_top (call/cc capture precondition).
*/
static int kos_validate_capture_cc(KosEvalState *es, KosDynCC *cc) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node && node != (KosDynNode *) cc) {
    node = KosDynNode_PARENT(node);
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "CC block not found in chain");
    return -1;
  }

  return 0;
}


/* === Continuation capture and free === */


/**
   Allocate and initialize a continuation. Takes ownership of captured_chain.
   resume_stack is stored as-is (caller must pass the stack to store; ownership transfers).
   token_ref is INCREF'd. Returns NULL on alloc failure (PyErr_NoMemory set);
   caller must free captured_chain on failure.
*/
#define CONT_MAX_FREE 64  /* cap for cached KosContinuation; tunable */

static size_t cont_freecount = 0;
static KosContinuation *cont_freelist = NULL;


/**
   Allocate or reuse a KosContinuation struct and wire captured chain, resume
   frame/pc/stack, graft id, one-shot flag, token ref. Callers: kos_shift_capture,
   kos_capture_cc, kos_continuation_clone.
*/
static KosContinuation *kos_continuation_create(
    KosDynNode *captured_chain,
    KosDynFrame *resume_frame,
    uint32_t resume_pc,
    KosStackBlock *resume_stack,
    uint32_t prompt_id,
    uint32_t graft_point_id,
    int one_shot,
    PyObject *token_ref) {

  KosContinuation *k = cont_freelist;
  if (k) {
    cont_freelist = (KosContinuation *) KosContinuation_CAPTURED_CHAIN(k);
    cont_freecount--;
    KOS_TRACE_FL_ALLOC("continuation", 1);
    memset(k, 0, sizeof(KosContinuation));
  } else {
    KOS_TRACE_FL_ALLOC("continuation", 0);
    k = PyMem_Malloc(sizeof(KosContinuation));
    if (! k) {
      PyErr_NoMemory();
      return NULL;
    }
    memset(k, 0, sizeof(KosContinuation));
  }

  KosContinuation_CAPTURED_CHAIN(k) = captured_chain;
  KosContinuation_RESUME_FRAME(k) = resume_frame;
  KosContinuation_RESUME_PC(k) = resume_pc;
  KosContinuation_RESUME_STACK(k) = resume_stack;
  KosContinuation_PROMPT_ID(k) = prompt_id;
  KosContinuation_GRAFT_POINT_ID(k) = graft_point_id;
  KosContinuation_ONE_SHOT(k) = one_shot;
  KosContinuation_TOKEN_REF(k) = token_ref;
  if (KosContinuation_TOKEN_REF(k))
    Py_INCREF(KosContinuation_TOKEN_REF(k));

  return k;
}


/**
   Delimited shift: validate prompt, walk handle(SHIFT), clone chain segment for
   invoke template, build one-shot KosContinuation. Callers: opcode PUSH_SHIFT.
*/
int kos_shift_capture(
    KosEvalState *es,
    uint32_t prompt_id,
    uint32_t resume_pc,
    KosContinuation **out_k) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  /* Find the prompt */
  KosDynPrompt *prompt = kos_find_prompt(es, prompt_id);
  if (! prompt) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found");
    return -1;
  }

  /* Validate no Python boundaries in the way */
  if (kos_validate_capture(es, prompt) < 0) {
    return -1;
  }

  /* handle(SHIFT) walk: GEN denies; must reach PROMPT which consumes */
  if (kos_dyn_walk_handle_shift_to_prompt(es, prompt) < 0) {
    return -1;
  }

  /* Clone the chain segment that will be restored on invoke: the prompt and
   * the frames below it (ns, frame).  Include the frame so resume_frame is
   * found and the push segment contains a frame for kos_dyn_rebuild_live_from_chain.
   * That way the resumed context has a prompt to POP_PROMPT and can return
   * to the invoker. */
  KosDynNode *frame_inside = KosDynNode_PARENT(prompt);
  KosDynNode *segment_top = (KosDynNode *) prompt;
  KosDynNode *stop = NULL;  /* capture full chain so frame is included */
  if (KOS_XFER_TRACE && frame_inside) {
    if (KosDynNode_IS_FRAME(frame_inside)) {
      KosDynFrame *f = (KosDynFrame *) frame_inside;
      SAYF("[xfer] shift_capture segment_top=%p (prompt) frame_inside=%p kind=%u stack=%p len=%zu",
           (void *) segment_top, (void *) frame_inside,
           (unsigned) KosDynNode_KIND(frame_inside),
           (void *) KosDynFrame_STACK(f),
           KosDynFrame_STACK(f)
               ? (size_t) KosStackBlock_LEN(KosDynFrame_STACK(f)) : 0);
      (void) f;

    } else {
      SAYF("[xfer] shift_capture segment_top=%p (prompt) frame_inside=%p kind=%u",
           (void *) segment_top, (void *) frame_inside,
           (unsigned) KosDynNode_KIND(frame_inside));
    }
  }

  KosDynNode *captured_chain = kos_clone_chain_segment(segment_top, stop);
  if (! captured_chain && segment_top != stop) {
    /* Clone failed */
    return -1;
  }

  /* Make the template self-contained: clear the bottom's parent so we do not
   * retain a reference to the original chain (e.g. the prompt).  Otherwise
   * INSTALL would follow parent into freed memory when cloning from template. */
  if (captured_chain) {
    KosDynNode *bottom = captured_chain;
    while (bottom->parent)
      bottom = bottom->parent;
    bottom->parent = NULL;
  }

  /* Find the topmost frame in the captured chain */
  KosDynFrame *resume_frame = NULL;
  KosDynNode *node = captured_chain;
  while (node) {
    if (KOS_XFER_TRACE)
      SAYF("[xfer] shift_capture walk node=%p kind=%u",
           (void *) node, (unsigned) KosDynNode_KIND(node));
    if (KosDynNode_IS_FRAME(node)) {
      resume_frame = (KosDynFrame *) node;
      break;
    }
    node = KosDynNode_PARENT(node);
  }
  if (! resume_frame) {
    kos_dynchain_free(captured_chain, NULL);
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_SHIFT: no frame in captured chain");
    return -1;
  }

  KosStackBlock *resume_stack = KosDynFrame_STACK(resume_frame);
  if (! resume_stack) {
    KosDynNode *rs_node = captured_chain;
    while (rs_node && ! KosDynNode_STACK(rs_node))
      rs_node = KosDynNode_PARENT(rs_node);
    resume_stack = rs_node ? KosDynNode_STACK(rs_node) : NULL;
  }
  uint32_t pc = resume_frame ? resume_pc : 0;
  uint32_t graft_point_id = stop ? KosDynNode_NODE_ID(stop) : 0;
  KosContinuation *k = kos_continuation_create(
      captured_chain,
      resume_frame,
      pc,
      resume_stack,
      prompt_id,
      graft_point_id,
      1,  /* one-shot */
      KosContext_TOKEN(ctx) ? (PyObject *) KosContext_TOKEN(ctx) : NULL);

  if (! k) {
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  *out_k = k;
  return 0;
}


/**
   Undelimited call/cc: clone chain above CC, multi-shot continuation with
   undelimited prompt id. Callers: opcode CAPTURE_CC.
*/
int kos_capture_cc(KosEvalState *es, KosContinuation **out_k) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosDynCC *cc = kos_find_cc(es);
  if (! cc) {
    PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC: no CC block on dynamic chain");
    return -1;
  }

  if (kos_validate_capture_cc(es, cc) < 0) {
    return -1;
  }

  /* Capture from the frame above the CC block (parent of CC) down to root.
   * On invoke we install this segment and resume at resume_pc in that frame. */
  KosDynNode *segment_top = KosDynNode_PARENT(cc);
  if (! segment_top) {
    PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC: CC block has no parent");
    return -1;
  }

  KosDynNode *captured_chain = kos_clone_chain_segment(segment_top, NULL);
  if (! captured_chain) {
    return -1;
  }

  /* Make the template self-contained */
  {
    KosDynNode *bottom = captured_chain;
    while (bottom->parent)
      bottom = bottom->parent;
    bottom->parent = NULL;
  }

  /* Find the topmost frame in the captured chain (the resume frame) */
  KosDynFrame *resume_frame = NULL;
  KosDynNode *node = captured_chain;
  while (node) {
    if (KosDynNode_IS_FRAME(node)) {
      resume_frame = (KosDynFrame *) node;
      break;
    }
    node = KosDynNode_PARENT(node);
  }

  if (! resume_frame) {
    kos_dynchain_free(captured_chain, NULL);
    PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC: no frame in captured segment");
    return -1;
  }

  KosStackBlock *resume_stack_cc = KosDynFrame_STACK(resume_frame);
  if (! resume_stack_cc) {
    KosDynNode *rs_node = captured_chain;
    while (rs_node && ! KosDynNode_STACK(rs_node))
      rs_node = KosDynNode_PARENT(rs_node);
    resume_stack_cc = rs_node ? KosDynNode_STACK(rs_node) : NULL;
  }

  KosContinuation *k = kos_continuation_create(
      captured_chain,
      resume_frame,
      KosDynCC_RESUME_PC(cc),
      resume_stack_cc,
      KOS_PROMPT_ID_UNDELIMITED,
      0,  /* graft_point_id: capture-cc captures to root */
      0,  /* multi-shot */
      KosContext_TOKEN(ctx) ? (PyObject *) KosContext_TOKEN(ctx) : NULL);

  if (! k) {
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  *out_k = k;
  return 0;
}


/**
   Free continuation and captured chain. Callers: error paths, wrapper dealloc.
*/
void kos_continuation_free(KosContinuation *k) {
  if (! k)
    return;

  Py_XDECREF(KosContinuation_TOKEN_REF(k));
  KosContinuation_TOKEN_REF(k) = NULL;

  /* resume_stack aliases the operand stack owned by resume_frame (or an
   * innermost region node) on captured_chain; kos_dynchain_free releases
   * node-owned blocks. Clear the pointer only. */
  KosContinuation_RESUME_STACK(k) = NULL;

  kos_dynchain_free(KosContinuation_CAPTURED_CHAIN(k), NULL);

  if (cont_freecount < CONT_MAX_FREE) {
    KOS_TRACE_FL_RETURN("continuation", 0);
    memset(k, 0, sizeof(KosContinuation));
    KosContinuation_CAPTURED_CHAIN(k) = (KosDynNode *) cont_freelist;
    cont_freelist = k;
    cont_freecount++;
  } else {
    KOS_TRACE_FL_RETURN("continuation", 1);
    PyMem_Free(k);
  }
}


/**
   Deep-copy captured segment and stack for a multi-shot re-invoke. Callers:
   kos_invoke_continuation when ONE_SHOT is false.
*/
KosContinuation *kos_continuation_clone(KosContinuation *k) {
  if (! k)
    return NULL;

  /* Clone only the continuation's segment (do not clone into context below
   * the prompt). Find the bottom of the captured chain and use its parent
   * as stop so we don't clone context nodes. */
  KosDynNode *bottom = KosContinuation_CAPTURED_CHAIN(k);
  while (bottom && bottom->parent)
    bottom = bottom->parent;
  KosDynNode *stop = bottom ? bottom->parent : NULL;

  KosDynNode *cloned_chain = kos_clone_chain_segment(KosContinuation_CAPTURED_CHAIN(k), stop);
  if (! cloned_chain && KosContinuation_CAPTURED_CHAIN(k)) {
    SAYF("[xfer] continuation_clone failed captured_chain=%p stop=%p pyerr=%d",
         (void *) KosContinuation_CAPTURED_CHAIN(k), (void *) stop,
         PyErr_Occurred() ? 1 : 0);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation captured chain clone failed");
    return NULL;
  }

  KosDynFrame *resume_frame = NULL;
  KosDynNode *node = cloned_chain;
  while (node) {
    if (KosDynNode_IS_FRAME(node)) {
      resume_frame = (KosDynFrame *) node;
      break;
    }
    node = KosDynNode_PARENT(node);
  }
  if (KosContinuation_CAPTURED_CHAIN(k) && ! resume_frame) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation clone has no frame");
    kos_dynchain_free(cloned_chain, NULL);
    return NULL;
  }

  KosContinuation *clone = kos_continuation_create(
      cloned_chain,
      resume_frame,
      KosContinuation_RESUME_PC(k),
      KosStackBlock_DeepCopy(KosContinuation_RESUME_STACK(k)),
      KosContinuation_PROMPT_ID(k),
      KosContinuation_GRAFT_POINT_ID(k),
      KosContinuation_ONE_SHOT(k),
      KosContinuation_TOKEN_REF(k));

  if (! clone) {
    kos_dynchain_free(cloned_chain, NULL);
    return NULL;
  }

  return clone;
}


/* === LCA computation === */


/**
   Linear search from chain toward root for a stable node_id (graft fallback
   when LCA walk finds no intersection across disjoint templates).
*/
static KosDynNode *kos_find_node_by_id(KosDynNode *chain, uint32_t id) {
  KosDynNode *node = chain;
  while (node) {
    if (KosDynNode_NODE_ID(node) == id)
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   Lowest common ancestor of two chain pointers via transient marks on chain a,
   or kos_find_node_by_id(a, graft_point_id) when disjoint. Callers:
   kos_build_transfer_plan and other builders that need unwind vs install split.
*/
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id) {
  if (! a || ! b)
    return NULL;

  KosDynNode *orig_a = a;

  /* Mark every node in chain a with the transient visited flag. */
  for (KosDynNode *n = a; n; n = KosDynNode_PARENT(n))
    KosDynNode_FLAGS(n) |= KOS_DYNF_LCA_VISITED;

  /* Walk chain b; the first marked node is the LCA.  If the chains are
   * disjoint (e.g. severed continuation template), no marked node is found. */
  KosDynNode *lca = NULL;
  for (KosDynNode *n = b; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_FLAGS(n) & KOS_DYNF_LCA_VISITED) {
      lca = n;
      break;
    }
  }

  /* Clear the transient marks from chain a. */
  for (KosDynNode *n = a; n; n = KosDynNode_PARENT(n))
    KosDynNode_FLAGS(n) &= ~KOS_DYNF_LCA_VISITED;

  if (! lca && graft_point_id)
    return kos_find_node_by_id(orig_a, graft_point_id);
  return lca;
}


/* === Transfer plan building === */


static KosTransferPlan *xfer_plan_cache = NULL;


/**
   Allocate a transfer plan with action_count action slots. Plan and actions
   are one allocation; default 32 slots, realloc entire block if more needed.
   Reuses cached plan when available. Returns NULL on failure (PyErr_NoMemory).
*/
static KosTransferPlan *kos_transfer_plan_alloc(
    size_t action_count) {

  size_t capacity = (action_count <= KOS_XFER_PLAN_DEFAULT_ACTIONS)
      ? KOS_XFER_PLAN_DEFAULT_ACTIONS
      : action_count;
  size_t size = sizeof(KosTransferPlan) + capacity * sizeof(KosTransferAction);

  KosTransferPlan *plan = NULL;
  if (xfer_plan_cache) {
    plan = xfer_plan_cache;
    xfer_plan_cache = NULL;
    if (KosTransferPlan_ACTION_CAPACITY(plan) < action_count) {
      capacity = action_count;
      size = sizeof(KosTransferPlan) + capacity * sizeof(KosTransferAction);
      plan = PyMem_Realloc(plan, size);
      if (! plan) {
        PyErr_NoMemory();
        return NULL;
      }
    }
  } else {
    plan = PyMem_Malloc(size);
    if (! plan) {
      PyErr_NoMemory();
      return NULL;
    }
  }

  memset(plan, 0, size);
  plan->action_count = action_count;
  plan->action_index = 0;
  plan->action_capacity = capacity;
  return plan;
}


/* Transfer plan counting and filling helpers.  Used by kos_build_transfer_plan. */


/**
   Count how many transfer actions are needed to unwind from current to lca.
   Walks the dynamic chain from current inward toward lca.  For each node:
   - WIND with an "after" hook (unwind): one RUN_HOOK action.
   - PARAM or DYN_WITH: one NODE_UNWIND action.
   Used to size the plan's action array before filling it.
*/
static size_t xfer_plan_count_unwind_and_params(
    KosDynNode *current,
    KosDynNode *lca) {

  size_t n = 0;
  KosDynNode *node = current;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_UNWIND_ACTION)) {
      if (KosDynNode_IS_WIND(node)) {
        KosDynWind *wind = (KosDynWind *) node;
        if (KosDynWind_ENTERED(wind) && KosDynWind_AFTER_THUNK(wind))
          n++;
      } else {
        n++;
      }
    }
    node = KosDynNode_PARENT(node);
  }
  return n;
}


/**
   Count how many "before" wind hooks lie between target and lca.
   Walks from target toward lca.  Each WIND node that has a before hook and
   has not yet been entered contributes one RUN_HOOK action when we later
   re-enter the segment.  Used to size the plan and to allocate the temp
   array in xfer_plan_fill_wind_actions.
*/
static size_t xfer_plan_count_wind_before(
    KosDynNode *target,
    KosDynNode *lca) {

  size_t n = 0;
  KosDynNode *node = target;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_THUNK(wind))
        n++;
    }
    node = KosDynNode_PARENT(node);
  }
  return n;
}


/**
   Fill plan action slots with unwind-phase steps: RUN_HOOK (after) and
   NODE_UNWIND (PARAM/DYN_WITH), from current inward to lca.  Order is
   inner-to-outer so that when the plan is executed, we run after hooks
   and unwind nodes in the correct order.  Advances *idx past the filled slots.
*/
static void xfer_plan_fill_unwind_actions(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *current,
    KosDynNode *lca) {

  KosDynNode *node = current;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_UNWIND_ACTION)) {
      if (KosDynNode_IS_WIND(node)) {
        KosDynWind *wind = (KosDynWind *) node;
        if (KosDynWind_ENTERED(wind) && KosDynWind_AFTER_THUNK(wind)) {
          KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_ACTION;
          KosTransferAction_EXTENT_NODE(&KosTransferPlan_ACTIONS(plan)[*idx]) = node;
          KosTransferAction_EXTENT_PHASE(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_PHASE_UNWIND;
          if (KOS_XFER_TRACE)
            SAYF("[xfer] plan action[%zu] = EXTENT_ACTION (unwind wind) node=%p",
                 *idx, (void *) node);
          (*idx)++;
        }
      } else {
        /* PARAM or DYN_WITH with NEEDS_UNWIND_ACTION */
        KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_ACTION;
        KosTransferAction_EXTENT_NODE(&KosTransferPlan_ACTIONS(plan)[*idx]) = node;
        KosTransferAction_EXTENT_PHASE(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_PHASE_UNWIND;
        if (KOS_XFER_TRACE)
          SAYF("[xfer] plan action[%zu] = EXTENT_ACTION (unwind) node=%p", *idx, (void *) node);
        (*idx)++;
      }
    }
    node = KosDynNode_PARENT(node);
  }
}


/**
   Fill a single INSTALL action: the plan will replace the current dynamic
   chain with a clone of the segment rooted at target.  Advances *idx by one.
*/
static void xfer_plan_fill_install_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target) {

  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_INSTALL;
  KosTransferAction_INSTALL_SEGMENT(&KosTransferPlan_ACTIONS(plan)[*idx]) = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = INSTALL segment=%p",
         *idx, (void *) target);
  (*idx)++;
}


/**
   Fill a single PUSH action: append a clone of the segment to the current
   chain (graft bottom to dyn_top).  Advances *idx by one.
*/
static void xfer_plan_fill_push_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target) {

  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_PUSH;
  KosTransferAction_INSTALL_SEGMENT(&KosTransferPlan_ACTIONS(plan)[*idx]) = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = PUSH segment=%p",
         *idx, (void *) target);
  (*idx)++;
}


/**
   Fill plan action slots with wind "before" hooks from lca toward target.
   We need outer-to-inner order at run time, but the chain walk is
   target-to-lca (inner-to-outer).  So we collect wind nodes into a
   temporary array, then emit RUN_HOOK actions in reverse order.
   Returns 0 on success, -1 on allocation failure.
*/
static int xfer_plan_fill_wind_actions(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target,
    KosDynNode *lca,
    size_t wind_count) {

  if (wind_count == 0)
    return 0;

  KosDynWind **wind_nodes = PyMem_Malloc(sizeof(KosDynWind *) * wind_count);
  if (! wind_nodes) {
    /* Ownership inversion: this function frees plan on failure so that callers
       (kos_build_transfer_plan, kos_build_delimited_push_plan, etc.) can do a
       single unconditional "return -1" without a separate free on this path. */
    PyMem_Free(plan);
    PyErr_NoMemory();
    return -1;
  }

  size_t wi = 0;
  KosDynNode *node = target;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_THUNK(wind))
        wind_nodes[wi++] = wind;
    }
    node = KosDynNode_PARENT(node);
  }

  while (wi--) {
    KosDynWind *wind = wind_nodes[wi];
    KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_ACTION;
    KosTransferAction_EXTENT_NODE(&KosTransferPlan_ACTIONS(plan)[*idx]) = (KosDynNode *) wind;
    KosTransferAction_EXTENT_PHASE(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_PHASE_WIND;
    (*idx)++;
  }

  PyMem_Free(wind_nodes);
  return 0;
}


/**
   Fill a single INJECT action: the value (or throw) to deliver when the
   transfer completes.  Takes a new reference on value (and exc_type when
   is_throw).  Advances *idx by one.
*/
static void xfer_plan_fill_inject_action(
    KosTransferPlan *plan,
    size_t *idx,
    PyObject *value,
    PyObject *exc_type,
    int is_throw) {

  KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[*idx];
  KosTransferAction_STEP(a) = KOS_XFER_INJECT;
  KosTransferAction_INJECT_VALUE(a) = value;
  Py_XINCREF(value);
  KosTransferAction_INJECT_TYPE(a) = exc_type;
  if (exc_type)
    Py_INCREF(exc_type);
  KosTransferAction_INJECT_IS_THROW(a) = is_throw;
  (*idx)++;
}


/**
   Append one KOS_XFER_POP_ONE slot (uninstall current top during plan run).
   Callers: kos_dyn_build_push_shift_unwind_plan.
*/
static void xfer_plan_fill_pop_one_action(KosTransferPlan *plan, size_t *idx) {
  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_POP_ONE;
  (*idx)++;
}


static void xfer_plan_fill_truncate_and_inject_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *node,
    PyObject *value,
    PyObject *exc_type,
    int is_throw) {

  KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[*idx];
  KosTransferAction_STEP(a) = KOS_XFER_TRUNCATE_AND_INJECT;
  KosTransferAction_TRUNCATE_NODE(a) = node;
  KosTransferAction_TRUNCATE_VALUE(a) = value;
  Py_XINCREF(value);
  KosTransferAction_TRUNCATE_TYPE(a) = exc_type;
  if (exc_type)
    Py_INCREF(exc_type);
  KosTransferAction_TRUNCATE_IS_THROW(a) = is_throw;
  (*idx)++;
}


/**
   Build a transfer plan to move control from the current dynamic chain top
   to the segment rooted at target, then deliver value (or throw it).
   Computes the LCA of current and target, counts all required actions
   (unwind hooks + param restores, one INSTALL, wind before hooks, one
   INJECT), allocates the plan, and fills it in phase order.  Caller runs
   the plan by repeatedly calling kos_run_transfer_step until it returns 1.
   Returns 0 on success, -1 on error (e.g. allocation failure).
*/
int kos_build_transfer_plan(
    KosEvalState *es,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KOS_TRACE_EVENTS_INC(KOS_TE_TRANSFER_PLAN_BUILD);

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca = kos_compute_lca(current, target, graft_point_id);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] build_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  size_t wind_count = xfer_plan_count_wind_before(target, lca);
  size_t action_count = xfer_plan_count_unwind_and_params(current, lca)
      + 1  /* INSTALL */
      + wind_count
      + 1;  /* INJECT */

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_GRAFT_POINT_ID(plan) = graft_point_id;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_unwind_actions(plan, &idx, current, lca);
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0) {
    return -1;
  }
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


/**
   Build a minimal plan with only INSTALL and INJECT (no wind, no LCA).
   Use for the delimited k(value) return path when the invocation-site
   chain is disjoint from the current chain.  Caller must set RESUME_PC.
   Returns 0 on success, -1 on error.
*/
int kos_build_return_plan(
    KosEvalState *es,
    KosDynNode *segment,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosTransferPlan *plan = kos_transfer_plan_alloc(2);  /* INSTALL + INJECT */
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = 2;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = segment;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_install_action(plan, &idx, segment);
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


/**
   Build a PUSH plan for delimited continuation invoke.  Segment is the
   continuation's frames (first child of prompt in captured_chain).
*/
int kos_build_delimited_push_plan(
    KosEvalState *es,
    KosDynNode *segment,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  if (! segment) {
    PyErr_SetString(PyExc_RuntimeError,
                    "delimited push: empty continuation segment");
    return -1;
  }

  size_t wind_count = xfer_plan_count_wind_before(segment, NULL);
  size_t action_count = wind_count + 1  /* PUSH */ + 1;  /* INJECT */

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = segment;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  if (xfer_plan_fill_wind_actions(plan, &idx, segment, NULL, wind_count) < 0) {
    return -1;
  }
  xfer_plan_fill_push_action(plan, &idx, segment);
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


/**
   Build a transfer plan that skips unwind (INSTALL + wind + INJECT only).
   Use when the current and target chains are disjoint or when the path
   crosses a PROMPT barrier that kos_dyn_pop_top cannot traverse, such as
   after a nested transfer interrupted and completed an outer plan's wind
   thunk.

   Caller must set RESUME_PC, RESUME_FRAME, RESUME_STACK on the plan.
   Returns 0 on success, -1 on error.
*/
int kos_build_direct_install_plan(
    KosEvalState *es,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca = kos_compute_lca(current, target, graft_point_id);
  if (! lca) {
    /* Target may be an ancestor of current (path crosses prompt). */
    KosDynNode *n = current;
    while (n && n != target)
      n = KosDynNode_PARENT(n);
    if (n == target)
      lca = target;
    /* Or target may be from a severed template (disjoint chain) - e.g.
     * delimited invoke.  Use lca = target so wind_count = 0. */
    if (! lca)
      lca = target;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] direct_install_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  size_t wind_count = xfer_plan_count_wind_before(target, lca);
  size_t action_count = 1  /* INSTALL */
      + wind_count
      + 1;  /* INJECT */

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_GRAFT_POINT_ID(plan) = graft_point_id;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0) {
    return -1;
  }
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


/**
   Attach CC_RAISE sentinel at chain root and build a full transfer plan to
   NULL target so unwind runs teardown then re-raises via sentinel. Callers:
   foreign continuation apply error path.
*/
int kos_build_cc_unwind_plan(
    KosEvalState *es,
    PyObject *exc_type,
    PyObject *exc_val,
    KosTransferPlan **out_plan) {

  /* Insert the sentinel at the bottom (root) of the chain so that the
     transfer plan unwinds all existing WITH/finally nodes first and hits the
     KosDynCCRaise re-raise step last.  We walk to the current chain root and
     attach the new node below it. */
  KosDynCCRaise *raise_node = kos_dyn_cc_raise_new(NULL, exc_type, exc_val);
  if (! raise_node) {
    return -1;
  }

  KosDynNode *cur = KosEvalState_DYN_TOP(es);
  if (! cur) {
    /* Empty chain: the sentinel becomes dyn_top directly. */
    KosEvalState_DYN_TOP(es) = (KosDynNode *) raise_node;
  } else {
    /* Walk to the bottom of the chain and attach below it. */
    while (KosDynNode_PARENT(cur))
      cur = KosDynNode_PARENT(cur);
    KosDynNode_PARENT(cur) = (KosDynNode *) raise_node;
  }

  /* Build a plan that unwinds from dyn_top all the way to NULL, running
     WITH/finally teardowns along the way.  kos_build_transfer_plan with
     target=NULL treats lca=NULL, so it emits EXTENT_ACTION steps for every
     node that has NEEDS_UNWIND_ACTION (including our sentinel at the bottom),
     then INSTALL(NULL), then INJECT.  When the plan reaches the KosDynCCRaise
     EXTENT_ACTION, cc_raise_unwind sets PyErr and returns -1, which causes
     run_transfer_plan_to_completion to package it as KOS_EV_RAISE. */
  return kos_build_transfer_plan(es, NULL, Py_None, 0, 0, out_plan);
}


/**
   Plan: unwind extent actions to prompt, then TRUNCATE_AND_INJECT value at
   prompt (shift result delivery). Callers: POP_SHIFT completion.
*/
int kos_dyn_build_pop_shift_plan(KosEvalState *es, KosDynPrompt *prompt,
                                 PyObject *value, KosTransferPlan **out_plan) {

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  size_t unwind_count = xfer_plan_count_unwind_and_params(current, prompt_node);
  size_t action_count = unwind_count + 1;  /* TRUNCATE_AND_INJECT */

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = prompt_node;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_unwind_actions(plan, &idx, current, prompt_node);
  xfer_plan_fill_truncate_and_inject_action(plan, &idx, prompt_node,
                                            value, NULL, 0);

  *out_plan = plan;
  return 0;
}


/**
   Plan: POP_ONE repeatedly until dyn_top is prompt (abort path before inject).
   Callers: PUSH_SHIFT unwind-to-prompt setup.
*/
int kos_dyn_build_push_shift_unwind_plan(KosEvalState *es, KosDynPrompt *prompt,
                                         KosTransferPlan **out_plan) {

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  size_t pop_count = 0;
  KosDynNode *node = current;
  while (node && node != prompt_node) {
    pop_count++;
    node = KosDynNode_PARENT(node);
  }
  if (node != prompt_node) {
    PyErr_SetString(PyExc_RuntimeError,
                    "push_shift_unwind: prompt not on chain");
    return -1;
  }

  KosTransferPlan *plan = kos_transfer_plan_alloc(pop_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = pop_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = prompt_node;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  node = current;
  while (node && node != prompt_node) {
    xfer_plan_fill_pop_one_action(plan, &idx);
    node = KosDynNode_PARENT(node);
  }

  *out_plan = plan;
  return 0;
}


/**
   Build a transfer plan with only INSTALL and wind-before hooks (no
   INJECT).  Use when the caller will push a frame and value manually
   after the plan runs (e.g. PUSH_SHIFT abort-to-prompt).  Skips unwind
   entirely, so safe when path crosses PROMPT.  Returns 0 on success.
*/
int kos_build_install_only_plan(
    KosEvalState *es,
    KosDynNode *target,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca = kos_compute_lca(current, target, graft_point_id);
  if (! lca) {
    KosDynNode *n = current;
    while (n && n != target)
      n = KosDynNode_PARENT(n);
    if (n == target)
      lca = target;
    if (! lca) {
      PyErr_SetString(PyExc_RuntimeError,
                      "install_only: target not reachable from current chain");
      return -1;
    }
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] install_only_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  size_t wind_count = xfer_plan_count_wind_before(target, lca);
  size_t action_count = 1  /* INSTALL */
      + wind_count;

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan) {
    return -1;
  }

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_GRAFT_POINT_ID(plan) = graft_point_id;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0) {
    return -1;
  }

  *out_plan = plan;
  return 0;
}


/**
   Release the multi-shot continuation clone held by the plan, if any.
   The clone was used for a re-invocation; we release its resume_stack
   chain.  KosTransferPlan_CONTINUATION_REF holds a strong ref to the
   Python wrapper for the whole plan lifetime, so operand-stack DECREFs
   during release cannot drop the last ref to that object.  Then we
   either return the clone to the continuation freelist or PyMem_Free it.
   Called from kos_free_transfer_plan.
*/
static void xfer_plan_release_invoke_clone(KosTransferPlan *plan) {
  if (! KosTransferPlan_INVOKE_CLONE(plan))
    return;

  KosContinuation *clone = KosTransferPlan_INVOKE_CLONE(plan);
  KosTransferPlan_INVOKE_CLONE(plan) = NULL;
  if (KosContinuation_RESUME_STACK(clone)) {
    KosStackBlock_ReleaseChain(KosContinuation_RESUME_STACK(clone));
    KosContinuation_RESUME_STACK(clone) = NULL;
  }
  if (KosContinuation_CAPTURED_CHAIN(clone)) {
    kos_dynchain_free(KosContinuation_CAPTURED_CHAIN(clone), NULL);
    KosContinuation_CAPTURED_CHAIN(clone) = NULL;
  }
  Py_XDECREF(KosContinuation_TOKEN_REF(clone));
  if (cont_freecount < CONT_MAX_FREE) {
    KOS_TRACE_FL_RETURN("continuation", 0);
    memset(clone, 0, sizeof(KosContinuation));
    KosContinuation_CAPTURED_CHAIN(clone) = (KosDynNode *) cont_freelist;
    cont_freelist = clone;
    cont_freecount++;
  } else {
    KOS_TRACE_FL_RETURN("continuation", 1);
    PyMem_Free(clone);
  }
}


/**
   Free a transfer plan and all resources it owns.  DECREFs any remaining
   INJECT values (we may have completed only part of the plan), releases
   the invoke clone (if any) via xfer_plan_release_invoke_clone, releases
   the resume_stack chain, DECREFs the continuation ref.  Caches the plan
   struct if cache is empty; otherwise frees it.  Safe to call with NULL.
*/
void kos_free_transfer_plan(KosTransferPlan *plan) {
  if (! plan)
    return;

  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[i];
    if (KosTransferAction_STEP(a) == KOS_XFER_INJECT) {
      Py_XDECREF(KosTransferAction_INJECT_VALUE(a));
      Py_XDECREF(KosTransferAction_INJECT_TYPE(a));
    } else if (KosTransferAction_STEP(a) == KOS_XFER_TRUNCATE_AND_INJECT) {
      Py_XDECREF(KosTransferAction_TRUNCATE_VALUE(a));
      Py_XDECREF(KosTransferAction_TRUNCATE_TYPE(a));
    }
  }
  xfer_plan_release_invoke_clone(plan);
  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock_ReleaseChain(KosTransferPlan_RESUME_STACK(plan));
    KosTransferPlan_RESUME_STACK(plan) = NULL;
  }
  Py_XDECREF(KosTransferPlan_CONTINUATION_REF(plan));
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  Py_XDECREF(KosTransferPlan_STEP_AWAITABLE(plan));
  KosTransferPlan_STEP_AWAITABLE(plan) = NULL;
  Py_XDECREF(KosTransferPlan_STEP_RESUME_INJECT(plan));
  KosTransferPlan_STEP_RESUME_INJECT(plan) = NULL;

  if (! xfer_plan_cache) {
    size_t cap = KosTransferPlan_ACTION_CAPACITY(plan);
    memset(plan, 0, sizeof(KosTransferPlan) + cap * sizeof(KosTransferAction));
    plan->action_capacity = cap;  /* preserve for reuse */
    xfer_plan_cache = plan;
  } else {
    PyMem_Free(plan);
  }
}


/* Transfer step handlers (one per action type).  Used by kos_run_transfer_step. */


/**
   Execute an EXTENT_ACTION step: run extent wind (enter) or unwind
   (exit) for the stored node.  Dispatches via kos_dyn_wind or
   kos_dynnode_run_transfer_unwind.  Returns 0 on sync completion, -1
   on error, or KOS_DYN_UNWIND_ASYNC_STARTED when async started.
*/
static int xfer_step_extent_action(KosEvalState *es, KosTransferAction *action,
                                   KosEvent *out_ev) {
  KosDynNode *node = KosTransferAction_EXTENT_NODE(action);
  int phase = KosTransferAction_EXTENT_PHASE(action);

  if (phase == KOS_XFER_EXTENT_PHASE_WIND)
    return kos_dyn_wind(es, node, out_ev);
  return kos_dynnode_run_transfer_unwind(es, node, KOS_XFER_EVENT_NONE,
                                         out_ev);
}


#define KOS_STACK_BUF_SIZE 8


/**
   Run install-phase wind for each node with KOS_DYN_CAP_NEEDS_INSTALL_WIND
   along the parent chain from seg toward chain_stop (walk stops before
   chain_stop).  Wind callbacks run in reverse of the collection order
   (outer chain nodes first), matching prior INSTALL/PUSH behavior.

   :param xfer_fail_tag: trace label on wind failure ("INSTALL" or "PUSH")
   :return: 0, -1, or KOS_DYN_UNWIND_ASYNC_STARTED
*/
static int kos_dyn_run_install_wind_span(
    KosEvalState *es,
    KosDynNode *seg,
    KosDynNode *chain_stop,
    KosEvent *out_ev,
    const char *xfer_fail_tag) {

  if (! seg)
    return 0;

  KosDynNode *stack_buf[KOS_STACK_BUF_SIZE];
  KosDynNode **wind_nodes = stack_buf;
  size_t wind_count = 0;
  size_t wind_cap = KOS_STACK_BUF_SIZE;
  KosDynNode *walk = seg;

  while (walk && walk != chain_stop) {
    if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND)) {
      if (wind_count == wind_cap) {
        size_t new_cap = wind_cap * 2;
        KosDynNode **next = PyMem_Malloc(new_cap * sizeof(KosDynNode *));
        if (! next) {
          PyErr_NoMemory();
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          return -1;
        }
        memcpy(next, wind_nodes, wind_cap * sizeof(KosDynNode *));
        if (wind_nodes != stack_buf)
          PyMem_Free(wind_nodes);
        wind_nodes = next;
        wind_cap = new_cap;
      }
      wind_nodes[wind_count++] = walk;
    }
    walk = KosDynNode_PARENT(walk);
  }

  if (wind_count == 0)
    return 0;

  /* Run wind in reverse order (outer nodes first) */
  for (size_t i = wind_count; i > 0; i--) {
    int r = kos_dynnode_run_wind(es, wind_nodes[i - 1], out_ev);
    if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
      if (wind_nodes != stack_buf)
        PyMem_Free(wind_nodes);
      return KOS_DYN_UNWIND_ASYNC_STARTED;
    }
    if (r < 0) {
      if (wind_nodes != stack_buf)
        PyMem_Free(wind_nodes);
      if (KOS_XFER_TRACE)
        SAYF("[xfer] %s wind failed", xfer_fail_tag);
      return -1;
    }
  }
  if (wind_nodes != stack_buf)
    PyMem_Free(wind_nodes);
  return 0;
}


/**
   Execute an INSTALL step: replace the current dynamic chain with a fresh
   clone of the plan's target segment, restore the frame's stack and PC
   from the plan's resume state, and run param_enter for each PARAM node
   in the new segment (outer to inner).  This "lands" the continuation:
   the context now runs in the captured segment.  On failure we decrement
   the plan's action index so the step can be retried or the caller can
   handle it.  Returns 0 on success, -1 on error,
   KOS_DYN_UNWIND_ASYNC_STARTED when async wind started.
*/
static int xfer_step_install(KosEvalState *es, KosTransferAction *action,
                             KosEvent *out_ev) {
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
  KosDynNode *template_bottom = template_seg;
  while (template_bottom && KosDynNode_PARENT(template_bottom))
    template_bottom = KosDynNode_PARENT(template_bottom);
  KosDynNode *stop = template_bottom ? KosDynNode_PARENT(template_bottom) : NULL;
  KosDynNode *seg = kos_clone_chain_segment(template_seg, stop);
  if (! seg && template_seg) {
    SAY("[xfer] INSTALL clone segment failed");
    KosTransferPlan_ACTION_INDEX(plan)--;  /* leave step for retry or caller */
    return -1;
  }
  #if KOS_XFER_TRACE
    SAYF("[xfer] INSTALL segment=%p (fresh) resume_pc=%u (before: dyn_top=%p frame_top=%p)",
         (void *) seg, (unsigned) KosTransferPlan_RESUME_PC(plan),
         (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  #endif
  {
    KosDynNode *old_top = KosEvalState_DYN_TOP(es);
    KosEvalState_DYN_TOP(es) = seg;
    kos_dyn_rebuild_live_from_chain(es);
    if (old_top)
      kos_dynchain_free(old_top, NULL);
  }

  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock *new_stack = KosTransferPlan_RESUME_STACK(plan);
    KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
    if (! owner && KosEvalState_FRAME(es))
      owner = (KosDynNode *) KosEvalState_FRAME(es);

    if (owner) {
      KosStackBlock *old_stack = KosDynNode_STACK(owner);
      KosDynNode_STACK(owner) = new_stack;
      KosTransferPlan_RESUME_STACK(plan) = NULL;  /* ownership transferred */

      if (old_stack && old_stack != new_stack) {
        KosStackBlock_ReleaseChain(old_stack);
      }
      kos_dyn_rebuild_live_from_chain(es);

    } else {
      KosStackBlock_ReleaseChain(new_stack);
      KosTransferPlan_RESUME_STACK(plan) = NULL;
    }
  }

  if (KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1 && KosEvalState_FRAME(es)) {
    KosEvalState_SET_PC(es, KosTransferPlan_RESUME_PC(plan));
  }

  #if KOS_XFER_TRACE
  if (KosEvalState_FRAME(es)) {
    size_t _install_stack_len = KosEvalState_StackLen(es);
    SAYF("[xfer] INSTALL done: frame_top=%p stack=%p stack_len=%zu resume_pc=%u",
         (void *) KosEvalState_FRAME(es),
         (void *) KosEvalState_StackHead(es),
         _install_stack_len,
         (unsigned) KosTransferPlan_RESUME_PC(plan));
  }
  #endif

  {
    KosDynNode *chain_bottom = seg;
    while (chain_bottom && KosDynNode_PARENT(chain_bottom))
      chain_bottom = KosDynNode_PARENT(chain_bottom);
    KosDynNode *chain_stop = chain_bottom ? KosDynNode_PARENT(chain_bottom) : NULL;
    int wind_r = kos_dyn_run_install_wind_span(es, seg, chain_stop, out_ev,
                                               "INSTALL");
    if (wind_r != 0)
      return wind_r;
  }

  SAYF("[xfer] INSTALL after: dyn_top=%p frame_top=%p",
       (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  return 0;
}


/**
   Execute a PUSH step: append a clone of the segment to the current chain,
   grafting the segment bottom to dyn_top.  Does not free the current chain.
   Restores stack/PC from plan and runs param wind for pushed nodes.
   Returns 0 on success, -1 on error,
   KOS_DYN_UNWIND_ASYNC_STARTED when async wind started.
*/
static int xfer_step_push(KosEvalState *es, KosTransferAction *action,
                          KosEvent *out_ev) {
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
  KosDynNode *template_bottom = template_seg;
  while (template_bottom && KosDynNode_PARENT(template_bottom))
    template_bottom = KosDynNode_PARENT(template_bottom);
  KosDynNode *stop = template_bottom ? KosDynNode_PARENT(template_bottom) : NULL;
  KosDynNode *seg = kos_clone_chain_segment(template_seg, stop);
  if (! seg && template_seg) {
    if (KOS_XFER_TRACE)
      SAY("[xfer] PUSH clone segment failed");
    KosTransferPlan_ACTION_INDEX(plan)--;
    return -1;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] PUSH segment=%p (before: dyn_top=%p frame_top=%p)",
         (void *) seg, (void *) KosEvalState_DYN_TOP(es),
         (void *) KosEvalState_FRAME(es));

  /* Graft bottom of segment to current dyn_top */
  KosDynNode *current_top = KosEvalState_DYN_TOP(es);
  KosDynNode *seg_bottom = seg;
  while (seg_bottom && KosDynNode_PARENT(seg_bottom))
    seg_bottom = KosDynNode_PARENT(seg_bottom);
  if (seg_bottom)
    KosDynNode_PARENT(seg_bottom) = current_top;

  KosEvalState_DYN_TOP(es) = seg;
  kos_dyn_rebuild_live_from_chain(es);

  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock *new_stack = KosTransferPlan_RESUME_STACK(plan);
    KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
    if (! owner && KosEvalState_FRAME(es))
      owner = (KosDynNode *) KosEvalState_FRAME(es);
    if (owner) {
      KosStackBlock *old_stack = KosDynNode_STACK(owner);
      KosDynNode_STACK(owner) = new_stack;
      KosTransferPlan_RESUME_STACK(plan) = NULL;  /* ownership transferred */
      if (old_stack && old_stack != new_stack) {
        KosStackBlock_ReleaseChain(old_stack);
      }
      kos_dyn_rebuild_live_from_chain(es);
    } else {
      KosStackBlock_ReleaseChain(new_stack);
      KosTransferPlan_RESUME_STACK(plan) = NULL;
    }
  }
  if (KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosEvalState_FRAME(es)) {
    KosEvalState_SET_PC(es, KosTransferPlan_RESUME_PC(plan));
  }

  {
    int wind_r = kos_dyn_run_install_wind_span(es, seg, current_top, out_ev,
                                               "PUSH");
    if (wind_r != 0)
      return wind_r;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] PUSH after: dyn_top=%p frame_top=%p",
         (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  return 0;
}


/**
   Execute an INJECT step: deliver the continuation's result to the
   resumed context.  If is_throw, set inject mode and store the value for
   the evaluator to raise; otherwise push the value onto the current
   frame's stack.  We consume our reference to the action's value (the
   plan no longer owns it).  Returns 0 on success, -1 on error (e.g.
   PushRef failure).
*/
static int xfer_step_inject(KosEvalState *es, KosTransferAction *action) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  #if KOS_XFER_TRACE
    size_t _inject_len = KosEvalState_StackLen(es);
    SAYF("[xfer] INJECT frame_top=%p stack=%p stack_len=%zu is_throw=%d",
         (void *) KosEvalState_FRAME(es),
         (void *) KosEvalState_StackHead(es),
         _inject_len,
         KosTransferAction_INJECT_IS_THROW(action));
  #endif
  if (KosTransferAction_INJECT_IS_THROW(action)) {
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_THROW;
    Py_XINCREF(KosTransferAction_INJECT_VALUE(action));
    Py_XDECREF(KosContext_INJECT_VALUE(ctx));
    KosContext_INJECT_VALUE(ctx) = KosTransferAction_INJECT_VALUE(action);
    Py_XDECREF(KosContext_INJECT_TYPE(ctx));
    KosContext_INJECT_TYPE(ctx) = KosTransferAction_INJECT_TYPE(action);
    if (KosContext_INJECT_TYPE(ctx))
      Py_INCREF(KosContext_INJECT_TYPE(ctx));
  } else {
    if (! KosEvalState_FRAME(es)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "INJECT requires a current frame");
      Py_XDECREF(KosTransferAction_INJECT_VALUE(action));
      KosTransferAction_INJECT_VALUE(action) = NULL;
      return -1;
    }
    if (KosEvalState_StackPushRef(es, KosTransferAction_INJECT_VALUE(action))
        < 0) {
      if (KOS_XFER_TRACE)
        SAY("[xfer] INJECT PushRef failed");
      Py_XDECREF(KosTransferAction_INJECT_VALUE(action));
      KosTransferAction_INJECT_VALUE(action) = NULL;
      return -1;
    }
  }
  Py_DECREF(KosTransferAction_INJECT_VALUE(action));
  KosTransferAction_INJECT_VALUE(action) = NULL;
  Py_XDECREF(KosTransferAction_INJECT_TYPE(action));
  KosTransferAction_INJECT_TYPE(action) = NULL;
  return 0;
}


/**
   Execute a POP_ONE step: run unwind for current top, unlink and free.
   Supports async (e.g. WITH exit returning awaitable).
*/
static int xfer_step_pop_one(KosEvalState *es, KosEvent *out_ev) {
  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "POP_ONE: empty chain");
    return -1;
  }
  return kos_dyn_pop_nonbarrier_top(es, KOS_XFER_EVENT_NONE, out_ev);
}


/**
   Execute a TRUNCATE_AND_INJECT step: free from current top through the
   given node (inclusive), set dyn_top to node's parent, then inject value.
   If the plan has RESUME_PC set, updates the new top frame's PC.
*/
static int xfer_step_truncate_and_inject(KosEvalState *es,
                                          KosTransferAction *action,
                                          KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *stop_node = KosTransferAction_TRUNCATE_NODE(action);
  KosDynNode *new_top = stop_node ? KosDynNode_PARENT(stop_node) : NULL;
  KosDynNode *current = KosEvalState_DYN_TOP(es);

  if (! current) {
    PyErr_SetString(PyExc_RuntimeError, "TRUNCATE_AND_INJECT: empty chain");
    return -1;
  }

  if (KOS_XFER_TRACE)
    SAYF("[xfer] TRUNCATE_AND_INJECT: freeing from current=%p to stop_node=%p (inclusive), new_top will be=%p",
         (void *) current, (void *) stop_node, (void *) new_top);

  while (KosEvalState_DYN_TOP(es) && KosEvalState_DYN_TOP(es) != new_top) {
    int pop_res = kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_NONE,
                                                out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
      return KOS_DYN_UNWIND_ASYNC_STARTED;
    if (pop_res < 0)
      return -1;
  }
  if (KosEvalState_DYN_TOP(es) != new_top) {
    PyErr_SetString(PyExc_RuntimeError,
                    "TRUNCATE_AND_INJECT: stop node not reachable");
    return -1;
  }

  if (plan && KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosEvalState_FRAME(es)) {
    KosEvalState_SET_PC(es, KosTransferPlan_RESUME_PC(plan));
  }

  if (KosTransferAction_TRUNCATE_IS_THROW(action)) {
    if (KOS_XFER_TRACE)
      SAY("[xfer] TRUNCATE_AND_INJECT: setting inject mode to THROW");
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_THROW;
    Py_XINCREF(KosTransferAction_TRUNCATE_VALUE(action));
    Py_XDECREF(KosContext_INJECT_VALUE(ctx));
    KosContext_INJECT_VALUE(ctx) = KosTransferAction_TRUNCATE_VALUE(action);
    Py_XDECREF(KosContext_INJECT_TYPE(ctx));
    KosContext_INJECT_TYPE(ctx) = KosTransferAction_TRUNCATE_TYPE(action);
    if (KosContext_INJECT_TYPE(ctx))
      Py_INCREF(KosContext_INJECT_TYPE(ctx));
  } else if (KosEvalState_FRAME(es)) {
    size_t stack_len_before = 0;
    if (KOS_XFER_TRACE) {
      stack_len_before = KosEvalState_StackLen(es);
      SAYF("[xfer] TRUNCATE_AND_INJECT: pushing value %R onto frame=%p stack (len before=%zu)",
           KosTransferAction_TRUNCATE_VALUE(action),
           (void *) KosEvalState_FRAME(es), stack_len_before);
    }
    (void) stack_len_before;
    {
      if (KosEvalState_StackPushRef(es, KosTransferAction_TRUNCATE_VALUE(action))
          < 0) {
        Py_DECREF(KosTransferAction_TRUNCATE_VALUE(action));
        return -1;
      }
    }
    if (KOS_XFER_TRACE) {
      size_t stack_len_after = KosEvalState_StackLen(es);
      SAYF("[xfer] TRUNCATE_AND_INJECT: stack len after=%zu", stack_len_after);
      (void) stack_len_after;
    }
    Py_DECREF(KosTransferAction_TRUNCATE_VALUE(action));
  }
  KosTransferAction_TRUNCATE_VALUE(action) = NULL;
  Py_XDECREF(KosTransferAction_TRUNCATE_TYPE(action));
  KosTransferAction_TRUNCATE_TYPE(action) = NULL;
  return 0;
}


/**
   Run the next action in the context's current transfer plan.  If there
   is no plan or the plan is already complete, returns 1 (done).  Otherwise
   advances the action index, dispatches to the appropriate step handler
   (EXTENT_ACTION, INSTALL, INJECT, POP_ONE, TRUNCATE_AND_INJECT), and
   returns: 0 if more steps remain, 1 if the plan is now complete, -1 on
   error, KOS_XFER_SUSPENDED_AWAIT when async.  POP_ONE requires out_ev.
*/
int kos_run_transfer_step(KosEvalState *es, KosEvent *out_ev) {

  if (! KosEvalState_PLAN(es))
    return 1;  /* no plan, complete */

  if (KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)) >= KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)))
    return 1;  /* plan complete */

  KosTransferAction *action = &KosTransferPlan_ACTIONS(KosEvalState_PLAN(es))[KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es))];
  if (KOS_XFER_TRACE)
    SAYF("[xfer] step %zu: plan=%p actions=%p step_type=%d (INSTALL=%d)",
         (size_t) KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)),
         (void *) KosEvalState_PLAN(es), (void *) KosTransferPlan_ACTIONS(KosEvalState_PLAN(es)),
         (int) KosTransferAction_STEP(action), (int) KOS_XFER_INSTALL);

  switch (KosTransferAction_STEP(action)) {
    case KOS_XFER_EXTENT_ACTION: {
      int res = xfer_step_extent_action(es, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        return KOS_XFER_SUSPENDED_AWAIT;
      }
      if (res < 0) {
        return -1;
      }
      break;
    }

    case KOS_XFER_INSTALL: {
      int res = xfer_step_install(es, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        return KOS_XFER_SUSPENDED_AWAIT;
      }
      if (res < 0) {
        return -1;
      }
      break;
    }

    case KOS_XFER_PUSH: {
      int res = xfer_step_push(es, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        return KOS_XFER_SUSPENDED_AWAIT;
      }
      if (res < 0) {
        return -1;
      }
      break;
    }

    case KOS_XFER_POP_ONE: {
      int res = xfer_step_pop_one(es, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        return KOS_XFER_SUSPENDED_AWAIT;
      }
      if (res < 0) {
        return -1;
      }
      break;
    }

    case KOS_XFER_TRUNCATE_AND_INJECT: {
      int res = xfer_step_truncate_and_inject(es, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        return KOS_XFER_SUSPENDED_AWAIT;
      }
      if (res < 0) {
        return -1;
      }
      break;
    }

    case KOS_XFER_INJECT: {
      int res = xfer_step_inject(es, action);
      if (res < 0) {
        return -1;
      }
      break;
    }
  }

  /* Advance only when step completed normally (no early return -1 or
     KOS_XFER_SUSPENDED_AWAIT).  No SyncFromContext here: each xfer_step_*
     leaves es coherent (kos_dyn_rebuild_live_from_chain on INSTALL/PUSH, wind/unwind
     tails on EXTENT, in-place inject or pop paths that already refreshed). */
  KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es))++;
  if (KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)) >= KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)))
    return 1;

  return 0;  /* more steps remain */
}


/**
   Suspend the current transfer plan for a nested transfer.  Pushes the
   active plan onto plan_stack and clears KosEvalState_PLAN(es).

   Called by kos_invoke_continuation when a continuation is invoked while
   a transfer is already in progress (e.g. from a dynamic-wind thunk).
   After the inner transfer completes, run_transfer_plan_to_completion calls
   kos_resume_transfer_plan and rebuilds the remaining steps via
   kos_build_direct_install_plan.
*/
void kos_suspend_transfer_plan(KosEvalState *es) {

  if (! KosEvalState_PLAN(es))
    return;

  KosTransferPlan_SUSPENDED(KosEvalState_PLAN(es)) = KosEvalState_PLAN_STACK(es);
  KosEvalState_PLAN_STACK(es) = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;
}


/**
   Resume a suspended transfer plan after a nested transfer completes.  Pops
   from plan_stack and restores it as KosEvalState_PLAN(es).  Pairs with
   kos_suspend_transfer_plan.

   Returns 0 on success (or when nothing to resume).
*/
int kos_resume_transfer_plan(KosEvalState *es) {

  if (! KosEvalState_PLAN_STACK(es))
    return 0;  /* nothing to resume */

  KosEvalState_PLAN(es) = KosEvalState_PLAN_STACK(es);
  KosEvalState_PLAN_STACK(es) = KosEvalState_PLAN(es)->suspended;
  KosEvalState_PLAN(es)->suspended = NULL;

  return 0;
}


/**
   Free all suspended transfer plans on plan_stack.
*/
void kos_free_transfer_plan_stack(KosEvalState *es) {

  while (KosEvalState_PLAN_STACK(es)) {
    KosTransferPlan *p = KosEvalState_PLAN_STACK(es);
    KosEvalState_PLAN_STACK(es) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }
}


/* === Continuation invocation === */


/**
   Build and install a transfer plan to invoke k with value or throw: PUSH for
   delimited (caller chain preserved), full plan for undelimited. Sets plan
   resume fields and optional continuation_ref for GC. Callers: callable invoke,
   apply paths.
*/
int kos_invoke_continuation(
    KosEvalState *es,
    KosContinuation *k,
    PyObject *value,
    int is_throw,
    PyObject *continuation_ref) {

  if (! k) {
    PyErr_SetString(PyExc_RuntimeError, "NULL continuation");
    return -1;
  }

  /* For multi-shot, clone the continuation */
  KosContinuation *to_use = k;
  #if KOS_XFER_TRACE
    SAYF("[xfer] kos_invoke_continuation k=%p one_shot=%d captured_chain=%p",
         (void *) k, KosContinuation_ONE_SHOT(k), (void *) KosContinuation_CAPTURED_CHAIN(k));
  #endif
  if (! KosContinuation_ONE_SHOT(k)) {
    to_use = kos_continuation_clone(k);
    #if KOS_XFER_TRACE
      SAYF("[xfer] clone -> to_use=%p", (void *) to_use);
    #endif
    if (! to_use) {
      #if KOS_XFER_TRACE
        SAY("[xfer] clone failed");
      #endif
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation clone failed without PyErr");
      return -1;
    }
  }

  /* Delimited k(value): use PUSH so caller stays on chain; install DELIM
   * marker so POP_PROMPT can route back to callsite without context slots. */
  int is_delimited = (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);
  KosDynNode *push_segment = NULL;
  KosDynDelim *ret_delim = NULL;
  if (is_delimited) {
    KosDynNode *captured = KosContinuation_CAPTURED_CHAIN(to_use);
    /* Segment to push: captured prompt plus body frames/ns nodes. */
    push_segment = captured;
    ret_delim = kos_dyn_push_delim(
        es,
        KOS_DYN_DELIM_MODE_POP_PROMPT,
        NULL,
        KosEvalState_FRAME(es)
            ? KosEvalState_PC(es) : (uint32_t) -1,
        KosEvalState_FRAME(es),
        NULL);
    if (! ret_delim) {
      if (to_use != k)
        kos_continuation_free(to_use);
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError,
                        "failed to push delimited return marker");
      return -1;
    }
    KosDynDelim_RETURN_NODE(ret_delim) = (KosDynNode *) ret_delim;
    if (KOS_XFER_TRACE)
      SAYF("[xfer] invoke: push DELIM dyn_top=%p kind=%s frame_top=%p return_node=%p",
           (void *) KosEvalState_DYN_TOP(es),
           KosEvalState_DYN_TOP(es)
               ? kos_dynnode_kind_name(KosDynNode_KIND(
                     KosEvalState_DYN_TOP(es)))
               : "NULL",
           (void *) KosEvalState_FRAME(es),
           (void *) KosDynDelim_RETURN_NODE(ret_delim));
  }

  /* Build plan: PUSH for delimited; full transfer for undelimited. */
  KosTransferPlan *plan = NULL;
  int res;
  if (is_delimited) {
    #if KOS_XFER_TRACE
      SAYF("[xfer] kos_build_delimited_push_plan (delimited) segment=%p",
           (void *) push_segment);
    #endif
    res = kos_build_delimited_push_plan(es, push_segment, value, is_throw,
                                        &plan);
  } else {
    #if KOS_XFER_TRACE
      SAYF("[xfer] kos_build_transfer_plan current=%p target=%p (template)",
           (void *) KosEvalState_DYN_TOP(es), (void *) to_use->captured_chain);
    #endif
    res = kos_build_transfer_plan(es, to_use->captured_chain,
                                  value, is_throw,
                                  KosContinuation_GRAFT_POINT_ID(to_use),
                                  &plan);
  }
  #if KOS_XFER_TRACE
    SAYF("[xfer] plan build -> res=%d plan=%p", res, (void *) plan);
  #endif
  if (res < 0) {
    if (ret_delim && kos_dyn_pop_delim(es, ret_delim) < 0)
      return -1;
    if (to_use != k)
      kos_continuation_free(to_use);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation plan build failed without PyErr");
    return -1;
  }

  KosTransferPlan_GRAFT_POINT_ID(plan) = KosContinuation_GRAFT_POINT_ID(to_use);
  KosTransferPlan_RESUME_PC(plan) = KosContinuation_RESUME_PC(k);
  KosTransferPlan_RESUME_FRAME(plan) = to_use->resume_frame;  /* template; INSTALL uses fresh */
  /* DeepCopy of template stack for this invoke; released in kos_free_transfer_plan. */
  KosTransferPlan_RESUME_STACK(plan) = to_use->resume_stack
      ? KosStackBlock_DeepCopy(to_use->resume_stack) : NULL;
  #if KOS_XFER_TRACE
    SAYF("[xfer] plan resume_frame=%p resume_stack=%p",
         (void *) KosTransferPlan_RESUME_FRAME(plan), (void *) KosTransferPlan_RESUME_STACK(plan));
  #endif

  /* If a transfer is already in progress (e.g. we were called from a wind
     thunk during an EXTENT_ACTION step), suspend the outer plan so it can be
     replanned and resumed after this inner transfer completes. */
  kos_suspend_transfer_plan(es);
  KosEvalState_ArmTransferPlan(es, plan);
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  if (continuation_ref) {
    Py_INCREF(continuation_ref);
    KosTransferPlan_CONTINUATION_REF(plan) = continuation_ref;
  }

  if (to_use != k) {
    KosTransferPlan_INVOKE_CLONE(plan) = to_use;
    /* Do not release to_use->resume_stack here; that would DECREF the
     * continuation and free k before the plan runs. kos_free_transfer_plan
     * releases it via KosStackBlock_ReleaseChain then frees to_use.
     * The clone retains ownership of captured_chain until
     * xfer_plan_release_invoke_clone frees it. */
  }

  return 0;
}


static int dynnode_to_dict_payload(KosDynNode *node, PyObject *data) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME:
      return dynnode_to_dict_frame(node, data);
    case KOS_DYN_PROMPT:
      return dynnode_to_dict_prompt(node, data);
    case KOS_DYN_DELIM:
      return dynnode_to_dict_delim(node, data);
    case KOS_DYN_WIND:
      return dynnode_to_dict_wind(node, data);
    case KOS_DYN_TRY:
      return dynnode_to_dict_try(node, data);
    case KOS_DYN_FINALLY:
      return dynnode_to_dict_finally(node, data);
    case KOS_DYN_LOOP:
      return dynnode_to_dict_loop(node, data);
    case KOS_DYN_FOREACH:
      return dynnode_to_dict_foreach(node, data);
    case KOS_DYN_NS:
      return dynnode_to_dict_ns(node, data);
    case KOS_DYN_COLLECT:
      return dynnode_to_dict_collect(node, data);
    case KOS_DYN_CC:
      return dynnode_to_dict_cc(node, data);
    case KOS_DYN_YIELD_FROM:
      return dynnode_to_dict_yield_from(node, data);
    case KOS_DYN_PARAM:
      return dynnode_to_dict_param(node, data);
    case KOS_DYN_WITH:
      return dynnode_to_dict_with(node, data);
    case KOS_DYN_SHIFT:
      return dynnode_to_dict_shift(node, data);
    case KOS_DYN_CC_RAISE:
      return dynnode_to_dict_cc_raise(node, data);
    case KOS_DYN_GEN:
      return dynnode_to_dict_gen(node, data);
    case KOS_DYN_NEXT:
      return dynnode_to_dict_next(node, data);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "unknown dynnode kind for snapshot: %u",
                   (unsigned) KosDynNode_KIND(node));
      return -1;
  }
}


static PyObject *dynnode_to_dict_generic(KosDynNode *node) {

  PyObject *out = PyDict_New();
  if (! out)
    return NULL;

  if (snapshot_set_uint(out, "node_id", KosDynNode_NODE_ID(node)) < 0
      || snapshot_set_item(out, "kind",
          PyUnicode_FromString(kos_dynnode_kind_name(KosDynNode_KIND(node)))) < 0
      || snapshot_set_obj(out, "namespace", KosDynNode_NS(node)) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  if (KosDynNode_STACK(node)) {
    PyObject *stack = snapshot_stack_to_list(KosDynNode_STACK(node));
    if (! stack) {
      Py_DECREF(out);
      return NULL;
    }
    if (snapshot_set_item(out, "stack", stack) < 0) {
      Py_DECREF(out);
      return NULL;
    }
  } else if (snapshot_set_obj(out, "stack", NULL) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  PyObject *data = PyDict_New();
  if (! data) {
    Py_DECREF(out);
    return NULL;
  }
  if (snapshot_set_item(out, "data", data) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  if (dynnode_to_dict_payload(node, data) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  return out;
}


PyObject *kos_dynchain_snapshot_to_list(KosDynNode *node) {

  PyObject *out = PyList_New(0);
  if (! out)
    return NULL;

  for (KosDynNode *walk = node; walk; walk = KosDynNode_PARENT(walk)) {
    PyObject *entry = dynnode_to_dict_generic(walk);
    if (! entry || PyList_Append(out, entry) < 0) {
      Py_XDECREF(entry);
      Py_DECREF(out);
      return NULL;
    }
    Py_DECREF(entry);
  }

  return out;
}


/* === GC traversal === */


/**
   GC traverse a single dynamic chain node.  Dispatches on node kind to
   per-kind helpers in dynchain/dyn_*.c.  Used by kos_dynchain_traverse and
   by continuation/plan traversals.
*/
int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg) {
  if (! node)
    return 0;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME:
      return dynnode_traverse_frame(node, visit, arg);
    case KOS_DYN_NS:
      return dynnode_traverse_ns(node, visit, arg);
    case KOS_DYN_PROMPT:
      return dynnode_traverse_prompt(node, visit, arg);
    case KOS_DYN_DELIM:
      return dynnode_traverse_delim(node, visit, arg);
    case KOS_DYN_CC:
      return dynnode_traverse_cc(node, visit, arg);
    case KOS_DYN_SHIFT:
      return dynnode_traverse_shift(node, visit, arg);
    case KOS_DYN_WIND:
      return dynnode_traverse_wind(node, visit, arg);
    case KOS_DYN_CC_RAISE:
      return dynnode_traverse_cc_raise(node, visit, arg);
    case KOS_DYN_GEN:
      return dynnode_traverse_gen(node, visit, arg);
    case KOS_DYN_NEXT:
      return dynnode_traverse_next(node, visit, arg);
    case KOS_DYN_TRY:
      return dynnode_traverse_try(node, visit, arg);
    case KOS_DYN_FINALLY:
      return dynnode_traverse_finally(node, visit, arg);
    case KOS_DYN_LOOP:
      return dynnode_traverse_loop(node, visit, arg);
    case KOS_DYN_FOREACH:
      return dynnode_traverse_foreach(node, visit, arg);
    case KOS_DYN_YIELD_FROM:
      return dynnode_traverse_yield_from(node, visit, arg);
    case KOS_DYN_PARAM:
      return dynnode_traverse_param(node, visit, arg);
    case KOS_DYN_WITH:
      return dynnode_traverse_with(node, visit, arg);
    case KOS_DYN_COLLECT:
      return dynnode_traverse_collect(node, visit, arg);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dynnode_traverse: unknown node kind %u",
                   (unsigned) KosDynNode_KIND(node));
      return -1;
  }
}


/**
   Visit every node from node down to stop (exclusive) with kos_dynnode_traverse.
   Callers: kos_continuation_traverse, GC of captured segments.
*/
int kos_dynchain_traverse(
    KosDynNode *node,
    KosDynNode *stop,
    visitproc visit,
    void *arg) {

  while (node && node != stop) {
    int res = kos_dynnode_traverse(node, visit, arg);
    if (res)
      return res;
    node = KosDynNode_PARENT(node);
  }

  return 0;
}


/**
   GC: visit token ref and captured chain. Used by continuation wrapper type.
*/
int kos_continuation_traverse(
    KosContinuation *k,
    visitproc visit,
    void *arg) {

  if (! k)
    return 0;

  Py_VISIT(KosContinuation_TOKEN_REF(k));
  return kos_dynchain_traverse(KosContinuation_CAPTURED_CHAIN(k), NULL, visit, arg);
}


/**
   GC: visit continuation ref, step awaitables, and pending INJECT/TRUNCATE
   payload objects in unfinished plan actions.
*/
int kos_transferplan_traverse(
    KosTransferPlan *plan,
    visitproc visit,
    void *arg) {

  if (! plan)
    return 0;

  Py_VISIT(KosTransferPlan_CONTINUATION_REF(plan));
  Py_VISIT(KosTransferPlan_STEP_AWAITABLE(plan));
  Py_VISIT(KosTransferPlan_STEP_RESUME_INJECT(plan));

  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[i];
    if (KosTransferAction_STEP(a) == KOS_XFER_INJECT) {
      Py_VISIT(KosTransferAction_INJECT_VALUE(a));
      Py_VISIT(KosTransferAction_INJECT_TYPE(a));
    } else if (KosTransferAction_STEP(a) == KOS_XFER_TRUNCATE_AND_INJECT) {
      Py_VISIT(KosTransferAction_TRUNCATE_VALUE(a));
      Py_VISIT(KosTransferAction_TRUNCATE_TYPE(a));
    }
  }

  return 0;
}


/**
   After POP_PROMPT on delimited invoke return path: push value on saved frame,
   restore PC, and remove the DELIM marker node.
*/
int kos_dyn_finish_delimited_pop_return(
    KosEvalState *es,
    KosDynDelim *delim,
    PyObject *value) {

  KosDynFrame *ret_frame;

  if (! es) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_delimited_pop_return: eval state is NULL");
    Py_XDECREF(value);
    return -1;
  }
  if (! value) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_delimited_pop_return: value is NULL");
    return -1;
  }
  if (! delim || ! KosDynNode_IS_DELIM((KosDynNode *) delim)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_delimited_pop_return: delim is NULL");
    Py_DECREF(value);
    return -1;
  }
  ret_frame = KosDynDelim_RETURN_FRAME(delim);
  if (! ret_frame) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_delimited_pop_return: "
                    "delimited_return_frame is NULL");
    Py_DECREF(value);
    return -1;
  }
  if (KosDynDelim_RETURN_PC(delim) == (uint32_t) -1) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_delimited_pop_return: "
                    "delimited_return_pc is unset");
    Py_DECREF(value);
    return -1;
  }

  /* Operand stack for the handler lives below the DELIM marker; do not use
   * ctx->block (walk parent chain). */
  {
    KosStackBlock **slot = NULL;
    KosDynNode *n = KosDynNode_PARENT((KosDynNode *) delim);
    while (n) {
      if (KosDynNode_STACK(n)) {
        slot = &KosDynNode_STACK(n);
        break;
      }
      n = KosDynNode_PARENT(n);
    }
    if (! slot)
      slot = &KosDynNode_STACK((KosDynNode *) ret_frame);
    if (KosStackBlock_PushRef(slot, value) < 0) {
      Py_DECREF(value);
      return -1;
    }
  }
  Py_DECREF(value);
  KosDynFrame_PC(ret_frame) = KosDynDelim_RETURN_PC(delim);
  if (KosEvalState_DYN_TOP(es) == (KosDynNode *) delim) {
    if (kos_dyn_pop_delim(es, delim) < 0)
      return -1;
    if (KosEvalState_FRAME(es) == ret_frame)
      kos_dyn_load_live_regs_from_frame(es, ret_frame);
    else
      kos_dyn_rebuild_live_from_chain(es);
  } else {
    /* DELIM not dyn_top: pop already done; full rebuild for live coherence. */
    kos_dyn_rebuild_live_from_chain(es);
  }
  return 0;
}



/**
   Shared path for KOS_EV_RETURN delivery to KOS_DYN_NEXT or awaiting
   KOS_DYN_FOREACH after a flattened Koskari async generator completes.
   Expects KosEvent_KIND(out_ev) == KOS_EV_RETURN and payload set.

   :return: same as kos_dyn_next_deliver_return / foreach deliver (0 success,
      -1 error; next may leave no PyErr for REPROLOGUE)
*/
int kos_dyn_deliver_iterator_boundary_return_event(
    KosEvalState *es,
    struct KosEvent *out_ev) {

  // TODO: this is bad, it hard-coded specific node types. It should use
  // a capability-based approach.

  KosDynNext *next = kos_dyn_find_next_above_frame(es);

  if (next)
    return kos_dyn_next_deliver_return(es, out_ev);

  KosDynForeach *fe = kos_dyn_find_awaiting_foreach(es);
  if (fe)
    return kos_dyn_foreach_deliver_return_kos_agen(es, fe, out_ev);

  KosDynYieldFrom *yf = kos_dyn_find_yield_from_iter_boundary(es);
  if (yf)
    return kos_dyn_yield_from_deliver_inner_return(es, yf, out_ev);

  PyErr_SetString(PyExc_RuntimeError,
                  "iterator boundary RETURN without NEXT/FOREACH/YIELD_FROM "
                  "Koskari agen state");
  return -1;
}


/**
 Delivers KOS_EV_YIELD to the first matching iterator boundary, in the same
 order as kos_dyn_deliver_iterator_boundary_return_event: NEXT, then
 FOREACH (Koskari agen await), then YIELD_FROM.

 :return: 0 if NEXT or FOREACH delivered (caller should REPROLOGUE);
          1 if YIELD_FROM delivered (caller sets KOS_SUSP_YIELD and returns
          STOP to the outer driver);
          -1 on error
*/
int kos_dyn_deliver_yield_to_iter_boundary(
    KosEvalState *es,
    struct KosEvent *out_ev) {

  // TODO: this is bad, it hard-coded specific node types. It should use
  // a capability-based approach.

  KosDynNext *next = kos_dyn_find_next_above_frame(es);

  if (next) {
    if (kos_dyn_next_deliver_yield(es, out_ev) < 0)
      return -1;
    return 0;
  }

  KosDynForeach *fe = kos_dyn_find_awaiting_foreach(es);
  if (fe) {
    if (kos_dyn_foreach_deliver_yield_kos_agen(es, fe, out_ev) < 0)
      return -1;
    return 0;
  }

  KosDynYieldFrom *yf = kos_dyn_find_yield_from_iter_boundary(es);
  if (yf) {
    if (kos_dyn_yield_from_deliver_inner_yield(es, yf, out_ev) < 0)
      return -1;
    return 1;
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "iterator boundary YIELD without NEXT/FOREACH/YIELD_FROM "
                  "Koskari agen state");
  return -1;
}


/**
 After kos_dyn_pop_frame from a callee: if a flattened Koskari async-gen
 consumer (NEXT or ITER_NEXT) is active, deliver the return value using the
 same logic as eval dispatch_boundary_event for KOS_EV_RETURN.

 On success or error paths that consume the value, sets *p_val to NULL.
 On return 1, *p_val is unchanged and the caller must deliver to the parent
 frame (native handler or stack push).

 :return: 0 if delivery ran (continue eval); -1 if POST_ERR; 1 if no
    boundary consumer for this return
*/
int kos_dyn_try_deliver_iterator_boundary_after_pop(
    KosEvalState *es,
    PyObject **p_val,
    struct KosEvent *out_ev) {

  PyObject *val;

  if (! kos_dyn_need_kos_agen_boundary_deliver(es)) {
    return 1;
  }

  val = *p_val;
  KosEvent_KIND(out_ev) = KOS_EV_RETURN;
  KosEvent_PAYLOAD(out_ev) = val;

  int dr = kos_dyn_deliver_iterator_boundary_return_event(es, out_ev);
  if (dr < 0) {
    if (PyErr_Occurred()) {
      *p_val = NULL;
      return -1;
    }
    *p_val = NULL;
    return 0;
  }

  *p_val = NULL;
  return 0;
}


/* === Unit lifecycle === */


/**
   Module init: param sentinel singleton for parameterize unset markers.
*/
int koskari_dynchain_init(PyObject *mod) {
  (void) mod;

  if (kos_dynyieldfrom_strings_init() < 0)
    return -1;

  Py_CLEAR(_param_sentinel_unset);
  _param_sentinel_unset = Py_BuildValue("(s)", "<param was unset>");
  if (! _param_sentinel_unset)
    return -1;
  return 0;
}


/**
   Interpreter shutdown: clear sentinel, drain dyn_node and continuation
   freelists, release cached transfer plan buffer.
*/
int koskari_dynchain_clear(PyObject *mod) {
  (void) mod;

  kos_dynyieldfrom_strings_clear();
  Py_CLEAR(_param_sentinel_unset);

  while (dyn_freelist) {
    KosDynNode *next = KosDynNode_PARENT(dyn_freelist);
    PyMem_Free(dyn_freelist);
    dyn_freelist = next;
  }
  dyn_freecount = 0;

  while (cont_freelist) {
    KosContinuation *next = (KosContinuation *) KosContinuation_CAPTURED_CHAIN(cont_freelist);
    PyMem_Free(cont_freelist);
    cont_freelist = next;
  }
  cont_freecount = 0;

  if (xfer_plan_cache) {
    PyMem_Free(xfer_plan_cache);
    xfer_plan_cache = NULL;
  }
  return 0;
}


// The end.
