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
*/


#include "lib.h"
#include "debug.h"
#include "tracing.h"
#include "hints.h"

#include <stdint.h>


/* === Forward declarations === */


static KosOpResult kos_dyn_uninstall_handler(KosEvalState *es, KosDynNode *node,
                                             KosXferEvent event);
int kos_dyn_push(KosEvalState *es, KosDynNode *node);
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node);
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top);

static void kos_dyn_apply_live(
    KosEvalState *es,
    KosDynFrame *ft,
    KosStackBlock **bl,
    PyObject *ns,
    int flush);
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
   Mint a fresh, never-zero node id. Every allocation gets one; clones for
   continuation capture overwrite it with the original's id for LCA
   matching. Prompt instantiation at k-invoke time mints anew so a grafted
   prompt is a distinct instance from the live original.
*/
static uint32_t kos_dynnode_mint_id(void) {
  uint32_t id = dyn_node_id_counter++;
  if (! dyn_node_id_counter)
    dyn_node_id_counter = 1;  /* avoid 0 on rollover */
  return id;
}


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
    p->node_id = kos_dynnode_mint_id();
    return p;
  }

  KOS_TRACE_FL_ALLOC("dyn_node", 0);
  p = PyMem_Malloc(sizeof(KosDynNode));
  if (! p) {
    PyErr_NoMemory();
    return NULL;
  }

  memset(p, 0, sizeof(KosDynNode));
  p->node_id = kos_dynnode_mint_id();

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


/* === Context exception stash (parked propagation state) === */


/**
   Chain a superseded exception triple as __context__ of the superseding
   one (Python raise-during-handling semantics). Normalizes both triples;
   consumes the old triple's references. The new triple is left owned by
   the caller (normalized in place). An existing __context__ on the new
   value is preserved.
*/
static void kos_exc_supersede_chain(
    PyObject **new_typ, PyObject **new_val, PyObject **new_tb,
    PyObject *old_typ, PyObject *old_val, PyObject *old_tb) {

  PyErr_NormalizeException(new_typ, new_val, new_tb);
  PyErr_NormalizeException(&old_typ, &old_val, &old_tb);

  if (old_val && old_tb)
    PyException_SetTraceback(old_val, old_tb);

  if (*new_val && old_val) {
    PyObject *existing = PyException_GetContext(*new_val);
    if (existing) {
      Py_DECREF(existing);
      Py_DECREF(old_val);
    } else {
      /* steals the old_val reference */
      PyException_SetContext(*new_val, old_val);
    }
  } else {
    Py_XDECREF(old_val);
  }

  Py_XDECREF(old_typ);
  Py_XDECREF(old_tb);
}


/**
   Pop the top stash entry. Returns the owned entry tuple (exc, node_id)
   with *out_id set, or NULL when the stash is empty.
*/
static PyObject *exc_stash_pop_entry(KosContext *ctx, uint32_t *out_id) {

  PyObject *stash = KosContext_EXC_STASH(ctx);
  Py_ssize_t n = stash ? PyList_GET_SIZE(stash) : 0;
  PyObject *entry;

  if (! n)
    return NULL;

  entry = PyList_GET_ITEM(stash, n - 1);
  Py_INCREF(entry);
  if (PyList_SetSlice(stash, n - 1, n, NULL) < 0) {
    Py_DECREF(entry);
    return NULL;
  }
  *out_id = (uint32_t) PyLong_AsUnsignedLong(PyTuple_GET_ITEM(entry, 1));
  return entry;
}


/**
   Park an exception triple on the context stash, tagged with the parking
   node's id. Normalizes the triple and attaches the traceback to the
   instance; consumes all three references. See dynchain.h.
*/
int kos_ctx_exc_stash_push(
    KosEvalState *es,
    uint32_t node_id,
    PyObject *typ,
    PyObject *val,
    PyObject *tb) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *entry = NULL;
  PyObject *id_obj = NULL;

  PyErr_NormalizeException(&typ, &val, &tb);
  if (! val) {
    Py_XDECREF(typ);
    Py_XDECREF(tb);
    PyErr_SetString(PyExc_SystemError,
                    "exc_stash_push: no exception value");
    return -1;
  }
  if (tb)
    PyException_SetTraceback(val, tb);
  Py_XDECREF(typ);
  Py_XDECREF(tb);

  if (! KosContext_EXC_STASH(ctx)) {
    KosContext_EXC_STASH(ctx) = PyList_New(0);
    if (! KosContext_EXC_STASH(ctx)) {
      Py_DECREF(val);
      return -1;
    }
  }

  id_obj = PyLong_FromUnsignedLong((unsigned long) node_id);
  if (id_obj)
    entry = PyTuple_Pack(2, val, id_obj);
  Py_XDECREF(id_obj);
  Py_DECREF(val);
  if (! entry)
    return -1;

  {
    int r = PyList_Append(KosContext_EXC_STASH(ctx), entry);
    Py_DECREF(entry);
    return r;
  }
}


/**
   True when the stash top entry was parked by node_id.
*/
int kos_ctx_exc_stash_top_is(KosEvalState *es, uint32_t node_id) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *stash = KosContext_EXC_STASH(ctx);
  Py_ssize_t n = stash ? PyList_GET_SIZE(stash) : 0;

  if (! n)
    return 0;
  {
    PyObject *entry = PyList_GET_ITEM(stash, n - 1);
    return (uint32_t) PyLong_AsUnsignedLong(PyTuple_GET_ITEM(entry, 1))
        == node_id;
  }
}


/**
   Pop the top parked exception and restore it into PyErr to continue
   propagation. A node_id mismatch is loudly traced but still pops (the
   alternative leaks the entry). See dynchain.h.
*/
int kos_ctx_exc_stash_reraise(KosEvalState *es, uint32_t node_id) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  uint32_t got_id = 0;
  PyObject *entry = exc_stash_pop_entry(ctx, &got_id);

  if (! entry) {
    PyErr_SetString(PyExc_SystemError,
                    "exc_stash_reraise: stash is empty");
    return -1;
  }
  if (got_id != node_id)
    SAYF("[eval] exc_stash_reraise: node_id mismatch (parked=%u, "
         "resumer=%u)\n", (unsigned) got_id, (unsigned) node_id);

  {
    PyObject *exc = PyTuple_GET_ITEM(entry, 0);
    PyObject *tb = PyException_GetTraceback(exc);
    Py_INCREF(exc);
    PyErr_Restore(Py_NewRef((PyObject *) Py_TYPE(exc)), exc, tb);
  }
  Py_DECREF(entry);
  return 0;
}


/**
   Pop the top parked exception and chain it as __context__ of the
   superseding triple (caller-owned, normalized in place). Used when a
   teardown's own exception replaces the one it was parked for. No-op on
   an empty stash.
*/
int kos_ctx_exc_stash_chain_top(
    KosEvalState *es,
    uint32_t node_id,
    PyObject **typ,
    PyObject **val,
    PyObject **tb) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  uint32_t got_id = 0;
  PyObject *entry = exc_stash_pop_entry(ctx, &got_id);

  if (! entry)
    return 0;
  if (got_id != node_id)
    SAYF("[eval] exc_stash_chain_top: node_id mismatch (parked=%u, "
         "superseder=%u)\n", (unsigned) got_id, (unsigned) node_id);

  {
    PyObject *exc = PyTuple_GET_ITEM(entry, 0);
    Py_INCREF(exc);
    kos_exc_supersede_chain(typ, val, tb,
                            Py_NewRef((PyObject *) Py_TYPE(exc)),
                            exc,
                            PyException_GetTraceback(exc));
  }
  Py_DECREF(entry);
  return 0;
}


/**
   Guard a kos_dyn_pop_* teardown against a foreign node. Being at the
   top of the chain is not enough: a node left behind by a mishandled
   transfer satisfies that check and is then torn down as the expected
   kind -- freeing fields it does not have (e.g. Py_DECREF of an absent
   namespace) instead of reporting the topology fault. Names the kind
   actually on top and fails the pop. A macro, not a helper: these sit
   on every dynchain pop.
*/
#define KOS_DYN_POP_REQUIRE_KIND(node, kind, who, want)                 \
  do {                                                                  \
    if (KosDynNode_KIND((KosDynNode *) (node)) != (kind)) {             \
      PyErr_Format(PyExc_RuntimeError,                                  \
                   who ": top of chain is %s, not " want,               \
                   kos_dynnode_kind_name(                               \
                       KosDynNode_KIND((KosDynNode *) (node))));        \
      return -1;                                                        \
    }                                                                   \
  } while (0)


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
#include "dynchain/dyn_boundary.c"


/**
   Dispatch post-await resume: WITH phases, WIND phases. Callers: eval after
   KosEvalState_CallArgs returns.
*/
KosOpResult kos_dynnode_on_control_returned(
    KosEvalState *es,
    KosDynNode *node) {

  int r = 0;
  /* Iterator-boundary yield/return uses eval dispatch_boundary_event /
     kos_dyn_deliver_* instead of this hook. */
  /* When node is a frame, check if parent is WITH in pending phase. */
  if (KosDynNode_IS_FRAME(node)) {
    KosDynNode *parent = KosDynNode_PARENT(node);
    if (parent && KosDynNode_IS_WITH(parent)) {
      uint8_t phase = KosDynWith_PHASE((KosDynWith *) parent);
      if (phase == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        r = kos_dyn_with_on_control_returned(es, (KosDynWith *) parent, 1);
      else if (phase == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
        r = kos_dyn_with_on_enter_control_returned(es, (KosDynWith *) parent);
    }
  } else {
    switch (KosDynNode_KIND(node)) {
      case KOS_DYN_WIND:
        r = kos_dyn_wind_on_control_returned(es, (KosDynWind *) node);
        break;
      case KOS_DYN_WITH:
        /* An installed __enter__/__exit__ frame has already popped by the
           time we get here, so the WITH node itself is current; the frame
           branch above only sees the await-resume shape. */
        if (KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
          r = kos_dyn_with_on_control_returned(es, (KosDynWith *) node, 1);
        else if (KosDynWith_PHASE((KosDynWith *) node)
                 == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
          r = kos_dyn_with_on_enter_control_returned(es, (KosDynWith *) node);
        break;
      default:
        break;
    }
  }
  return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
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
   kos_dyn_uninstall_core, kos_dynchain_free, error paths after failed push.
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
   Free nodes from node toward the root, stopping before stop (stop is not
   freed). Used to tear down a suffix of chain or a cloned template.
*/
void kos_dynchain_free(KosDynNode *node, const KosDynNode *stop) {

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
KosDynNode *kos_clone_chain_segment(
    KosDynNode *top,
    KosDynNode *stop) {

  SAYF("[xfer] clone_segment enter top=%p stop=%p", (void *) top, (void *) stop);
  if (! top || top == stop)
    return NULL;

  /* Inner-to-outer clone; link cloned_prev->parent = next older clone */
  KosDynNode *cloned_top = NULL;
  KosDynNode *cloned_prev = NULL;

  KosDynNode *node = top;
  while (node && node != stop) {
    /* An INPLACE delim is transient invoke bookkeeping -- it marks that a
       resume is in flight against a live prompt -- not part of the
       delimited region a template represents. Cloning one puts an
       invoke marker into a segment whose whole purpose is to be
       instantiated fresh per invoke, and the copy then competes with the
       live original for POP_PROMPT's attention. Link around it. */
    if (KosDynNode_IS_DELIM(node)
        && KosDynDelim_MODE((KosDynDelim *) node)
           == KOS_DYN_DELIM_MODE_INPLACE) {
      if (KOS_XFER_TRACE)
        SAYF("[xfer] clone_segment skipping INPLACE delim %p",
             (void *) node);
      node = KosDynNode_PARENT(node);
      continue;
    }
    if (KOS_XFER_TRACE)
      SAYF("[xfer] clone_segment cloning node %p kind=%u",
           (void *) node, (unsigned) KosDynNode_KIND(node));
    KosDynNode *cloned = kos_dyn_clone(node);
    if (! cloned) {
      SAYF("[xfer] clone_segment kos_dyn_clone failed node=%p kind=%u pyerr=%d",
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
   frame pointer changes and flush is non-zero, flushes live pc and acting_pc
   onto the outgoing frame before loading code and PCs from the new frame.
   Pass flush=0 when the outgoing frame is about to be freed (return path) so
   the write is skipped. When only block or namespace change, updates those
   slots without clobbering live PCs.
*/
static void kos_dyn_apply_live(
    KosEvalState *es,
    KosDynFrame *ft,
    KosStackBlock **bl,
    PyObject *ns,
    int flush) {

  if (! es)
    return;

  KosDynFrame *const ef = KosEvalState_FRAME(es);
  if (ef != ft) {
    if (ef && flush)
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
static void kos_dyn_load_live_regs_from_frame(
    KosEvalState *es,
    KosDynFrame *ft) {

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
   only the slots they carry. Passes flush=1 to kos_dyn_apply_live so the
   outgoing frame's PCs are saved before the new frame is loaded. Do not use
   after splices, gen install, or
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

  kos_dyn_apply_live(es, ft, bl, ns, 1);

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
   dyn_top as removed and the new head as new_top. Passes flush=0 to
   kos_dyn_apply_live — the outgoing frame's PCs were already saved when it was
   pushed, and on the return path the frame is about to be freed. When new_top
   does not equal parent(removed) (splice/gen replace), delegates to
   kos_dyn_rebuild_live_from_chain. For a normal single-head pop, updates only
   the live slots the removed node owned; returns
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
                       KosEvalState_NAMESPACE(es), 0);
    return;
  }

  kos_dyn_walk_live_from(es, new_top, &ft, &bl, &ns);

  if (! need_frame)
    ft = KosEvalState_FRAME(es);
  if (! need_block)
    bl = KosEvalState_BLOCK(es);
  if (! need_ns)
    ns = KosEvalState_NAMESPACE(es);

  kos_dyn_apply_live(es, ft, bl, ns, 0);
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

  kos_dyn_apply_live(es, ft, bl, ns, 1);

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

  if (kos_dyn_install(es, node) < 0) {
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
   kos_dyn_uninstall_core, fast kos_dyn_pop_* paths, gen install, and manual
   splices.
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


static KosOpResult kos_dyn_uninstall_core(
    KosEvalState *es,
    KosDynNode *node,
    KosXferEvent event) {

  KosOpResult kr = kos_dyn_uninstall_handler(es, node, event);
  if (kr.kind != KOS_RES_NORMAL)
    return kr;

  kos_dyn_commit_dyn_top(es, KosDynNode_PARENT(node));
  KosDynNode_PARENT(node) = NULL;
  kos_dynnode_free(node);
  return EVAL_NORMAL();
}


/**
   Map a pop/wind KosOpResult to the int unwind protocol: 0 on normal,
   KOS_DYN_UNWIND_ASYNC_STARTED on AWAIT (releases r.val) or REPROLOGUE,
   -1 on anything else (RAISE, FATAL).
*/
static inline int kos_map_pop_result(KosOpResult r) {
  if (r.kind == KOS_RES_AWAIT) {
    Py_XDECREF(r.val);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }
  if (r.kind == KOS_RES_REPROLOGUE)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  return (r.kind == KOS_RES_NORMAL) ? 0 : -1;
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


/* === Generic dispatch: switch-based per-kind handlers === */


/**
   Run install-phase setup for a node that was just pushed.  Only PARAM has
   an install action.  WITH wind (enter) is invoked explicitly by opcodes or
   the transfer plan after push, never here; PROMPT is a barrier only (the
   operand stack stays on the frame below, and SHIFT inherits it via
   KosDynNode_STACK == NULL).

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
int kos_dyn_install(KosEvalState *es, KosDynNode *node) {
  if (KosDynNode_KIND(node) == KOS_DYN_PARAM)
    return kos_dynparam_enter(KosEvalState_CONTEXT(es), (KosDynParam *) node);
  return 0;
}


/**
   Run the pop handler for a node being popped off the chain.  Types with
   no pop action return EVAL_NORMAL() immediately via the default case.

   :return: EVAL_NORMAL on success, EVAL_RAISE on error,
     EVAL_AWAIT or EVAL_REPROLOGUE when async started
*/
static KosOpResult kos_dyn_uninstall_handler(KosEvalState *es, KosDynNode *node,
                                              KosXferEvent event) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
    case KOS_DYN_WITH:
      return kos_dyn_unwind(es, node, event);
    case KOS_DYN_PARAM:
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      return EVAL_NORMAL();
    case KOS_DYN_YIELD_FROM:
      kos_dyn_yieldfrom_unwind(es, (KosDynYieldFrom *) node);
      return EVAL_NORMAL();
    default:
      return EVAL_NORMAL();
  }
}


/**
   Run wind (enter) for an extent node during continuation transfer.
   Only called for kinds that have wind semantics; returns error for
   others.

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
KosOpResult kos_dyn_wind(KosEvalState *es, KosDynNode *node) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PARAM: {
      int r = kos_dyn_param_wind(es, (KosDynParam *) node);
      return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
    }
    case KOS_DYN_WITH:
      return kos_dyn_dyn_with_wind(es, (KosDynWith *) node);
    case KOS_DYN_WIND: {
      int r = kos_dyn_wind_wind(es, (KosDynWind *) node);
      if (r == KOS_DYN_UNWIND_ASYNC_STARTED) return EVAL_REPROLOGUE();
      return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
    }
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_wind: node kind %u has no wind",
                   (unsigned) KosDynNode_KIND(node));
      return EVAL_RAISE();
  }
}


/**
   Run unwind (exit) for an extent node.  May start async for WITH.

   :param event: transfer event; nodes can distinguish RETURN, RAISE, etc.
   :return: EVAL_NORMAL on success, EVAL_RAISE on error,
     EVAL_AWAIT when WITH __exit__ returned awaitable,
     EVAL_REPROLOGUE when WITH swallowed the exception
*/
KosOpResult kos_dyn_unwind(KosEvalState *es, KosDynNode *node,
                            KosXferEvent event) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_DELIM:
      return EVAL_NORMAL();
    case KOS_DYN_SHIFT:
      return EVAL_NORMAL();
    case KOS_DYN_PARAM:
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      return EVAL_NORMAL();
    case KOS_DYN_WIND: {
      int r = kos_dyn_wind_unwind(es, (KosDynWind *) node);
      return (r < 0) ? EVAL_RAISE()
           : (r == KOS_DYN_UNWIND_ASYNC_STARTED) ? EVAL_REPROLOGUE()
           : EVAL_NORMAL();
    }
    case KOS_DYN_WITH: {
      KosOpResult wr = kos_dyn_with_unwind(es, (KosDynWith *) node);
      /* REPROLOGUE: exception swallowed; resume_pc set and Py_None pushed to
         parent stack by dyn_with_unwind.  Map to NORMAL so kos_dyn_uninstall_core
         pops the WITH node and commits the stack — then kos_dyn_try_local_raise_body's
         top_may_swallow check returns REPROLOGUE to restart past POP_WITH.
         A node left in PENDING_EXIT_AWAIT means the opposite: __exit__ has
         not run yet (installed frame), so the node must survive for
         kos_dynnode_on_control_returned to complete and pop it. */
      if (wr.kind == KOS_RES_REPROLOGUE
          && KosDynWith_PHASE((KosDynWith *) node)
             != KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        return EVAL_NORMAL();
      return wr;
    }
    case KOS_DYN_CC_RAISE: {
      int r = cc_raise_unwind(ctx, node);
      return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
    }
    case KOS_DYN_GEN:
      return EVAL_NORMAL();
    case KOS_DYN_NEXT:
      return EVAL_NORMAL();
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_unwind: node kind %u has no unwind",
                   (unsigned) KosDynNode_KIND(node));
      return EVAL_RAISE();
  }
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
                               KosXferEvent event) {

  KOS_TRACE_EVENTS_INC(KOS_TE_DYN_HANDLE_ENTER);

  /* A receiving iterator boundary interprets the step-ending exception
     from its in-flight step as end-of-iteration -- StopIteration from a
     flattened generator, StopAsyncIteration from a suspended python
     async iterator -- the same protocol judgment the eager step path
     makes when classifying GET_NEXT_EXHAUSTED. The capability is the
     routing key and is held exactly while a step is in flight, so the
     same exception raised by the consumer's own body (boundary idle)
     passes through. */
  if (event == KOS_XFER_EVENT_RAISE
      && KosDynNode_HAS_CAP(node, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)
      && PyErr_Occurred()
      && PyErr_ExceptionMatches(kos_dyn_iter_boundary_exhaustion_exc(node)))
    return KOS_HANDLE_CONSUMED;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_TRY:
      if (event == KOS_XFER_EVENT_RAISE) {
        /* Node-authoritative: a live (enabled) TRY consumes; its
           handler pc was fixed from the extent table at push time. */
        int en = KosDynTry_ENABLED((KosDynTry *) node);
        int py = PyErr_Occurred() ? 1 : 0;
        KosDynFrame *frame = KosEvalState_FRAME(es);
        if (! en || ! py || ! frame) {
          SAYF("[xfer] kos_dyn_handle TRY RAISE skip enabled=%d pyerr=%d frame=%p\n",
               en, py, (void *) frame);
          return KOS_HANDLE_PASS;
        }
        return KOS_HANDLE_CONSUMED;
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
      if (event == KOS_XFER_EVENT_YIELD
          || event == KOS_XFER_EVENT_RETURN
          || event == KOS_XFER_EVENT_RAISE)
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

  /* Node-authoritative: the handler pc was fixed from the extent table
     at push time; no runtime extent lookup. */
  (void) es;
  if (KosDynNode_IS_TRY(region))
    return KosDynTry_HANDLER_PC((KosDynTry *) region);
  return KosDynFinally_HANDLER_PC((KosDynFinally *) region);
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


#include "dynchain/dyn_walk.c"


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

   This does NOT close the delegate. It is the completion path: callers
   reach here once the delegate has finished on its own (raised
   StopIteration / StopAsyncIteration) or has already been closed/thrown
   explicitly. Per PEP 380, an exhausted subiterator must not be closed
   again -- and for a Python async generator, calling aclose() here would
   produce an un-awaited coroutine. Forced-teardown callers (the outer
   generator being closed while the delegate is still active) must call
   kos_yieldfrom_close_delegate themselves before popping.
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
   Ensure target lies on the chain from dyn_top; errmsg names the miss.
*/
static int kos_chain_contains(
    KosEvalState *es,
    const KosDynNode *target,
    const char *errmsg) {

  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node && node != target) {
    node = KosDynNode_PARENT(node);
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, errmsg);
    return -1;
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
  return kos_chain_contains(es, (KosDynNode *) cc,
                            "CC block not found in chain");
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
   pc/stack, graft id, one-shot flag, token ref. Callers: kos_shift_capture,
   kos_capture_cc, kos_continuation_clone.
*/
static KosContinuation *kos_continuation_create(
    KosDynNode *captured_chain,
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
   Clear the parent link at the bottom of a cloned template chain.

   kos_clone_chain_segment leaves the clone's innermost parent pointing
   at stop -- a node on the live chain -- so the seam must be severed
   here or the template stays attached to storage it does not own:
   freeing or traversing the captured chain would then run past the
   seam into the live context (or into already-freed nodes once the
   live chain unwinds). Walk only within the clone: stop at the first
   parent link that leaves the segment.
*/
static inline void kos_chain_detach_template(
    KosDynNode *chain,
    const KosDynNode *stop) {

  KosDynNode *bottom = chain;

  if (! bottom)
    return;
  while (KosDynNode_PARENT(bottom)
         && KosDynNode_PARENT(bottom) != stop)
    bottom = KosDynNode_PARENT(bottom);
  KosDynNode_PARENT(bottom) = NULL;
}


/**
   Innermost FRAME on a cloned chain segment (dyn_top downward).
*/
static inline KosDynFrame *kos_chain_first_frame(KosDynNode *chain) {

  KosDynNode *node = chain;

  while (node) {
    if (KosDynNode_IS_FRAME(node))
      return (KosDynFrame *) node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   Operand stack for resume: frame stack or innermost stack owner on chain.
*/
static inline KosStackBlock *kos_chain_resume_stack(
    KosDynNode *chain,
    KosDynFrame *frame) {

  /* The block that was current at the capture point: the innermost node
     on the captured chain owning one. A PROMPT owns the delimited
     region's block, so for a shift capture this is the region's operands
     and nothing else. Preferring the frame's block instead would sweep in
     operands belonging to an enclosing pending expression -- and would
     also miss the region's own, leaving the resumed slice short. Frames
     own blocks too, so the walk terminates there when no region node
     above the frame owns one. */
  KosDynNode *node = chain;

  while (node && ! KosDynNode_STACK(node))
    node = KosDynNode_PARENT(node);
  if (node)
    return KosDynNode_STACK(node);

  return KosDynFrame_STACK(frame);
}


/**
   Clone segment_top..stop, detach template, wire KosContinuation. Caller
   supplies prompt_id, graft_point_id, one_shot, and no_frame_err text.

   baseline_src, when non-NULL, is cloned and appended at the template
   bottom, below the prompt clone; baseline_ns, when non-NULL, replaces
   that clone's namespace with the one live at the capture bound. It is the compressed region bottom for
   a capture bounded at an invocation prompt: the frame executing the
   region's own code lies below the capture boundary and must not be
   captured in place, but the graft needs its code, namespace, and
   mid-region pc so a return falling through the invocation prompt lands
   in the copy and runs the remaining region bytecode to POP_PROMPT.
*/
static KosContinuation *kos_continuation_capture_segment(
    KosEvalState *es,
    KosDynNode *segment_top,
    KosDynNode *stop,
    uint32_t resume_pc,
    uint32_t prompt_id,
    uint32_t graft_point_id,
    int one_shot,
    KosDynFrame *baseline_src,
    PyObject *baseline_ns,
    const char *no_frame_err) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *captured_chain;
  KosDynFrame *resume_frame;
  KosStackBlock *resume_stack;
  KosContinuation *k;
  PyObject *token;

  captured_chain = kos_clone_chain_segment(segment_top, stop);
  if (! captured_chain && segment_top != stop)
    return NULL;
  if (captured_chain)
    kos_chain_detach_template(captured_chain, stop);

  if (baseline_src) {
    KosDynFrame *base = kos_dynframe_clone(baseline_src);
    if (! base) {
      if (captured_chain)
        kos_dynchain_free(captured_chain, NULL);
      return NULL;
    }
    /* The clone carries the frame's own namespace; the region runs in
       the one live at the prompt (enclosing let/collector scopes that
       the capture bound excluded). */
    if (baseline_ns && baseline_ns != KosDynFrame_NS(base)) {
      PyObject *own = KosDynFrame_NS(base);
      KosDynFrame_NS(base) = KosNamespace_Reveal(baseline_ns);
      Py_XDECREF(own);
    }
    if (captured_chain) {
      KosDynNode *bottom = captured_chain;
      while (KosDynNode_PARENT(bottom))
        bottom = KosDynNode_PARENT(bottom);
      KosDynNode_PARENT(bottom) = (KosDynNode *) base;
    } else {
      captured_chain = (KosDynNode *) base;
    }
  }

  resume_frame = kos_chain_first_frame(captured_chain);
  if (! resume_frame) {
    if (captured_chain)
      kos_dynchain_free(captured_chain, NULL);
    PyErr_SetString(PyExc_RuntimeError, no_frame_err);
    return NULL;
  }

  resume_stack = kos_chain_resume_stack(captured_chain, resume_frame);
  token = KosContext_TOKEN(ctx) ? (PyObject *) KosContext_TOKEN(ctx) : NULL;
  k = kos_continuation_create(
      captured_chain,
      resume_pc,
      resume_stack,
      prompt_id,
      graft_point_id,
      one_shot,
      token);
  if (! k) {
    if (captured_chain)
      kos_dynchain_free(captured_chain, NULL);
    return NULL;
  }
  return k;
}


/**
   Delimited shift: locate the prompt and offer handle(SHIFT) en route in
   one walk, clone the chain segment for the invoke template, build the
   KosContinuation. Callers: opcode PUSH_SHIFT.
*/
int kos_shift_capture(
    KosEvalState *es,
    uint32_t prompt_id,
    uint32_t resume_pc,
    KosContinuation **out_k) {

  /* One walk from dyn_top locates the nearest matching prompt while
     offering handle(SHIFT) to every node crossed: GEN denies (a shift
     cannot cross a generator boundary), a PROMPT consumes -- the match
     when its tag agrees, a topology error otherwise. Reachability is
     implied by the walk itself. */
  KosDynPrompt *prompt = NULL;
  {
    KosDynNode *node = KosEvalState_DYN_TOP(es);

    while (node) {
      KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_SHIFT);

      if (hr == KOS_HANDLE_ERROR)
        return -1;
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_PROMPT(node)
            && KosDynPrompt_ID((KosDynPrompt *) node) == prompt_id) {
          prompt = (KosDynPrompt *) node;
          break;
        }
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(SHIFT) consumed before reaching prompt");
        return -1;
      }
      node = KosDynNode_PARENT(node);
    }
  }
  if (! prompt) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found");
    return -1;
  }

  /* Bound the invoke template at the prompt's owning FRAME: clone from
   * dyn_top down through heritage nodes (WIND, PARAM, ...), the PROMPT,
   * and the nearest FRAME at-or-below it. Caller frames further down
   * resolve from the invoke-time environment instead of being replayed.
   *
   * An INVOCATION prompt has no owning frame of its own: it was created
   * by a k-invocation (graft instantiation or in-place invoke) and the
   * nodes below it -- the pairing DELIM, the original prompt, the outer
   * chain -- are not region content and must not be captured. Bound the
   * template at the prompt itself and synthesize the baseline frame from
   * the first frame below it: that frame executes the slice, so it
   * provides code, and its stack holds the region's pending operands
   * whenever the prompt does not own a block.
   *
   * Its namespace, however, is not the one the region runs in. Bounding
   * at the prompt drops every NS node between the prompt and that frame
   * -- the enclosing let/collector scopes -- so the frame's own
   * namespace would leave the resumed region indexing FAST slots it
   * does not have. Carry the innermost namespace live at the prompt
   * instead. */
  KosDynNode *stop;
  KosDynFrame *baseline_src = NULL;
  PyObject *baseline_ns = NULL;
  if (KosDynNode_FLAGS((KosDynNode *) prompt) & KOS_DYNF_INVOCATION) {
    KosDynNode *below = KosDynNode_PARENT((KosDynNode *) prompt);

    stop = below;
    while (below && ! KosDynNode_NS(below) && ! KosDynNode_IS_FRAME(below))
      below = KosDynNode_PARENT(below);
    baseline_ns = below ? KosDynNode_NS(below) : NULL;

    while (below && ! KosDynNode_IS_FRAME(below))
      below = KosDynNode_PARENT(below);
    if (! below) {
      PyErr_SetString(PyExc_RuntimeError,
                      "PUSH_SHIFT: no frame below invocation prompt");
      return -1;
    }
    baseline_src = (KosDynFrame *) below;
  } else {
    KosDynNode *owning = (KosDynNode *) prompt;
    while (owning && ! KosDynNode_IS_FRAME(owning))
      owning = KosDynNode_PARENT(owning);
    if (! owning) {
      PyErr_SetString(PyExc_RuntimeError,
                      "PUSH_SHIFT: no owning frame below prompt");
      return -1;
    }
    stop = KosDynNode_PARENT(owning);
  }
  #if KOS_XFER_TRACE
  {
    KosDynNode *frame_inside = KosDynNode_PARENT(prompt);
    KosDynNode *segment_top = KosEvalState_DYN_TOP(es);
    if (frame_inside && KosDynNode_IS_FRAME(frame_inside)) {
      KosDynFrame *f = (KosDynFrame *) frame_inside;
      SAYF("[xfer] shift_capture segment_top=%p dyn_top kind=%u frame_inside=%p stack=%p len=%zu",
           (void *) segment_top,
           (unsigned) KosDynNode_KIND(segment_top),
           (void *) frame_inside,
           (void *) KosDynFrame_STACK(f),
           KosDynFrame_STACK(f)
               ? (size_t) KosStackBlock_LEN(KosDynFrame_STACK(f)) : 0);
      (void) f;

    } else {
      SAYF("[xfer] shift_capture segment_top=%p kind=%u frame_inside=%p",
           (void *) segment_top,
           (unsigned) KosDynNode_KIND(segment_top),
           (void *) frame_inside);
    }
  }
  #endif

  {
    KosContinuation *k = kos_continuation_capture_segment(
        es,
        KosEvalState_DYN_TOP(es),
        stop,
        resume_pc,
        prompt_id,
        0,
        1,
        baseline_src,
        baseline_ns,
        "PUSH_SHIFT: no frame in captured chain");

    if (! k)
      return -1;

    {
      uint32_t live_id = KosDynNode_NODE_ID((KosDynNode *) prompt);
      KosDynNode *pclone = KosContinuation_CAPTURED_CHAIN(k);
      KosDynNode *walk;

      while (pclone
             && ! (KosDynNode_IS_PROMPT(pclone)
                   && KosDynNode_NODE_ID(pclone) == live_id))
        pclone = KosDynNode_PARENT(pclone);

      if (pclone) {
        /* An in-place invocation prompt owns no block -- the live
         * region's operands sit in the innermost owner below it. The
         * graft has no live region, so the template's prompt clone
         * takes a deep copy of those operands: the region-bottom
         * pending state a fall-through return delivers into. */
        if (baseline_src && ! KosDynNode_STACK((KosDynNode *) prompt)
            && ! KosDynNode_STACK(pclone)) {
          KosDynNode *owner = KosDynNode_PARENT((KosDynNode *) prompt);

          while (owner && ! KosDynNode_STACK(owner))
            owner = KosDynNode_PARENT(owner);
          if (owner) {
            KosStackBlock *ops =
                KosStackBlock_DeepCopy(KosDynNode_STACK(owner));
            if (! ops) {
              kos_continuation_free(k);
              return -1;
            }
            KosDynNode_STACK(pclone) = ops;
          }
        }
      }

      /* Falsify every template prompt's identity, once, here. Invokes
       * clone the template verbatim (node_id and flags preserved), so
       * each graft of this k lands as an instance distinct from the
       * live original and from other ks' grafts, with no per-invoke
       * renumbering. Grafts of the same k share these ids; that is safe
       * because grafts land at dyn_top and capture bounds at the
       * nearest prompt, so a same-id instance can never sit between a
       * SHIFT and its true delimiter. Beyond the boundary prompt, the
       * walk to the owning frame can pass heritage prompts (a reset
       * inside an outer shift's handler) whose clones ride below as
       * never-executed padding; their originals stay live, so they must
       * be falsified too or the landed copy shadows the real one. SHIFT
       * nodes cloned alongside recorded live ids; follow each to its
       * falsified copy. */
      for (walk = KosContinuation_CAPTURED_CHAIN(k); walk;
           walk = KosDynNode_PARENT(walk)) {
        KosDynNode *snode;
        uint32_t old_id;

        if (! KosDynNode_IS_PROMPT(walk))
          continue;
        old_id = KosDynNode_NODE_ID(walk);
        KosDynNode_NODE_ID(walk) = kos_dynnode_mint_id();
        KosDynNode_FLAGS(walk) |= KOS_DYNF_INVOCATION;
        for (snode = KosContinuation_CAPTURED_CHAIN(k); snode;
             snode = KosDynNode_PARENT(snode)) {
          if (KosDynNode_IS_SHIFT(snode)
              && KosDynShift_PROMPT_NODE_ID(snode) == old_id)
            KosDynShift_PROMPT_NODE_ID(snode) = KosDynNode_NODE_ID(walk);
        }
      }
    }

    /* Live identities for in-place (Path A) invoke detection; the SHIFT
     * node is recorded by PUSH_SHIFT after it is pushed. */
    KosContinuation_LIVE_PROMPT_NODE(k) = (KosDynNode *) prompt;
    KosContinuation_LIVE_PROMPT_NODE_ID(k) =
        KosDynNode_NODE_ID((KosDynNode *) prompt);

    *out_k = k;
  }
  return 0;
}


/**
   Undelimited call/cc: clone chain above CC, multi-shot continuation with
   undelimited prompt id. Callers: opcode CAPTURE_CC.
*/
int kos_capture_cc(KosEvalState *es, KosContinuation **out_k) {

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

  {
    KosContinuation *k = kos_continuation_capture_segment(
        es,
        segment_top,
        NULL,
        KosDynCC_RESUME_PC(cc),
        KOS_PROMPT_ID_UNDELIMITED,
        0,
        0,
        NULL,
        NULL,
        "CAPTURE_CC: no frame in captured segment");

    if (! k)
      return -1;
    *out_k = k;
  }
  return 0;
}


/**
   Return a KosContinuation struct to the freelist or free it.  Struct
   teardown only; callers must have released all owned resources.
*/
static void kos_continuation_release_struct(KosContinuation *k) {
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
   Free continuation and captured chain. Callers: error paths, wrapper dealloc.
*/
void kos_continuation_free(KosContinuation *k) {
  if (! k)
    return;

  Py_XDECREF(KosContinuation_TOKEN_REF(k));
  KosContinuation_TOKEN_REF(k) = NULL;

  /* resume_stack aliases a node-owned operand stack on captured_chain;
   * kos_dynchain_free releases node-owned blocks. Clear the pointer
   * only. */
  KosContinuation_RESUME_STACK(k) = NULL;

  kos_dynchain_free(KosContinuation_CAPTURED_CHAIN(k), NULL);

  kos_continuation_release_struct(k);
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

  if (KosContinuation_CAPTURED_CHAIN(k)
      && ! kos_chain_first_frame(cloned_chain)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation clone has no frame");
    kos_dynchain_free(cloned_chain, NULL);
    return NULL;
  }

  KosContinuation *clone = kos_continuation_create(
      cloned_chain,
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

  KosContinuation_LIVE_SLICE_CONSUMED(clone) =
      KosContinuation_LIVE_SLICE_CONSUMED(k);
  KosContinuation_LIVE_PROMPT_NODE(clone) = KosContinuation_LIVE_PROMPT_NODE(k);
  KosContinuation_LIVE_PROMPT_NODE_ID(clone) =
      KosContinuation_LIVE_PROMPT_NODE_ID(k);
  KosContinuation_LIVE_SHIFT_NODE(clone) = KosContinuation_LIVE_SHIFT_NODE(k);
  KosContinuation_LIVE_SHIFT_NODE_ID(clone) =
      KosContinuation_LIVE_SHIFT_NODE_ID(k);

  return clone;
}


#include "dynchain/xfer_plan.c"


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
    const KosDynNode *stop,
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
   Restitch an in-place (Path A) delimited invoke at its INPLACE delim:
   restore the resume frame's displaced stack/pc, re-link the stashed
   invoke span above the prompt (which stays on the chain for the shift
   body's POP_SHIFT), free the delim, and restore the invoke-site live
   registers. Shared by value return (POP_PROMPT) and exception
   propagation out of the slice. Expects the delim at dyn_top.
*/
int kos_dyn_inplace_restitch(KosEvalState *es, KosDynDelim *delim) {

  KosDynNode *prompt;
  KosDynNode *span;
  KosDynFrame *ret_frame;
  KosDynFrame *resume_frame;
  uint32_t ret_pc;

  if (! delim || ! KosDynNode_IS_DELIM((KosDynNode *) delim)
      || KosDynDelim_MODE(delim) != KOS_DYN_DELIM_MODE_INPLACE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_inplace_restitch: not an INPLACE delim");
    return -1;
  }
  if (KosEvalState_DYN_TOP(es) != (KosDynNode *) delim) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_inplace_restitch: delim not at dyn_top");
    return -1;
  }

  prompt = KosDynNode_PARENT((KosDynNode *) delim);
  span = KosDynDelim_RETURN_CHAIN(delim);
  ret_frame = KosDynDelim_RETURN_FRAME(delim);
  ret_pc = KosDynDelim_RETURN_PC(delim);
  if (! prompt || ! KosDynNode_IS_PROMPT(prompt) || ! span || ! ret_frame
      || ret_pc == (uint32_t) -1) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_inplace_restitch: "
                    "inconsistent INPLACE delim state");
    return -1;
  }

  /* Resume frame whose state was displaced: nearest FRAME at-or-below the
   * prompt. */
  {
    KosDynNode *node = prompt;
    while (node && ! KosDynNode_IS_FRAME(node))
      node = KosDynNode_PARENT(node);
    if (! node) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_inplace_restitch: no frame below prompt");
      return -1;
    }
    resume_frame = (KosDynFrame *) node;
  }

  /* Drop the spent slice stack; restore the displaced invoke-time state. */
  {
    KosStackBlock *spent = KosDynFrame_STACK(resume_frame);
    KosDynFrame_STACK(resume_frame) = KosDynDelim_SAVED_STACK(delim);
    KosDynDelim_SAVED_STACK(delim) = NULL;
    KosDynFrame_PC(resume_frame) = KosDynDelim_SAVED_PC(delim);
    if (spent)
      KosStackBlock_ReleaseChain(spent);
  }

  /* Re-link the stashed invoke span above the prompt; the prompt stays on
   * the chain for the shift body's POP_SHIFT. */
  {
    KosDynNode *bottom = span;
    KosDynNode *base = prompt;

    /* A snapshotted handler frame is not on the chain -- the original was
     * freed when the slice returned out of it. Push the copy above the
     * prompt so the handler has a frame to resume in, and hang the span
     * off that rather than straight onto the prompt. Ownership transfers
     * to the chain here; the delim stops covering the frame in its
     * traversal and teardown. */
    if (KosDynNode_FLAGS((KosDynNode *) delim)
        & KOS_DYNF_HOLDS_RETURN_FRAME) {
      KosDynNode_FLAGS((KosDynNode *) delim) &= ~KOS_DYNF_HOLDS_RETURN_FRAME;
      KosDynNode_PARENT((KosDynNode *) ret_frame) = prompt;
      base = (KosDynNode *) ret_frame;
    }

    while (KosDynNode_PARENT(bottom))
      bottom = KosDynNode_PARENT(bottom);
    KosDynNode_PARENT(bottom) = base;
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }

  KosDynFrame_PC(ret_frame) = ret_pc;
  KosDynFrame_ACTING_PC(ret_frame) = KosDynDelim_SAVED_ACTING_PC(delim);
  kos_dyn_commit_dyn_top(es, span);

  KosDynNode_PARENT((KosDynNode *) delim) = NULL;
  kos_dyndelim_free(delim);

  kos_dyn_rebuild_live_from_chain(es);
  /* rebuild leaves live PCs alone when the frame is unchanged; load the
   * invoke-site return pc explicitly. */
  if (KosEvalState_FRAME(es) == ret_frame)
    kos_dyn_load_live_regs_from_frame(es, ret_frame);

  return 0;
}


/**
   Complete an in-place (Path A) delimited POP_PROMPT return. See dynchain.h.
*/
int kos_dyn_finish_inplace_pop_return(
    KosEvalState *es,
    KosDynDelim *delim,
    PyObject *value) {

  if (! value) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_finish_inplace_pop_return: value is NULL");
    return -1;
  }

  if (kos_dyn_inplace_restitch(es, delim) < 0) {
    Py_DECREF(value);
    return -1;
  }

  /* StackPUSH consumes the value reference, on failure as well. */
  return KosEvalState_StackPUSH(es, value);
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
