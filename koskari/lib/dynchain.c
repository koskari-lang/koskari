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
   replacing separate frame and handler chains. Transfer goals (see
   xfer_goal.h) move control for delimited and undelimited continuation
   invoke. Types and macros live in dynchain.h.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "xfer_goal.h"
#include "debug.h"
#include "tracing.h"
#include "hints.h"

#include <stdint.h>


/* === Forward declarations === */


static KosOpResult kos_dyn_uninstall_handler(KosEvalState *es, KosDynNode *node,
                                             KosXferEvent event);
static KosOpResult kos_dyn_unlink_top_take(
    KosEvalState *es,
    KosXferEvent ev,
    int allow_barrier,
    KosObligation *out_return);
int kos_dyn_push(KosEvalState *es, KosDynNode *node);
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node);
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top);
int kos_dyn_reaches(KosEvalState *es, const KosDynNode *target);
static void kos_dyn_restamp_spliced_prefix(KosEvalState *es);
static void kos_dyn_stamp_span(
    KosDynNode **span,
    size_t len,
    KosDynFrame *rft,
    KosSegment **rbl,
    PyObject *rns);
static void kos_dyn_restamp_span(
    KosDynNode *from,
    KosDynNode *stop,
    const KosLiveRegs *base);

static void kos_dyn_resync_apply(
    KosEvalState *es,
    KosDynFrame *ft,
    KosSegment **bl,
    PyObject *ns,
    int flush);
static void kos_dyn_resync_from(
    KosDynNode *start,
    KosDynFrame **ft_out,
    KosSegment ***bl_out,
    PyObject **ns_out);
static void kos_dyn_resync_after_push(
    KosEvalState *es,
    KosDynNode *new_node);
static void kos_dyn_resync_after_pop(
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
    /* zeroed on return to the freelist; only the parent link (reused
       as the freelist chain) needs clearing */
    KosDynNode_PARENT(p) = NULL;
    p->node_id = kos_dynnode_mint_id();
    return p;
  }

  KOS_TRACE_FL_ALLOC("dyn_node", 0);
  p = PyMem_Malloc(sizeof(KosDynNode));
  if (! p) {
    PyErr_NoMemory();
    return NULL;
  }

  /* The embedded segment at the tail needs no zeroing: its header is
     initialized on use (kos_dynnode_use_inline_segment) and its slots
     are gated by len. */
  memset(p, 0, offsetof(KosDynNode, iseg));
  p->node_id = kos_dynnode_mint_id();

  return p;
}


/**
   Return a node to the freelist or free it. Call only after kind-specific
   cleanup (refs released). Node is zeroed before being cached.
*/
static void kos_dynnode_return_to_freelist(KosDynNode *node) {
  if (KosDynNode_STACK(node)) {
    KosStack_Release(KosDynNode_STACK(node));
    KosDynNode_STACK(node) = NULL;
  }

  /* The base ns field is uniformly owned (introducers attach their
     own; every other pushed node carries a fill of the live one). */
  Py_CLEAR(KosDynNode_NS(node));

  if (dyn_freecount < DYN_NODE_MAX_FREE) {
    KOS_TRACE_FL_RETURN("dyn_node", 0);
    /* Skip the embedded segment tail; see kos_dynnode_alloc. */
    memset(node, 0, offsetof(KosDynNode, iseg));
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


static PyObject *snapshot_stack_to_list(KosSegment *stack) {

  size_t len = stack ? KosSegment_LEN(stack) : 0;

  PyObject *out = PyList_New((Py_ssize_t) len);
  if (! out)
    return NULL;

  for (size_t i = 0; i < len; i++) {
    PyObject *item = stack->objs[i];
    Py_INCREF(item);
    PyList_SET_ITEM(out, (Py_ssize_t) i, item);
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


/* === Continuation capture errors === */


static PyObject *exc_prompt_not_found = NULL;
static PyObject *exc_prompt_unreachable = NULL;


/**
   Lazily resolve the capture error classes from koskari.exceptions.
   Capture only runs long after the package is fully imported, so the
   import never cycles; the classes are cached for the process lifetime.
*/
static int kos_load_capture_excs(void) {
  PyObject *mod;

  if (exc_prompt_not_found && exc_prompt_unreachable)
    return 0;

  mod = PyImport_ImportModule("koskari.exceptions");
  if (! mod)
    return -1;

  exc_prompt_not_found =
      PyObject_GetAttrString(mod, "KoskariPromptNotFound");
  exc_prompt_unreachable =
      PyObject_GetAttrString(mod, "KoskariPromptUnreachable");
  Py_DECREF(mod);

  if (! exc_prompt_not_found || ! exc_prompt_unreachable) {
    Py_CLEAR(exc_prompt_not_found);
    Py_CLEAR(exc_prompt_unreachable);
    return -1;
  }
  return 0;
}


/**
   Raise KoskariPromptNotFound for a capture walk that exhausted the
   chain without meeting a prompt matching prompt_id.
*/
static int kos_raise_prompt_not_found(uint32_t prompt_id) {
  PyObject *exc;

  if (kos_load_capture_excs())
    return -1;

  exc = PyObject_CallFunction(
      exc_prompt_not_found, "sIs",
      "shift has no enclosing reset",
      (unsigned int) prompt_id, "shift");
  if (! exc)
    return -1;
  PyErr_SetObject(exc_prompt_not_found, exc);
  Py_DECREF(exc);
  return -1;
}


/**
   Raise KoskariPromptUnreachable at a node that denied handle(SHIFT).
   The deny site does not know which prompt the walk was seeking, so
   prompt_id is left None; kos_capture_note_prompt_id fills it in when
   the walk owner regains control.
*/
static void kos_raise_prompt_unreachable(const char *boundary) {
  PyObject *msg;
  PyObject *exc;

  if (kos_load_capture_excs())
    return;

  msg = PyUnicode_FromFormat(
      "shift cannot capture across a %s boundary", boundary);
  if (! msg)
    return;

  exc = PyObject_CallFunction(
      exc_prompt_unreachable, "OOss",
      msg, Py_None, "shift", boundary);
  Py_DECREF(msg);
  if (! exc)
    return;
  PyErr_SetObject(exc_prompt_unreachable, exc);
  Py_DECREF(exc);
}


/**
   Note the seeking prompt id on an in-flight KoskariPromptUnreachable.
   Any other in-flight exception is left untouched.
*/
static void kos_capture_note_prompt_id(uint32_t prompt_id) {
  PyObject *typ;
  PyObject *val;
  PyObject *tb;
  PyObject *pid;

  if (! exc_prompt_unreachable
      || ! PyErr_ExceptionMatches(exc_prompt_unreachable))
    return;

  PyErr_Fetch(&typ, &val, &tb);
  PyErr_NormalizeException(&typ, &val, &tb);
  pid = PyLong_FromUnsignedLong(prompt_id);
  if (pid) {
    if (PyObject_SetAttrString(val, "prompt_id", pid))
      PyErr_Clear();
    Py_DECREF(pid);
  }
  PyErr_Restore(typ, val, tb);
}


/**
   Debug cross-check (KOS_DEBUG_PATCH_DYN_CACHES): compare a cached
   live-fact answer against the chain walk from anchor and raise
   SystemError on drift. Each expectation is compared only when its
   check flag is set; the ns comparison additionally requires the walk
   to have found one (a NULL walk result proves nothing). Compiles to
   nothing in normal builds.
*/
#if KOS_DEBUG_PATCH_DYN_CACHES
static void kos_dyn_debug_check_walk(
    KosDynNode *anchor,
    KosDynFrame *expect_ft, int check_ft,
    KosSegment **expect_bl, int check_bl,
    PyObject *expect_ns, int check_ns) {

  KosDynFrame *ft_dbg = NULL;
  KosSegment **bl_dbg = NULL;
  PyObject *ns_dbg = NULL;

  kos_dyn_resync_from(anchor, &ft_dbg, &bl_dbg, &ns_dbg);

  if ((check_ft && expect_ft != ft_dbg)
      || (check_bl && expect_bl != bl_dbg)
      || (check_ns && ns_dbg && expect_ns != ns_dbg))
    PyErr_SetString(PyExc_SystemError, "unwind cache drift vs walk");
}
#define KOS_DYN_DEBUG_CHECK_WALK(...) kos_dyn_debug_check_walk(__VA_ARGS__)
#else
#define KOS_DYN_DEBUG_CHECK_WALK(...) ((void) 0)
#endif


/**
   Recompute a node's segment back link in place: the enclosing owning
   slot is the first stack owner below the node. For segments restored
   to their node after displacement (delim stash), whose install-time
   link cannot be trusted across the restitch.
*/
static void kos_dyn_reslot_segment_back(KosDynNode *node) {

  KosSegment **slot = NULL;

  for (KosDynNode *p = KosDynNode_PARENT(node); p; p = KosDynNode_PARENT(p)) {
    if (KosDynNode_STACK(p)) {
      slot = &KosDynNode_STACK(p);
      break;
    }
  }

  KosSegment_BACK(KosDynNode_STACK(node)) = slot;
}


/**
   Clear every unwind cache -- segment back links and frame back
   links -- on nodes in [from, stop), walking parent links. Call
   whenever a span of nodes is detached from (or re-spliced into) the
   live chain: the caches were recorded against the chain shape at
   install time and a moved span cannot trust them. Over-clearing is
   always safe -- a NULL cache falls back to the chain walk.
*/
static void kos_dyn_clear_unwind_caches(KosDynNode *from, KosDynNode *stop) {
  for (KosDynNode *n = from; n && n != stop; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_STACK(n))
      KosSegment_BACK(KosDynNode_STACK(n)) = NULL;
    if (KosDynNode_IS_FRAME(n))
      KosDynFrame_BACK_FRAME(n) = NULL;
  }
}


/**
   Region kinds own an operand segment; transfer bookmarks (cc,
   cc_raise, delim, shift, gen, next, yield_from) stay transparent.
   The compiler emits bookmark pushes mid-expression -- PUSH_CC inside
   an open call window, OBJECT_NEXT between operand loads -- so their
   extents legitimately straddle operand windows on the enclosing
   segment and must not capture them. Block-structured regions
   (PUSH_X body POP_X) isolate their body's operands on their own
   segment. The eight kinds that predate universal ownership allocate
   in their constructors; this covers the region kinds that inherited
   before.
*/
static void kos_dyn_ensure_region_segment(KosDynNode *node) {

  if (KosDynNode_STACK(node))
    return;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PARAM:
    case KOS_DYN_NS:
    case KOS_DYN_WIND:
    case KOS_DYN_COLLECT:
      kos_dynnode_use_inline_segment(node);
      break;
    default:
      break;
  }
}


/**
   Shared push preparation: attach the region segment and run the
   namespace owned fill. A node that does not introduce its own
   namespace carries an owned reference to the live one, so on pop the
   revealed node always answers the namespace question. Frames keep
   their exact semantics (a placeholder frame's NULL ns stays NULL
   until its real namespace is bound).
*/
static void kos_dyn_prepare_push(KosEvalState *es, KosDynNode *node) {

  kos_dyn_ensure_region_segment(node);

  if (! KosDynNode_IS_FRAME(node) && ! KosDynNode_NS(node)) {
    PyObject *live_ns = KosEvalState_NAMESPACE(es);
    if (live_ns) {
      Py_INCREF(live_ns);
      KosDynNode_NS(node) = live_ns;
    }
  }
}




/**
   Copy a source node's segment onto a stackless clone: into the
   clone's embedded storage when it fits, a heap deep copy otherwise.
   Copies are born with back NULL. No-op when the clone already has a
   segment or the source has none.

   :return: 0 on success, -1 on allocation failure
*/
static int kos_dyn_clone_copy_segment(KosDynNode *cloned, KosDynNode *src) {

  KosSegment *seg = KosDynNode_STACK(src);

  if (KosDynNode_STACK(cloned) || ! seg)
    return 0;

  size_t len = KosSegment_LEN(seg);

  if (len <= KOS_SEGMENT_INLINE_SLOTS) {
    KosSegment *dst = kos_dynnode_use_inline_segment(cloned);
    for (size_t i = 0; i < len; i++) {
      PyObject *val = seg->objs[i];
      Py_INCREF(val);
      dst->objs[i] = val;
    }
    KosSegment_LEN(dst) = len;
    return 0;
  }

  KosDynNode_STACK(cloned) = KosStack_DeepCopy(seg);
  return KosDynNode_STACK(cloned) ? 0 : -1;
}


/**
   Install-time segment stamp: record the displaced owning slot as the
   unwind cache and count inline installs.
*/
static inline void kos_dyn_stamp_installed_segment(
    KosSegment *seg,
    KosSegment **live_slot) {

  KosSegment_BACK(seg) = live_slot;
  if (KosSegment_IS_INLINE(seg))
    KOS_TRACE_EVENTS_INC(KOS_TE_SEG_INLINE_INSTALL);
}


/**
   Steal every value remaining on a transparent extent's segment ahead
   of its pop. Pair with kos_dyn_repush_stolen after the pop completes:
   the values flow onto the revealed segment exactly as they did when
   the extent shared its parent's segment. Refcount-free in both
   halves: references move, they are never duplicated or dropped.
*/
static int kos_dyn_steal_extent_values(
    KosDynNode *node,
    KosStackSteal *vals) {

  KosSegment *seg = KosDynNode_STACK(node);
  size_t count = seg ? KosSegment_LEN(seg) : 0;

  return KosStackSteal_Tail(vals, count, &KosDynNode_STACK(node));
}


/**
   Push values stolen by kos_dyn_steal_extent_values onto the live
   segment, bottom-first, consuming vals. On a push failure the failed
   value has been released by the push; the unpushed remainder is
   released here.
*/
static int kos_dyn_repush_stolen(KosEvalState *es, KosStackSteal *vals) {

  for (size_t i = 0; i < vals->len; i++) {
    if (kos_stack_push_steal_fast(KosEvalState_BLOCK(es), vals->vals[i]) < 0) {
      vals->owned_start = i + 1;
      KosStackSteal_Free(vals);
      return -1;
    }
  }

  vals->len = 0;
  KosStackSteal_Free(vals);
  return 0;
}


/**
   Pop a transparent extent at dyn_top: steal its remaining values,
   commit the parent as the new top, free the node, and repush the
   values onto the revealed segment. The caller performs any per-kind
   teardown (e.g. param restore) first.
*/
static int kos_dyn_pop_transparent_deliver(
    KosEvalState *es,
    KosDynNode *node) {

  KosStackSteal vals;
  KosDynNode *parent = KosDynNode_PARENT(node);

  if (kos_dyn_steal_extent_values(node, &vals) < 0)
    return -1;

  kos_dyn_commit_dyn_top(es, parent);
  KosDynNode_PARENT(node) = NULL;
  kos_dynnode_free(node);

  return kos_dyn_repush_stolen(es, &vals);
}


#include "dynchain/dyn_frame.c"
#include "dynchain/dyn_prompt.c"
#include "dynchain/dyn_delim.c"
#include "dynchain/dyn_cc.c"
#include "dynchain/dyn_shift.c"
#include "dynchain/dyn_gen.c"
#include "dynchain/dyn_wind.c"
/**
   A TRY or FINALLY region leaving the chain gives back the
   handler-visible exception it displaced when it consumed a raise,
   the way POP_EXCEPT restores the previous handled exception in
   CPython. A region that consumed nothing changes nothing. Every path
   off the chain runs this: the fast pops, and the uninstall the raise
   walk and the return and break teardowns go through.
*/
static void kos_dyn_region_leave_exc(KosDynNode *node) {
  uint8_t *saved_flag;
  KosExcState *saved;

  if (KosDynNode_IS_TRY(node)) {
    saved_flag = &KosDynTry_EXC_SAVED((KosDynTry *) node);
    saved = KosDynTry_SAVED((KosDynTry *) node);
  } else {
    saved_flag = &KosDynFinally_EXC_SAVED((KosDynFinally *) node);
    saved = KosDynFinally_SAVED((KosDynFinally *) node);
  }

  if (*saved_flag) {
    /* an empty saved state clears the thread's, which is right: the
       region's exception stops being visible when the region goes */
    KosExc_Install(saved);
    *saved_flag = 0;
  }
}


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
#include "dynchain/dyn_iter_node.c"


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


/* === Obligations === */


KosObligation *kos_dyn_node_ob(KosDynNode *node) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return KosDynWind_OB(node);
    case KOS_DYN_WITH:
      return KosDynWith_OB(node);
    default:
      return NULL;
  }
}


void kos_dyn_ob_clear(KosObligation *ob) {
  switch (ob->kind) {
    case KOS_OB_VALUE:
      Py_CLEAR(ob->u.value);
      break;
    case KOS_OB_EXCEPTION:
      KosExc_Clear(&ob->u.exc);
      break;
    case KOS_OB_RETURN:
      Py_CLEAR(ob->u.ret.value);
      ob->u.ret.delim = NULL;
      break;
    default:
      break;
  }
  ob->kind = KOS_OB_NONE;
}


int kos_dyn_ob_traverse(KosObligation *ob, visitproc visit, void *arg) {
  switch (ob->kind) {
    case KOS_OB_VALUE:
      Py_VISIT(ob->u.value);
      break;
    case KOS_OB_EXCEPTION:
      return KosExc_Traverse(&ob->u.exc, visit, arg);
    case KOS_OB_RETURN:
      Py_VISIT(ob->u.ret.value);
      break;
    default:
      break;
  }
  return 0;
}


/**
   The empty slot of a node that carries one, or NULL with PyErr set.
*/
static KosObligation *kos_dyn_ob_slot_for(KosDynNode *node) {
  KosObligation *ob;

  if (! KosDynNode_HAS_CAP(node, KOS_DYN_CAP_HOLDS_OBLIGATION)) {
    PyErr_Format(PyExc_SystemError,
                 "obligation parked on a %s node, which carries none",
                 kos_dynnode_kind_name(KosDynNode_KIND(node)));
    return NULL;
  }
  ob = kos_dyn_node_ob(node);
  if (ob->kind != KOS_OB_NONE) {
    PyErr_Format(PyExc_SystemError,
                 "obligation parked on a %s node already holding one",
                 kos_dynnode_kind_name(KosDynNode_KIND(node)));
    return NULL;
  }
  return ob;
}


int kos_dyn_ob_park_value(KosDynNode *node, PyObject *val) {
  KosObligation *ob = kos_dyn_ob_slot_for(node);

  if (! ob) {
    Py_XDECREF(val);
    return -1;
  }
  ob->kind = KOS_OB_VALUE;
  ob->u.value = val;
  return 0;
}


int kos_dyn_ob_park_exception(
    KosDynNode *node,
    PyObject *typ,
    PyObject *val,
    PyObject *tb) {

  KosObligation *ob = kos_dyn_ob_slot_for(node);

  if (! ob) {
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return -1;
  }
  PyErr_NormalizeException(&typ, &val, &tb);
  if (val && tb)
    PyException_SetTraceback(val, tb);
  ob->kind = KOS_OB_EXCEPTION;
  ob->u.exc.type = typ;
  ob->u.exc.value = val;
  ob->u.exc.tb = tb;
  return 0;
}


int kos_dyn_ob_park_return(
    KosDynNode *node,
    PyObject *val,
    uint8_t path,
    KosDynNode *delim) {

  KosObligation *ob = kos_dyn_ob_slot_for(node);

  if (! ob) {
    Py_XDECREF(val);
    return -1;
  }
  ob->kind = KOS_OB_RETURN;
  ob->u.ret.value = val;
  ob->u.ret.path = path;
  ob->u.ret.delim = delim;
  return 0;
}


/**
   Resume a parked exception's propagation. With another exception
   already in flight (PyErr set) the parked one has been superseded and
   becomes its __context__; otherwise the parked one is raised again.
   The obligation is released either way.

   :return: EVAL_RAISE
*/
static KosOpResult kos_dyn_ob_resume_exception(KosObligation *ob) {

  PyObject *ntyp = NULL, *nval = NULL, *ntb = NULL;

  if (! PyErr_Occurred()) {
    KosExc_Reraise(&ob->u.exc);
    ob->kind = KOS_OB_NONE;
    return EVAL_RAISE();
  }
  PyErr_Fetch(&ntyp, &nval, &ntb);
  kos_exc_supersede_chain(&ntyp, &nval, &ntb,
                          ob->u.exc.type, ob->u.exc.value, ob->u.exc.tb);
  KosExc_Init(&ob->u.exc);
  ob->kind = KOS_OB_NONE;
  PyErr_Restore(ntyp, nval, ntb);
  return EVAL_RAISE();
}


/* === The extent contract === */


KosOpResult kos_dyn_extent_enter(KosEvalState *es, KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WITH:
      return kos_dyn_with_enter(es, (KosDynWith *) node);
    case KOS_DYN_WIND:
      return kos_dyn_wind_enter(es, (KosDynWind *) node);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_extent_enter: node kind %u has no enter",
                   (unsigned) KosDynNode_KIND(node));
      return EVAL_RAISE();
  }
}


KosOpResult kos_dyn_extent_leave(
    KosEvalState *es,
    KosDynNode *node,
    KosXferEvent ev) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return kos_dyn_wind_leave(es, (KosDynWind *) node, ev);
    case KOS_DYN_WITH:
      return kos_dyn_with_leave(es, (KosDynWith *) node, ev);
    case KOS_DYN_PARAM:
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      return EVAL_NORMAL();

    case KOS_DYN_DELIM:
    case KOS_DYN_SHIFT:
    case KOS_DYN_GEN:
    case KOS_DYN_NEXT:
      return EVAL_NORMAL();
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_extent_leave: node kind %u has no leave",
                   (unsigned) KosDynNode_KIND(node));
      return EVAL_RAISE();
  }
}


int kos_dyn_extent_settle(KosEvalState *es, KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return kos_dyn_wind_settle(es, (KosDynWind *) node);
    case KOS_DYN_WITH:
      return kos_dyn_with_settle(es, (KosDynWith *) node);
    default:
      return 0;
  }
}


void kos_dyn_extent_abort(KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      kos_dyn_wind_abort((KosDynWind *) node);
      break;
    case KOS_DYN_WITH:
      kos_dyn_with_abort((KosDynWith *) node);
      break;
    default:
      break;
  }
}


int kos_dyn_extent_in_flight(const KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN
          || KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_AFTER;
    case KOS_DYN_WITH:
      return KosDynWith_ENTER_PENDING(node) || KosDynWith_EXIT_PENDING(node);
    default:
      return 0;
  }
}


KosOpResult kos_dyn_settle_top(KosEvalState *es, KosObligation *out_return) {
  KosDynNode *top = KosEvalState_DYN_TOP(es);
  int was_leave;

  if (! top || ! KosDynNode_HAS_CAP(top, KOS_DYN_CAP_CONTROL_HOOK))
    return EVAL_NORMAL();
  if (! kos_dyn_extent_in_flight(top))
    return EVAL_NORMAL();
  if (kos_xfer_goal_waits_on(es, top))
    return EVAL_NORMAL();

  was_leave = KosDynNode_IS_WIND(top)
      ? KosDynWind_PHASE(top) == KOS_WIND_PHASE_PRE_AFTER
      : KosDynWith_EXIT_PENDING(top);

  if (kos_dyn_extent_settle(es, top) < 0)
    return EVAL_RAISE();
  if (! was_leave)
    return EVAL_NORMAL();

  return kos_dyn_unlink_top_take(es, KOS_XFER_EVENT_NONE, 0, out_return);
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
KosDynNode *kos_dyn_clone_segment(
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
   Write frame, operand-segment slot, and namespace onto KosEvalState.

   Call only from kos_dyn_resync_after_push, kos_dyn_resync_after_pop, or
   kos_dyn_resync_chain after dyn_top is already correct. When the
   frame pointer changes and flush is non-zero, flushes live pc and acting_pc
   onto the outgoing frame before loading code and PCs from the new frame.
   Pass flush=0 when the outgoing frame is about to be freed (return path) so
   the write is skipped. When only block or namespace change, updates those
   slots without clobbering live PCs.
*/
static void kos_dyn_resync_apply(
    KosEvalState *es,
    KosDynFrame *ft,
    KosSegment **bl,
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
static void kos_dyn_resync_load_regs(
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
   skip that result. Used by kos_dyn_resync_after_pop when a popped node
   owned one or more live slots, and by the same walk logic as rebuild.
*/
static void kos_dyn_resync_from(
    KosDynNode *start,
    KosDynFrame **ft_out,
    KosSegment ***bl_out,
    PyObject **ns_out) {

  KosDynFrame *ft = NULL;
  KosSegment **bl = NULL;
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

    /* under the owned fill, any carried ns is the live value */
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
   only the slots they carry. Passes flush=1 to kos_dyn_resync_apply so the
   outgoing frame's PCs are saved before the new frame is loaded. Do not use
   after splices, gen install, or
   xfer INSTALL/PUSH; use kos_dyn_resync_chain instead.
*/
static void kos_dyn_resync_after_push(
    KosEvalState *es,
    KosDynNode *new_node) {

  KosDynFrame *ft = NULL;
  KosSegment **bl = NULL;
  PyObject *ns = NULL;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es || ! new_node)
    goto done;

  if (KosDynNode_IS_FRAME(new_node)) {
    ft = (KosDynFrame *) new_node;
    bl = &KosDynNode_STACK(new_node);
    ns = KosDynNode_NS(new_node);
    /* install-time back link for uninstall restoration */
    KosDynFrame_BACK_FRAME(new_node) =
        (KosDynNode *) KosEvalState_FRAME(es);
  } else {
    ft = KosEvalState_FRAME(es);
    /* the fill ran at push, so the node's ns is the live value; NULL
       only in the placeholder-frame window, where the mirror stands */
    ns = KosDynNode_NS(new_node)
        ? KosDynNode_NS(new_node)
        : KosEvalState_NAMESPACE(es);
    if (KosDynNode_STACK(new_node))
      bl = &KosDynNode_STACK(new_node);
    else
      bl = KosEvalState_BLOCK(es);
  }

  /* Install-time back link: the pushed node's segment remembers the
     owning slot it displaced, for uninstall restoration and parent
     delivery. Inheriting nodes (bl == live slot) link nothing. */
  if (bl != KosEvalState_BLOCK(es) && *bl)
    kos_dyn_stamp_installed_segment(*bl, KosEvalState_BLOCK(es));

  kos_dyn_resync_apply(es, ft, bl, ns, 1);

#if KOS_DEBUG_PATCH_DYN_CACHES
  {
    KosDynFrame *ft_dbg = KosEvalState_FRAME(es);
    KosSegment **bl_dbg = KosEvalState_BLOCK(es);
    PyObject *ns_dbg = KosEvalState_NAMESPACE(es);

    kos_dyn_resync_chain(es);
    if (ft_dbg != KosEvalState_FRAME(es)
        || bl_dbg != KosEvalState_BLOCK(es)
        || ns_dbg != KosEvalState_NAMESPACE(es)) {
      PyErr_SetString(PyExc_SystemError,
                      "kos_dyn_resync_after_push: live drift vs rebuild");
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
   kos_dyn_resync_apply — the outgoing frame's PCs were already saved when it was
   pushed, and on the return path the frame is about to be freed. When new_top
   does not equal parent(removed) (splice/gen replace), delegates to
   kos_dyn_resync_chain. For a normal single-head pop, updates only
   the live slots the removed node owned; returns
   without work when the pop did not own frame, block, or namespace on es.
*/
static void kos_dyn_resync_after_pop(
    KosEvalState *es,
    KosDynNode *removed,
    KosDynNode *new_top) {

  int need_frame = 0;
  int need_block = 0;
  int walk_ns = 0;
  KosDynFrame *ft = NULL;
  KosSegment **bl = NULL;
  PyObject *ns = NULL;
  PyObject *ns_new;

  if (! es)
    return;

  if (! removed || new_top != KosDynNode_PARENT(removed)) {
    kos_dyn_resync_chain(es);
    return;
  }

  if (KosEvalState_FRAME(es) == (KosDynFrame *) removed)
    need_frame = 1;

  if (KosDynNode_STACK(removed)
      && KosEvalState_BLOCK(es) == &KosDynNode_STACK(removed))
    need_block = 1;

  /* Namespace reconcile: under the owned fill the revealed node
     carries the live namespace, so the answer is read from the
     survivor -- never from the dying node. A node that reached the
     chain without passing through push (a splice) may carry none;
     fall back to the introduces-walk for that case. */
  ns_new = KosEvalState_NAMESPACE(es);
  {
    PyObject *ns_top = new_top ? KosDynNode_NS(new_top) : NULL;
    if (ns_top) {
      KOS_DYN_DEBUG_CHECK_WALK(new_top, NULL, 0, NULL, 0, ns_top, 1);
      ns_new = ns_top;
    } else if (KosDynNode_NS(removed)
               && KosEvalState_NAMESPACE(es) == KosDynNode_NS(removed)) {
      /* unfilled survivor and the dying node carried the live ns:
         recompute by walk */
      walk_ns = 1;
    }
  }

  if (! need_frame && ! need_block && ! walk_ns) {
    if (ns_new != KosEvalState_NAMESPACE(es))
      kos_dyn_resync_apply(es, KosEvalState_FRAME(es), KosEvalState_BLOCK(es),
                         ns_new, 0);
    return;
  }

  /* Block-only restore: the removed owner's segment remembers the
     slot it displaced at install (still alive here -- uninstall frees
     the node only after this retarget completes); a survivor that
     owns a stack supplies the same answer when the cache is missing.
     When frame or ns also changed, one walk resolves everything. */
  if (need_block && ! need_frame && ! walk_ns) {
    KosSegment **slot = KosSegment_BACK(KosDynNode_STACK(removed));
    if (! slot && new_top && KosDynNode_STACK(new_top))
      slot = &KosDynNode_STACK(new_top);
    if (slot) {
      KOS_DYN_DEBUG_CHECK_WALK(new_top, NULL, 0, slot, 1, NULL, 0);
      kos_dyn_resync_apply(es, KosEvalState_FRAME(es), slot, ns_new, 0);
      return;
    }
  }

  /* Frame pop: every fact is cache-available -- the back frame
     recorded at install, the frame segment's displaced owning slot,
     and the reconciled ns from the survivor. A missing piece (a
     splice-installed frame carries neither link) falls through to
     the walk. */
  if (need_frame && ! walk_ns
      && KosDynFrame_BACK_FRAME(removed)
      && (! need_block
          || (KosDynNode_STACK(removed)
              && KosSegment_BACK(KosDynNode_STACK(removed))))) {
    KosDynFrame *cached_ft = (KosDynFrame *) KosDynFrame_BACK_FRAME(removed);
    KosSegment **cached_bl = need_block
        ? KosSegment_BACK(KosDynNode_STACK(removed))
        : KosEvalState_BLOCK(es);
    KOS_DYN_DEBUG_CHECK_WALK(new_top, cached_ft, 1, cached_bl, need_block,
                             ns_new, 1);
    kos_dyn_resync_apply(es, cached_ft, cached_bl, ns_new, 0);
    return;
  }

#if KOS_EVAL_SAFETYNET
  /* Safety net: recompute by walk whatever the caches could not
     answer. Under the cache model (install stamps + splice restamp +
     detach clears) this is unreachable -- the fallback counter
     measured zero across the full benchmark battery -- so the walk is
     a diagnostic for model violations, not a code path. */
  KOS_TRACE_EVENTS_INC(KOS_TE_RETARGET_WALK_FALLBACK);
  kos_dyn_resync_from(new_top, &ft, &bl, &ns);

  if (! need_frame)
    ft = KosEvalState_FRAME(es);
  if (! need_block)
    bl = KosEvalState_BLOCK(es);
  if (! walk_ns)
    ns = ns_new;

  kos_dyn_resync_apply(es, ft, bl, ns, 0);
#else
  /* The cache model is authoritative for frame and block. The one
     question it cannot answer is the namespace when the survivor
     carries no fill and the dying node held the live one; native
     frames carry the owned fill (kos_dyn_push_frame_native), so this
     arises only when the surviving extent genuinely has no namespace
     anywhere -- a native frame at the root of an adapter host whose
     fill was NULL. Recompute by walk; applying ns_new there would
     republish the dying frame's namespace, which the pop is about to
     release. A model violation in the frame or block facts lands NULL
     here and fails fast nearby; build with -DKOS_EVAL_SAFETYNET=1 to
     diagnose by walk instead. */
  (void) ft;
  (void) bl;
  (void) ns;
  if (walk_ns) {
    ns_new = NULL;
    kos_dyn_resync_from(new_top, NULL, NULL, &ns_new);
  }
  kos_dyn_resync_apply(
      es,
      need_frame
          ? (KosDynFrame *) KosDynFrame_BACK_FRAME(removed)
          : KosEvalState_FRAME(es),
      (need_block && KosDynNode_STACK(removed))
          ? KosSegment_BACK(KosDynNode_STACK(removed))
          : KosEvalState_BLOCK(es),
      ns_new,
      0);
#endif
}


/**
   Full chain walk: recompute live frame, block, and namespace on es from dyn_top.

   Use when incremental push or pop retarget does not apply: transfer plan
   INSTALL or PUSH, kos_dyn_gen_install, KosContext_New bootstrap, tail koscall
   frame replacement, opcode paths that assign a segment after push, or any
   time dyn_top was published without new_top == parent(removed). Walks from
   ctx->dyn_top toward parents, then kos_dyn_resync_apply on es.
   Prefer kos_dyn_resync_after_push or kos_dyn_resync_after_pop inside
   kos_dyn_push and kos_dyn_commit_dyn_top when the edit is a single-head push
   or parent pop.
*/
/**
   Restamp the unstamped top region of the chain after a splice.
   Spliced spans arrive with NULL unwind caches: clones are born
   clear, detaches clear, and direct links never stamp. Chain
   mutations are top-regional (installs, restitches, and thaws land at
   or near dyn_top), so unstamped nodes form a contiguous prefix;
   collect it, read the facts live at its base, and stamp
   rootward-first -- segment backs, frame back links, and ns fills --
   so the pop-side caches are complete for every resident node.
   Stamps never overwrite: a non-NULL cache is valid by the detach
   hygiene invariant. Allocation failure just leaves the prefix
   unstamped; the counted fallback walks remain the safety net.

   Known, accepted waste: the scan keys on positive stamp evidence,
   and two node classes never provide any -- bookmarks (no
   restampable caches at all) and the bottom of the chain, whose
   caches are legitimately NULL (the root frame displaced no frame;
   nothing encloses the root owner's segment). Such nodes are
   re-collected and idempotently re-stamped on every rebuild whose
   scan reaches them -- most visibly when the root frame itself is
   dyn_top. Bounded to a few loads per rebuild and measured invisible
   on the rebuild-heavy benchmarks; an exactness flag would cost more
   in detach-path hygiene than it saves.
*/
#define KOS_RESTAMP_FIXED 64

static void kos_dyn_restamp_spliced_prefix(KosEvalState *es) {

  KosDynNode *fixed[KOS_RESTAMP_FIXED];
  KosDynNode **span = fixed;
  size_t cap = KOS_RESTAMP_FIXED;
  size_t len = 0;
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  /* Collect until the first COMPLETELY stamped node: at least one
     cache set and none missing. Absence of evidence is not
     stamped-ness -- a bookmark carries no restampable caches at all
     (its ns fill is owned state that survives detach, so it proves
     nothing about the nodes below it), and the caches at the bottom
     of the chain are legitimately NULL. A partially stamped node
     (e.g. a frame whose back link survived while its restored
     segment's did not) still needs collecting. */
  for (; node; node = KosDynNode_PARENT(node)) {
    int seg_set = KosDynNode_STACK(node)
        && KosSegment_BACK(KosDynNode_STACK(node)) != NULL;
    int seg_missing = KosDynNode_STACK(node)
        && KosSegment_BACK(KosDynNode_STACK(node)) == NULL;
    int bf_set = KosDynNode_IS_FRAME(node)
        && KosDynFrame_BACK_FRAME(node) != NULL;
    int bf_missing = KosDynNode_IS_FRAME(node)
        && KosDynFrame_BACK_FRAME(node) == NULL;

    if ((seg_set || bf_set) && ! seg_missing && ! bf_missing)
      break;

    if (len == cap) {
      size_t ncap = cap * 2;
      KosDynNode **grown;
      if (span == fixed) {
        grown = PyMem_Malloc(ncap * sizeof(KosDynNode *));
        if (grown)
          memcpy(grown, span, len * sizeof(KosDynNode *));
      } else {
        grown = PyMem_Realloc(span, ncap * sizeof(KosDynNode *));
      }
      if (! grown)
        goto out;
      span = grown;
      cap = ncap;
    }
    span[len++] = node;
  }

  if (len) {
    /* facts live at the base of the prefix */
    KosDynFrame *rft = NULL;
    KosSegment **rbl = NULL;
    PyObject *rns = NULL;
    kos_dyn_resync_from(node, &rft, &rbl, &rns);
    kos_dyn_stamp_span(span, len, rft, rbl, rns);
  }

out:
  if (span != fixed)
    PyMem_Free(span);
}


/**
   Stamp the collected nodes span[0..len), span[0] nearest dyn_top,
   rootward-first from the facts live at their base, threading the
   running facts upward: a frame's back link, a segment's displaced
   owning slot, and a non-frame node's namespace fill. Stamps never
   overwrite.
*/
static void kos_dyn_stamp_span(
    KosDynNode **span,
    size_t len,
    KosDynFrame *rft,
    KosSegment **rbl,
    PyObject *rns) {

  for (size_t i = len; i--; ) {
    KosDynNode *n = span[i];

    if (! KosDynNode_NS(n)) {
      /* fill non-frames only; a frame's NULL ns is the placeholder
         window and stays NULL until its real namespace is bound */
      if (rns && ! KosDynNode_IS_FRAME(n)) {
        Py_INCREF(rns);
        KosDynNode_NS(n) = rns;
      }
    } else {
      rns = KosDynNode_NS(n);
    }

    if (KosDynNode_IS_FRAME(n)) {
      if (! KosDynFrame_BACK_FRAME(n))
        KosDynFrame_BACK_FRAME(n) = (KosDynNode *) rft;
      rft = (KosDynFrame *) n;
    }

    if (KosDynNode_STACK(n)) {
      if (! KosSegment_BACK(KosDynNode_STACK(n)))
        KosSegment_BACK(KosDynNode_STACK(n)) = rbl;
      rbl = &KosDynNode_STACK(n);
    }
  }
}


/**
   Restamp the unwind caches of a span just spliced back onto the
   chain: the nodes from from down to stop, both included, with the
   facts live at stop's parent supplied by the caller (a resync of
   that parent derives the same three). The prefix restamp's scan for
   stamped evidence and its walk for the base facts are skipped; the
   caller knows the span's ends and holds the facts. Callers:
   kos_dyn_gen_install, where the consumer's registers recorded at the
   install are those facts.
*/
static void kos_dyn_restamp_span(
    KosDynNode *from,
    KosDynNode *stop,
    const KosLiveRegs *base) {

  KosDynNode *fixed[KOS_RESTAMP_FIXED];
  KosDynNode **span = fixed;
  size_t cap = KOS_RESTAMP_FIXED;
  size_t len = 0;
  KosDynNode *node;

  for (node = from; node; node = KosDynNode_PARENT(node)) {
    if (len == cap) {
      size_t ncap = cap * 2;
      KosDynNode **grown;
      if (span == fixed) {
        grown = PyMem_Malloc(ncap * sizeof(KosDynNode *));
        if (grown)
          memcpy(grown, span, len * sizeof(KosDynNode *));
      } else {
        grown = PyMem_Realloc(span, ncap * sizeof(KosDynNode *));
      }
      if (! grown)
        goto out;
      span = grown;
      cap = ncap;
    }
    span[len++] = node;
    if (node == stop)
      break;
  }

  kos_dyn_stamp_span(span, len, (KosDynFrame *) base->frame, base->block,
                     base->ns);

out:
  if (span != fixed)
    PyMem_Free(span);
}


void kos_dyn_resync_chain(KosEvalState *es) {

  KOS_TRACE_EVENTS_INC(KOS_TE_LIVE_REBUILD);

  KosDynFrame *ft = NULL;
  KosSegment **bl = NULL;
  PyObject *ns = NULL;
  KosDynNode *node;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es)
    goto done;

  node = KosEvalState_DYN_TOP(es);
  while (node) {
    if (! bl && KosDynNode_STACK(node))
      bl = &KosDynNode_STACK(node);

    /* under the owned fill, any carried ns is the live value */
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

  kos_dyn_resync_apply(es, ft, bl, ns, 1);

  kos_dyn_restamp_spliced_prefix(es);

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_rebuild_live_from_chain);
}


/**
   Link node as DYN_TOP, run install, retarget live fields.

   kos_dyn_resync_after_push updates es; the frame-crossing flush runs in
   kos_dyn_resync_apply when the pushed node is a frame.
*/
int kos_dyn_push(KosEvalState *es, KosDynNode *node) {
  // CONTEXT: dirty, touches context directly
  KosDynNode *prev;

  if (! node)
    return -1;

  kos_dyn_prepare_push(es, node);

  prev = KosEvalState_DYN_TOP(es);
  KosEvalState_DYN_TOP(es) = node;

  if (kos_dyn_install(es, node) < 0) {
    KosEvalState_DYN_TOP(es) = prev;
    return -1;
  }
  kos_dyn_resync_after_push(es, node);
  return 0;
}


/**
   Flush live PCs, set dyn_top to new_top, retarget live fields on es.

   Central commit for pops and any dyn_top publish that is not a push. Captures
   the old dyn_top as removed and calls kos_dyn_resync_after_pop. Use for
   kos_dyn_uninstall_core, fast kos_dyn_pop_* paths, gen install, and manual
   splices.
   Do not use for kos_dyn_push; use kos_dyn_resync_after_push via push instead.
*/
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top) {

  KosDynNode *removed;

  removed = KosEvalState_DYN_TOP(es);
  KosEvalState_DYN_TOP(es) = new_top;
  kos_dyn_resync_after_pop(es, removed, new_top);
}


/**
   Like kos_dyn_push but skips kos_dyn_install (region nodes, etc.).
*/
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node) {

  if (! node)
    return -1;

  kos_dyn_prepare_push(es, node);

  KosEvalState_DYN_TOP(es) = node;
  kos_dyn_resync_after_push(es, node);
  return 0;
}


/**
   kos_dyn_unlink_top with a slot for a detached RETURN obligation: the
   hook passes one, so a return parked on the node it settled resumes
   through the caller; every other caller passes NULL and a RETURN
   under KOS_XFER_EVENT_NONE is a fault.
*/
static KosOpResult kos_dyn_unlink_top_take(
    KosEvalState *es,
    KosXferEvent ev,
    int allow_barrier,
    KosObligation *out_return) {

  KosDynNode *node = KosEvalState_DYN_TOP(es);
  KosDynNode *parent;
  KosObligation ob;

  /* detached state, acted on once the node is gone */
  PyObject *deliver = NULL;   /* a with's body value */

  ob.kind = KOS_OB_NONE;

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return EVAL_RAISE();
  }
  if (! allow_barrier && KosDynNode_HAS_CAP(node, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return EVAL_RAISE();
  }

  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_CONTROL_HOOK)) {
    KosOpResult r = kos_dyn_extent_leave(es, node, ev);
    if (r.kind != KOS_RES_NORMAL)
      return r;
  } else {
    KosOpResult r = kos_dyn_uninstall_handler(es, node, ev);
    if (r.kind != KOS_RES_NORMAL)
      return r;
  }

  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_HOLDS_OBLIGATION)) {
    KosObligation *slot = kos_dyn_node_ob(node);
    ob = *slot;
    slot->kind = KOS_OB_NONE;
  }

  parent = KosDynNode_PARENT(node);
  kos_dyn_commit_dyn_top(es, parent);
  KosDynNode_PARENT(node) = NULL;
  kos_dynnode_free(node);

  /* Step 5: act on the obligation by kind and event. */
  switch (ob.kind) {
    case KOS_OB_EXCEPTION:
      if (ev == KOS_XFER_EVENT_NONE || ev == KOS_XFER_EVENT_RAISE) {
        /* the leave that ran under this exception is done, or died:
           propagation continues, chained under whatever superseded it */
        return kos_dyn_ob_resume_exception(&ob);
      }
      kos_dyn_ob_clear(&ob);
      return EVAL_NORMAL();

    case KOS_OB_RETURN:
      if (ev != KOS_XFER_EVENT_NONE) {
        /* a raise or a transfer crossed the return: it is void */
        kos_dyn_ob_clear(&ob);
        return EVAL_NORMAL();
      }
      if (! out_return) {
        kos_dyn_ob_clear(&ob);
        PyErr_SetString(PyExc_SystemError,
                        "a parked return was unlinked with no one to"
                        " resume it");
        return EVAL_RAISE();
      }
      *out_return = ob;
      return EVAL_NORMAL();

    case KOS_OB_VALUE:
      deliver = ob.u.value;
      break;

    case KOS_OB_SWALLOWED:
      /* __exit__ consumed the raise: None stands in for the body value
         at the with's resume pc, whatever event brought the raise */
      if (KosEvalState_StackPushRef(es, Py_None) < 0)
        return EVAL_RAISE();
      return EVAL_NORMAL();

    default:
      break;
  }

  if (deliver) {
    if (ev != KOS_XFER_EVENT_NONE) {
      /* a return, raise, or transfer crossed the extent: the body
         value is not the value of anything */
      Py_DECREF(deliver);
      return EVAL_NORMAL();
    }
    if (KosEvalState_StackPushRef(es, deliver) < 0) {
      Py_DECREF(deliver);
      return EVAL_RAISE();
    }
    Py_DECREF(deliver);
  }
  return EVAL_NORMAL();
}


KosOpResult kos_dyn_unlink_top(
    KosEvalState *es,
    KosXferEvent ev,
    int allow_barrier) {

  return kos_dyn_unlink_top_take(es, ev, allow_barrier, NULL);
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

   :return: 0 on success, -1 on error
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
  (void) event;
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PARAM:
      kos_dyn_param_unwind(es, (KosDynParam *) node);
      return EVAL_NORMAL();
    case KOS_DYN_YIELD_FROM:
      kos_dyn_yieldfrom_unwind(es, (KosDynYieldFrom *) node);
      return EVAL_NORMAL();
    case KOS_DYN_TRY:
    case KOS_DYN_FINALLY:
      kos_dyn_region_leave_exc(node);
      return EVAL_NORMAL();
    default:
      return EVAL_NORMAL();
  }
}


/**
   Offer a control transit to dyn_top without unlinking: PASS, CONSUMED,
   DEFERRED, or ERROR. Central dispatch for exception/return/yield/shift
   semantics (TRY/FINALLY except/finally PCs, GEN node, PROMPT shift).
   Callers: the kos_dyn_unwind_* walks (raise, return, yield, to_frame),
   shift walk.

   TRY: CONSUMED when RAISE, enabled, PyErr set, and extent has except_pc.
   FINALLY: CONSUMED when RAISE or RETURN and finally enabled.
*/
KosHandleResult kos_dyn_handle(KosEvalState *es, KosDynNode *node,
                               KosXferEvent event) {

  KOS_TRACE_EVENTS_INC(KOS_TE_DYN_HANDLE_ENTER);

  /* A receiving iteration node interprets the step-ending exception
     from its in-flight step as end-of-iteration -- StopIteration from a
     flattened generator, StopAsyncIteration from a suspended python
     async iterator -- the same protocol judgment the eager step path
     makes when classifying GET_NEXT_EXHAUSTED. The capability is the
     routing key and is held exactly while a step is in flight, so the
     same exception raised by the consumer's own body (iteration node idle)
     passes through. */
  if (event == KOS_XFER_EVENT_RAISE
      && KosDynNode_HAS_CAP(node, KOS_DYN_CAP_ITER_RECEIVER)
      && PyErr_Occurred()) {
    if (PyErr_ExceptionMatches(kos_dyn_iter_node_exhaustion_exc(node)))
      return KOS_HANDLE_CONSUMED;

    if (kos_dyn_iter_node_value_mode(node)) {
      /* A non-protocol error escaping a value-grafted invoke: release
         the provider's busy mark and disarm before the raise passes to
         the consumer's own handlers. */
      kos_dyn_iter_request_finish(node, 0);
      kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
    }
  }

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
        kos_raise_prompt_unreachable("generator");
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
   the current frame. Callers: kos_dyn_unwind_raise when routing RAISE
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
static KosDynNode *kos_dyn_clone_dispatch(KosDynNode *node) {
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

    case KOS_DYN_GEN:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot clone generator node node");
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


KosDynNode *kos_dyn_clone(KosDynNode *node) {
  if (! node)
    return NULL;

  KosDynNode *cloned = kos_dyn_clone_dispatch(node);

  /* Filled namespaces are copied generically: a per-kind clone that
     attached no ns of its own (every non-introducing kind) carries
     the same live-ns fill its source held. Introducing kinds handle
     their own (frame goes through KosNamespace_Reveal). */
  if (cloned && ! KosDynNode_NS(cloned) && KosDynNode_NS(node)) {
    Py_INCREF(KosDynNode_NS(node));
    KosDynNode_NS(cloned) = KosDynNode_NS(node);
  }

  /* Segment copy is centralized here for clones built through this
     dispatcher; kos_dynframe_clone copies its own segment so its
     direct callers (delim return-frame snapshot, capture baseline)
     stay faithful too. */
  if (cloned && kos_dyn_clone_copy_segment(cloned, node) < 0) {
    kos_dynnode_free(cloned);
    return NULL;
  }

  return cloned;
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
  kos_dyn_yield_from_disarm(es, node);
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
   Predicate: does the chain from dyn_top reach target?  Read-only; never
   sets PyErr.  Callers: kos_validate_capture_cc, kos_dyn_wind_on_control_returned,
   the NEXT and YIELD_FROM still-on-chain checks.

   :return: 1 if target is on the chain from dyn_top, 0 otherwise
*/
int kos_dyn_reaches(KosEvalState *es, const KosDynNode *target) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  while (node && node != target) {
    node = KosDynNode_PARENT(node);
  }

  return node != NULL;
}




/**
   Innermost CC node from dyn_top. Callers: kos_capture_cc.
*/
KosDynCC *kos_dyn_find_cc(KosEvalState *es) {
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
   The one raising wrapper over kos_dyn_reaches.
*/
static int kos_validate_capture_cc(KosEvalState *es, KosDynCC *cc) {
  if (! kos_dyn_reaches(es, (KosDynNode *) cc)) {
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
   pc/stack, graft id, one-shot flag, token ref. Callers: kos_shift_capture,
   kos_capture_cc, kos_continuation_clone.
*/
static KosContinuation *kos_continuation_create(
    KosDynNode *captured_chain,
    uint32_t resume_pc,
    KosSegment *resume_stack,
    uint32_t prompt_id,
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
  KosContinuation_ONE_SHOT(k) = one_shot;
  KosContinuation_TOKEN_REF(k) = token_ref;
  if (KosContinuation_TOKEN_REF(k))
    Py_INCREF(KosContinuation_TOKEN_REF(k));

  return k;
}


/**
   Clear the parent link at the bottom of a cloned template chain.

   kos_dyn_clone_segment leaves the clone's innermost parent pointing
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
static inline KosSegment *kos_chain_resume_stack(
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
   supplies prompt_id, one_shot, and no_frame_err text.

   baseline_src, when non-NULL, is cloned and appended at the template
   bottom, below the prompt clone; baseline_ns, when non-NULL, replaces
   that clone's namespace with the one live at the capture bound. It is the compressed region bottom for
   a capture bounded at an invocation prompt: the frame executing the
   region's own code lies below the capture point and must not be
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
    int one_shot,
    KosDynFrame *baseline_src,
    PyObject *baseline_ns,
    const char *no_frame_err) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *captured_chain;
  KosDynFrame *resume_frame;
  KosSegment *resume_stack;
  KosContinuation *k;
  PyObject *token;

  captured_chain = kos_dyn_clone_segment(segment_top, stop);
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
     cannot cross a generator node), a PROMPT consumes -- the match
     when its tag agrees, a topology error otherwise. Reachability is
     implied by the walk itself. */
  KosDynPrompt *prompt = NULL;
  {
    KosDynNode *node = KosEvalState_DYN_TOP(es);

    while (node) {
      KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_SHIFT);

      if (hr == KOS_HANDLE_ERROR) {
        kos_capture_note_prompt_id(prompt_id);
        return -1;
      }
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
  if (! prompt)
    return kos_raise_prompt_not_found(prompt_id);

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
               ? (size_t) KosSegment_LEN(KosDynFrame_STACK(f)) : 0);
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
            KosSegment *ops =
                KosStack_DeepCopy(KosDynNode_STACK(owner));
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
       * SHIFT and its true delimiter. Beyond the delimiting prompt, the
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

  KosDynCC *cc = kos_dyn_find_cc(es);
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
      || snapshot_set_obj(out, "namespace",
          kos_dynnode_introduces_ns(node) ? KosDynNode_NS(node) : NULL) < 0) {
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

  /* The base ns and stack fields are uniformly owned; visit them
     here, once, for every kind. Per-kind traversals cover only their
     payloads. A namespace that was never revealed is untracked, so its
     contents are reported through the node that introduced it, and
     only there: every other node holds that same namespace as its
     live-ns fill, and reporting it again would count each of its
     references once per holder. Its parent is either revealed or
     introduced by another node on the chain, which reports it. Only a
     KOS_DYN_CAP_BINDS_NS node introduces one: a code frame, a let, a
     collect. A native or code-less frame carries a fill of the live
     namespace like any other non-introducer, and a parked function
     frame keeps the cap with the namespace it still holds. */
  Py_VISIT(KosDynNode_NS(node));
  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_BINDS_NS)) {
    int tr = KosNamespace_TraverseUntracked(KosDynNode_NS(node), visit, arg);
    if (tr != 0)
      return tr;
  }
  {
    int tr = KosStack_Traverse(KosDynNode_STACK(node), visit, arg);
    if (tr != 0)
      return tr;
  }

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
    KosSegment **slot = NULL;
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
    if (KosStack_PushRef(slot, value) < 0) {
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
      kos_dyn_resync_load_regs(es, ret_frame);
    else
      kos_dyn_resync_chain(es);
  } else {
    /* DELIM not dyn_top: pop already done; full rebuild for live coherence. */
    kos_dyn_resync_chain(es);
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

  /* Drop the spent slice stack; restore the displaced invoke-time state.
     The restored segment's install-time back link predates the
     displacement; the chain below may have been restitched since, so
     clear it and let the walk answer. */
  {
    KosSegment *spent = KosDynFrame_STACK(resume_frame);
    KosDynFrame_STACK(resume_frame) = KosDynDelim_SAVED_STACK(delim);
    if (KosDynFrame_STACK(resume_frame))
      /* the install-time link predates the displacement; recompute
         the enclosing slot at the restored position */
      kos_dyn_reslot_segment_back((KosDynNode *) resume_frame);
    KosDynDelim_SAVED_STACK(delim) = NULL;
    KosDynFrame_PC(resume_frame) = KosDynDelim_SAVED_PC(delim);
    if (spent)
      KosStack_Release(spent);
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
      /* The snapshot was taken of the frame nearest the shift, which
         may have sat above other frames in the region; here it stands
         directly above the prompt. */
      KosDynFrame_DEPTH(ret_frame) =
          (uint16_t) (kos_dyn_frame_depth_at(prompt) + 1);
      base = (KosDynNode *) ret_frame;
    }

    while (KosDynNode_PARENT(bottom))
      bottom = KosDynNode_PARENT(bottom);
    KosDynNode_PARENT(bottom) = base;
    KosDynDelim_RETURN_CHAIN(delim) = NULL;

    /* The re-linked span's unwind-cache back links predate the stash;
       clear them and let the walk answer. */
    kos_dyn_clear_unwind_caches(span, base);
  }

  KosDynFrame_PC(ret_frame) = ret_pc;
  KosDynFrame_ACTING_PC(ret_frame) = KosDynDelim_SAVED_ACTING_PC(delim);
  kos_dyn_commit_dyn_top(es, span);

  KosDynNode_PARENT((KosDynNode *) delim) = NULL;
  kos_dyndelim_free(delim);

  kos_dyn_resync_chain(es);
  /* rebuild leaves live PCs alone when the frame is unchanged; load the
   * invoke-site return pc explicitly. */
  if (KosEvalState_FRAME(es) == ret_frame)
    kos_dyn_resync_load_regs(es, ret_frame);

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
  return 0;
}


// The end.
