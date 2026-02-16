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

   Dynamic chain infrastructure implementation. Provides DynNode
   allocation, freeing, cloning, and chain management operations.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "debug.h"

#include <stdint.h>
#include <string.h>

/* Set to 1 to trace transfer plan steps (for debugging).  All trace output
 * goes through SAYF from debug.h. */
#define KOS_XFER_TRACE 0


/* === DynNode base operations === */


static void kos_dynnode_init(
    KosDynNode *node,
    KosDynKind kind,
    KosDynNode *parent) {

  KosDynNode_PARENT(node) = parent;
  KosDynNode_KIND(node) = kind;
  KosDynNode_FLAGS(node) = KOS_DYNF_NONE;
}


#define DYN_NODE_MAX_FREE 128  /* cap for cached KosDynNode; tunable */

static size_t dyn_freecount = 0;
static KosDynNode *dyn_freelist = NULL;


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
    memset(p, 0, sizeof(KosDynNode));
    return p;
  }
  p = PyMem_Malloc(sizeof(KosDynNode));
  if (! p) {
    PyErr_NoMemory();
    return NULL;
  }
  memset(p, 0, sizeof(KosDynNode));
  return p;
}


/**
   Return a node to the freelist or free it. Call only after kind-specific
   cleanup (refs released). Node is zeroed before being cached.
*/
static void kos_dynnode_return_to_freelist(KosDynNode *node) {
  if (dyn_freecount < DYN_NODE_MAX_FREE) {
    memset(node, 0, sizeof(KosDynNode));
    KosDynNode_PARENT(node) = dyn_freelist;
    dyn_freelist = node;
    dyn_freecount++;
  } else {
    PyMem_Free(node);
  }
}


/* === KosDynFrame implementation === */


KosDynFrame *kos_dynframe_new(
    KosDynNode *parent,
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack) {

  KosDynFrame *frame = kos_dynnode_alloc();
  if (! frame) {
    Py_XDECREF(ns);
    return NULL;
  }

  kos_dynnode_init(frame, KOS_DYN_FRAME, parent);
  KosDynFrame_CODE(frame) = code;  /* borrowed */
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_STACK(frame) = stack;  /* ownership transferred */
  KosDynFrame_NS(frame) = ns;  /* ownership transferred (already stole ref) */
  KosDynFrame_FLAGS(frame) = 0;
  return frame;
}


KosDynFrame *kos_dynframe_clone(KosDynFrame *frame) {
  if (! frame)
    return NULL;

  KosStackBlock *cloned_stack = KosStackBlock_COW(KosDynFrame_STACK(frame));

  KosDynFrame *clone = kos_dynnode_alloc();
  if (! clone) {
    if (cloned_stack) {
      KosStackBlock *b = cloned_stack;
      while (b) {
        KosStackBlock *p = KosStackBlock_PARENT(b);
        KosStackBlock_Release(b);
        b = p;
      }
    }
    return NULL;
  }

  kos_dynnode_init(clone, KOS_DYN_FRAME, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(frame) | KOS_DYNF_CLONED;
  KosDynFrame_CODE(clone) = KosDynFrame_CODE(frame);  /* borrowed */
  KosDynFrame_PC(clone) = KosDynFrame_PC(frame);
  KosDynFrame_STACK(clone) = cloned_stack;
  KosDynFrame_NS(clone) = KosDynFrame_NS(frame);
  Py_XINCREF(KosDynFrame_NS(clone));
  KosDynFrame_FLAGS(clone) = KosDynFrame_FLAGS(frame);
  return clone;
}


void kos_dynframe_free(KosDynFrame *frame) {
  if (! frame)
    return;

  /* Release the stack block chain */
  KosStackBlock *block = KosDynFrame_STACK(frame);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  Py_XDECREF(KosDynFrame_NS(frame));
  /* code is borrowed, don't decref */

  kos_dynnode_return_to_freelist(frame);
}


/* === KosDynPrompt implementation === */


KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent);
  KosDynPrompt_ID(prompt) = prompt_id;
  KosDynPrompt_HANDLER_CODE(prompt) = NULL;
  KosDynPrompt_HANDLER_PC(prompt) = 0;
  return prompt;
}


static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_PROMPT, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(prompt) | KOS_DYNF_CLONED;
  KosDynPrompt_ID(clone) = KosDynPrompt_ID(prompt);
  KosDynPrompt_HANDLER_CODE(clone) = KosDynPrompt_HANDLER_CODE(prompt);  /* borrowed */
  KosDynPrompt_HANDLER_PC(clone) = KosDynPrompt_HANDLER_PC(prompt);
  return clone;
}


void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  /* handler_code is borrowed */
  kos_dynnode_return_to_freelist(prompt);
}


/* === KosDynCC implementation === */


KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = kos_dynnode_alloc();
  if (! cc)
    return NULL;

  kos_dynnode_init(cc, KOS_DYN_CC, parent);
  KosDynCC_RESUME_PC(cc) = resume_pc;
  return cc;
}


static KosDynCC *kos_dyncc_clone(KosDynCC *cc) {
  if (! cc)
    return NULL;

  KosDynCC *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_CC, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(cc) | KOS_DYNF_CLONED;
  KosDynCC_RESUME_PC(clone) = KosDynCC_RESUME_PC(cc);
  return clone;
}


void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  kos_dynnode_return_to_freelist(cc);
}


/* === KosDynWind implementation === */


KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    KosCode *before_code,
    uint32_t before_pc,
    KosCode *after_code,
    uint32_t after_pc) {

  KosDynWind *wind = kos_dynnode_alloc();
  if (! wind)
    return NULL;

  kos_dynnode_init(wind, KOS_DYN_WIND, parent);
  KosDynWind_BEFORE_CODE(wind) = before_code;  /* borrowed */
  KosDynWind_BEFORE_PC(wind) = before_pc;
  KosDynWind_AFTER_CODE(wind) = after_code;  /* borrowed */
  KosDynWind_AFTER_PC(wind) = after_pc;
  KosDynWind_ENTERED(wind) = 0;
  return wind;
}


KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  KosDynWind *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_WIND, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(wind) | KOS_DYNF_CLONED;
  KosDynWind_BEFORE_CODE(clone) = KosDynWind_BEFORE_CODE(wind);  /* borrowed */
  KosDynWind_BEFORE_PC(clone) = KosDynWind_BEFORE_PC(wind);
  KosDynWind_AFTER_CODE(clone) = KosDynWind_AFTER_CODE(wind);  /* borrowed */
  KosDynWind_AFTER_PC(clone) = KosDynWind_AFTER_PC(wind);
  KosDynWind_ENTERED(clone) = KosDynWind_ENTERED(wind);
  return clone;
}


void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  /* code pointers are borrowed */
  kos_dynnode_return_to_freelist(wind);
}


/* === KosDynRegion implementation === */


KosDynRegion *kos_dynregion_new(
    KosDynNode *parent,
    KosRegionStyle style,
    uint32_t break_pc,
    uint32_t continue_pc) {

  KosDynRegion *region = kos_dynnode_alloc();
  if (! region)
    return NULL;

  kos_dynnode_init(region, KOS_DYN_REGION, parent);
  KosDynRegion_STYLE(region) = style;
  KosDynRegion_BREAK_PC(region) = break_pc;
  KosDynRegion_CONTINUE_PC(region) = continue_pc;
  KosDynRegion_ENABLED(region) = 1;
  KosDynRegion_RESULT(region) = NULL;
  KosDynRegion_ITER(region) = NULL;
  KosDynRegion_EXHAUSTED(region) = 0;
  KosDynRegion_HANDLING_RAISE(region) = 0;
  KosDynRegion_ITER_NEXT_ELSE_PC(region) = 0;
  return region;
}


KosDynRegion *kos_dynregion_clone(KosDynRegion *region) {
  if (! region)
    return NULL;

  KosDynRegion *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_REGION, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(region) | KOS_DYNF_CLONED;
  KosDynRegion_STYLE(clone) = KosDynRegion_STYLE(region);
  KosDynRegion_BREAK_PC(clone) = KosDynRegion_BREAK_PC(region);
  KosDynRegion_CONTINUE_PC(clone) = KosDynRegion_CONTINUE_PC(region);
  KosDynRegion_ENABLED(clone) = KosDynRegion_ENABLED(region);
  KosDynRegion_RESULT(clone) = KosDynRegion_RESULT(region);
  Py_XINCREF(KosDynRegion_RESULT(clone));
  KosDynRegion_ITER(clone) = KosDynRegion_ITER(region);
  Py_XINCREF(KosDynRegion_ITER(clone));
  KosDynRegion_EXHAUSTED(clone) = KosDynRegion_EXHAUSTED(region);
  KosDynRegion_HANDLING_RAISE(clone) = KosDynRegion_HANDLING_RAISE(region);
  KosDynRegion_ITER_NEXT_ELSE_PC(clone) = KosDynRegion_ITER_NEXT_ELSE_PC(region);
  return clone;
}


void kos_dynregion_free(KosDynRegion *region) {
  if (! region)
    return;

  /* DECREF payload owned references */
  Py_XDECREF(KosDynRegion_RESULT(region));
  Py_XDECREF(KosDynRegion_ITER(region));

  kos_dynnode_return_to_freelist(region);
}


/* === KosDynNs (namespace scope) implementation === */


KosDynNs *kos_dynns_new(KosDynNode *parent, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynnode_alloc();
  if (! ns_node) {
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init(ns_node, KOS_DYN_NS, parent);
  KosDynNs_SAVED_NS(ns_node) = saved_ns;  /* caller transferred ownership */
  return ns_node;
}


KosDynNs *kos_dynns_clone(KosDynNs *ns_node) {
  if (! ns_node)
    return NULL;

  PyObject *saved = KosDynNs_SAVED_NS(ns_node);
  Py_INCREF(saved);
  return kos_dynns_new(NULL, saved);
}


void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  Py_DECREF(KosDynNs_SAVED_NS(ns_node));
  kos_dynnode_return_to_freelist(ns_node);
}


/* === KosDynPycall (boundary marker) implementation === */


KosDynNode *kos_dynpycall_new(KosDynNode *parent) {

  KosDynNode *node = kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_PYCALL, parent);
  KosDynNode_FLAGS(node) |= KOS_DYNF_BOUNDARY;
  return node;
}


void kos_dynpycall_free(KosDynNode *node) {
  if (! node)
    return;

  kos_dynnode_return_to_freelist(node);
}


/* === KosDynYieldFrom implementation === */


KosDynYieldFrom *kos_dynyieldfrom_new(
    KosDynNode *parent,
    PyObject *delegate,
    uint8_t kind) {

  KosDynYieldFrom *node = kos_dynnode_alloc();
  if (! node) {
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_YIELD_FROM, parent);
  Py_INCREF(delegate);
  KosDynYieldFrom_DELEGATE(node) = delegate;
  KosDynYieldFrom_KIND(node) = kind;
  KosDynYieldFrom_PENDING_ACTION(node) = 0;
  KosDynYieldFrom_PENDING_ARG(node) = NULL;
  KosDynYieldFrom_INFLIGHT(node) = NULL;
  return node;
}


void kos_dynyieldfrom_free(KosDynYieldFrom *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynYieldFrom_DELEGATE(node));
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(node));
  Py_XDECREF(KosDynYieldFrom_INFLIGHT(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


KosDynYieldFrom *kos_dynyieldfrom_clone(KosDynYieldFrom *node) {

  KosDynYieldFrom *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_YIELD_FROM, NULL);
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynYieldFrom_DELEGATE(clone) = KosDynYieldFrom_DELEGATE(node);
  Py_XINCREF(KosDynYieldFrom_DELEGATE(clone));
  KosDynYieldFrom_KIND(clone) = KosDynYieldFrom_KIND(node);
  KosDynYieldFrom_PENDING_ACTION(clone) = KosDynYieldFrom_PENDING_ACTION(node);
  KosDynYieldFrom_PENDING_ARG(clone) = KosDynYieldFrom_PENDING_ARG(node);
  if (KosDynYieldFrom_PENDING_ARG(clone))
    Py_INCREF(KosDynYieldFrom_PENDING_ARG(clone));
  KosDynYieldFrom_INFLIGHT(clone) = KosDynYieldFrom_INFLIGHT(node);
  if (KosDynYieldFrom_INFLIGHT(clone))
    Py_INCREF(KosDynYieldFrom_INFLIGHT(clone));
  return clone;
}


/* === Generic DynNode operations === */


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
    case KOS_DYN_WIND:
      kos_dynwind_free((KosDynWind *) node);
      break;
    case KOS_DYN_REGION:
      kos_dynregion_free((KosDynRegion *) node);
      break;
    case KOS_DYN_NS:
      kos_dynns_free((KosDynNs *) node);
      break;
    case KOS_DYN_PYCALL:
      kos_dynpycall_free(node);
      break;
    case KOS_DYN_CC:
      kos_dyncc_free((KosDynCC *) node);
      break;
    case KOS_DYN_YIELD_FROM:
      kos_dynyieldfrom_free((KosDynYieldFrom *) node);
      break;
  }
}


KosDynNode *kos_dynnode_clone(KosDynNode *node) {
  if (! node)
    return NULL;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME:
      return (KosDynNode *) kos_dynframe_clone((KosDynFrame *) node);
    case KOS_DYN_PROMPT:
      return (KosDynNode *) kos_dynprompt_clone((KosDynPrompt *) node);
    case KOS_DYN_WIND:
      return (KosDynNode *) kos_dynwind_clone((KosDynWind *) node);
    case KOS_DYN_REGION:
      return (KosDynNode *) kos_dynregion_clone((KosDynRegion *) node);
    case KOS_DYN_NS:
      return (KosDynNode *) kos_dynns_clone((KosDynNs *) node);
    case KOS_DYN_PYCALL:
      return kos_dynpycall_new(NULL);
    case KOS_DYN_CC:
      return (KosDynNode *) kos_dyncc_clone((KosDynCC *) node);
    case KOS_DYN_YIELD_FROM:
      return (KosDynNode *) kos_dynyieldfrom_clone((KosDynYieldFrom *) node);
  }

  PyErr_Format(PyExc_RuntimeError, "cannot clone dynnode kind %u",
               (unsigned) KosDynNode_KIND(node));
  return NULL;
}


void kos_dynchain_free(KosDynNode *node, KosDynNode *stop) {

  while (node && node != stop) {
    KosDynNode *parent = node->parent;
    kos_dynnode_free(node);
    node = parent;
  }
}


KosDynNode *kos_clone_chain_segment(KosDynNode *top, KosDynNode *stop) {
  if (! top || top == stop)
    return NULL;

  /* Clone nodes from top to stop, linking them together */
  KosDynNode *cloned_top = NULL;
  KosDynNode *cloned_prev = NULL;

  KosDynNode *node = top;
  while (node && node != stop) {
    if (KOS_XFER_TRACE)
      SAYF("[xfer] clone_segment cloning node %p kind=%u",
           (void *) node, (unsigned) KosDynNode_KIND(node));
    KosDynNode *cloned = kos_dynnode_clone(node);
    if (! cloned) {
      if (KOS_XFER_TRACE)
        SAY("[xfer] clone_segment kos_dynnode_clone failed");
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


/* === Context operations === */


/**
   Update the frame_top cache after chain modifications.
   Walks from dyn_top to find the topmost FRAME node.
*/
static void kos_update_frame_top(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);
  while (node) {
    if (KosDynNode_IS_FRAME(node)) {
      KosContext_FRAME_TOP(ctx) = (KosDynFrame *) node;
      return;
    }
    node = KosDynNode_PARENT(node);
  }
  KosContext_FRAME_TOP(ctx) = NULL;
}


KosDynFrame *kos_dyn_push_frame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  KosDynFrame *frame = kos_dynframe_new(KosContext_DYN_TOP(ctx), code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;

  return frame;
}


int kos_dyn_pop_frame(KosContext *ctx) {
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to pop");
    return -1;
  }

  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }

  KosDynNode *old_top = KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(old_top);
  KosContext_CHAIN_VERSION(ctx)++;

  kos_dynnode_free(old_top);
  kos_update_frame_top(ctx);

  return 0;
}


/**
   Attach an existing frame to the context's dynamic chain (no allocation).

   Reparents the frame so its parent is KosContext_DYN_TOP(ctx), then sets it as
   dyn_top/frame_top. Used to run a generator frame in the current context
   (e.g. async gen step). Does not free the frame on detach.

   :param ctx: execution context
   :param frame: existing frame (not freed by this module)

   :return: 0 on success, -1 on error
*/
int kos_dyn_attach_frame(KosContext *ctx, KosDynFrame *frame) {
  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "NULL frame for attach");
    return -1;
  }
  KosDynNode_PARENT(frame) = KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;
  return 0;
}


/**
   Detach the top frame from the context's dynamic chain without freeing it.

   Sets KosContext_DYN_TOP(ctx) to the frame's parent and updates frame_top. Clears the
   detached frame's parent link so it is fully disconnected from the chain.
   The frame is returned in *frame_out; caller may keep it (e.g. for async
   gen storage in agen->ctx).

   :param ctx: execution context
   :param frame_out: output; set to the detached frame (may be NULL)

   :return: 0 on success, -1 if top is not a frame
*/
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out) {
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to detach");
    return -1;
  }
  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }
  KosDynFrame *frame = (KosDynFrame *) KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(frame);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);
  KosDynNode_PARENT(frame) = NULL;
  if (frame_out)
    *frame_out = frame;
  return 0;
}


KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynprompt_new(KosContext_DYN_TOP(ctx), prompt_id);
  if (! prompt)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) prompt;
  KosContext_CHAIN_VERSION(ctx)++;

  return prompt;
}


void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    /* Prompt is not at top - this is an error but we handle gracefully */
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(prompt);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynprompt_free(prompt);
}


KosDynCC *kos_dyn_push_cc(KosContext *ctx, uint32_t resume_pc) {

  KosDynCC *cc = kos_dyncc_new(KosContext_DYN_TOP(ctx), resume_pc);
  if (! cc)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) cc;
  KosContext_CHAIN_VERSION(ctx)++;

  return cc;
}


void kos_dyn_pop_cc(KosContext *ctx, KosDynCC *cc) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) cc) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(cc);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncc_free(cc);
}


KosDynWind *kos_dyn_push_wind(
    KosContext *ctx,
    KosCode *before_code,
    uint32_t before_pc,
    KosCode *after_code,
    uint32_t after_pc) {

  KosDynWind *wind = kos_dynwind_new(KosContext_DYN_TOP(ctx),
                                     before_code, before_pc,
                                     after_code, after_pc);
  if (! wind)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) wind;
  KosContext_CHAIN_VERSION(ctx)++;

  return wind;
}


int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) wind) {
    PyErr_SetString(PyExc_RuntimeError, "wind node is not at top of chain");
    return -1;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(wind);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynwind_free(wind);

  return 0;
}


KosDynRegion *kos_dyn_push_region(
        KosContext *ctx,
        KosRegionStyle style,
        uint32_t break_pc,
        uint32_t continue_pc) {

  KosDynRegion *region = kos_dynregion_new(KosContext_DYN_TOP(ctx),
                                           style, break_pc, continue_pc);
  if (! region)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) region;
  KosContext_CHAIN_VERSION(ctx)++;

  return region;
}


void kos_dyn_pop_region(KosContext *ctx, KosDynRegion *region) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(region);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynregion_free(region);
}


KosDynNs *kos_dyn_push_ns(KosContext *ctx, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynns_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! ns_node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) ns_node;
  KosContext_CHAIN_VERSION(ctx)++;

  return ns_node;
}


void kos_dyn_pop_ns(KosContext *ctx, KosDynNs *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynns_free(node);
}


/**
   Pop the top node of the dynamic chain. Dispatches on node kind.
   For NS, restores frame_top->ns from the node's saved_ns before popping.

   :return: 0 on success, -1 on error or if top is PROMPT/PYCALL (cannot unwind)
*/
int kos_dyn_pop_top(KosContext *ctx) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return -1;
  }

  switch (KosDynNode_KIND(top)) {
  case KOS_DYN_FRAME:
    return kos_dyn_pop_frame(ctx);

  case KOS_DYN_WIND:
    return kos_dyn_pop_wind(ctx, (KosDynWind *) top);

  case KOS_DYN_REGION:
    kos_dyn_pop_region(ctx, (KosDynRegion *) top);
    return 0;

  case KOS_DYN_NS: {
    KosDynNs *ns_node = (KosDynNs *) top;
    if (KosContext_FRAME_TOP(ctx)) {
      PyObject *prior = KosDynNs_SAVED_NS(ns_node);
      Py_INCREF(prior);
      Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
      KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
    }
    kos_dyn_pop_ns(ctx, ns_node);
    return 0;
  }

  case KOS_DYN_PROMPT:
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot unwind past prompt");
    return -1;

  case KOS_DYN_CC:
    kos_dyn_pop_cc(ctx, (KosDynCC *) top);
    return 0;

  case KOS_DYN_PYCALL:
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot unwind past Python call boundary");
    return -1;

  case KOS_DYN_YIELD_FROM: {
    KosDynYieldFrom *yf = (KosDynYieldFrom *) top;
    kos_yieldfrom_close_delegate(yf);
    KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(top);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_update_frame_top(ctx);
    kos_dynyieldfrom_free(yf);
    return 0;
  }
  }

  PyErr_SetString(PyExc_RuntimeError, "unknown dynamic chain node kind");
  return -1;
}


KosDynRegion *kos_find_region_for_raise(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

  while (node) {
    if (KosDynNode_IS_REGION(node)) {
      KosDynRegion *region = (KosDynRegion *) node;
      if (KosDynRegion_ENABLED(region) &&
          (KosDynRegion_STYLE(region) == KOS_REGION_TRY ||
           KosDynRegion_STYLE(region) == KOS_REGION_FINALLY)) {
        return region;
      }
    }
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


KosDynRegion *kos_find_region_for_loop(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

  while (node) {
    if (KosDynNode_IS_REGION(node)) {
      KosDynRegion *region = (KosDynRegion *) node;
      if (KosDynRegion_STYLE(region) == KOS_REGION_LOOP ||
          KosDynRegion_STYLE(region) == KOS_REGION_FOREACH) {
        return region;
      }
    }
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


KosDynNode *kos_dyn_push_pycall(KosContext *ctx) {

  KosDynNode *marker = kos_dynpycall_new(KosContext_DYN_TOP(ctx));
  if (! marker)
    return NULL;

  KosContext_DYN_TOP(ctx) = marker;
  KosContext_CHAIN_VERSION(ctx)++;

  return marker;
}


void kos_dyn_pop_pycall(KosContext *ctx, KosDynNode *marker) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != marker) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(marker);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynpycall_free(marker);
}


KosDynYieldFrom *kos_dyn_push_yield_from(
    KosContext *ctx,
    PyObject *delegate,
    uint8_t kind) {

  KosDynYieldFrom *node = kos_dynyieldfrom_new(
      KosContext_DYN_TOP(ctx), delegate, kind);
  if (! node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


void kos_dyn_pop_yield_from(KosContext *ctx, KosDynYieldFrom *node) {

  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);
  kos_dynyieldfrom_free(node);
}


/* === Prompt lookup and validation === */


KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

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


int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

  while (node && node != (KosDynNode *) prompt) {
    if (KosDynNode_IS_PYCALL(node)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot capture continuation across Python boundary");
      return -1;
    }
    node = KosDynNode_PARENT(node);
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found in chain");
    return -1;
  }

  return 0;
}


KosDynCC *kos_find_cc(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

  while (node) {
    if (KosDynNode_IS_CC(node))
      return (KosDynCC *) node;
    node = KosDynNode_PARENT(node);
  }

  return NULL;
}


static int kos_validate_capture_cc(KosContext *ctx, KosDynCC *cc) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);

  while (node && node != (KosDynNode *) cc) {
    if (KosDynNode_IS_PYCALL(node)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot capture continuation across Python boundary");
      return -1;
    }
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
   resume_stack is stored as-is (caller must have taken COW ref if needed).
   token_ref is INCREF'd. Returns NULL on alloc failure (PyErr_NoMemory set);
   caller must free captured_chain on failure.
*/
#define CONT_MAX_FREE 64  /* cap for cached KosContinuation; tunable */

static size_t cont_freecount = 0;
static KosContinuation *cont_freelist = NULL;


static KosContinuation *kos_continuation_create(
    KosDynNode *captured_chain,
    KosDynFrame *resume_frame,
    uint32_t resume_pc,
    KosStackBlock *resume_stack,
    uint32_t prompt_id,
    int one_shot,
    PyObject *token_ref) {

  KosContinuation *k = cont_freelist;
  if (k) {
    cont_freelist = (KosContinuation *) KosContinuation_CAPTURED_CHAIN(k);
    cont_freecount--;
    memset(k, 0, sizeof(KosContinuation));
  } else {
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
  KosContinuation_ONE_SHOT(k) = one_shot;
  KosContinuation_TOKEN_REF(k) = token_ref;
  if (KosContinuation_TOKEN_REF(k))
    Py_INCREF(KosContinuation_TOKEN_REF(k));

  return k;
}


int kos_shift_capture(
    KosContext *ctx,
    uint32_t prompt_id,
    KosContinuation **out_k) {

  /* Find the prompt */
  KosDynPrompt *prompt = kos_find_prompt(ctx, prompt_id);
  if (! prompt) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found");
    return -1;
  }

  /* Validate no Python boundaries in the way */
  if (kos_validate_capture(ctx, prompt) < 0)
    return -1;

  /* Clone the chain segment that will be restored on invoke: the prompt and
   * the frame below it (the body we were running).  That way the resumed
   * context has a prompt to POP_PROMPT and can return to the invoker. */
  KosDynNode *frame_inside = KosDynNode_PARENT(prompt);
  KosDynNode *segment_top = (KosDynNode *) prompt;
  KosDynNode *stop = frame_inside ? frame_inside->parent : NULL;
  if (KOS_XFER_TRACE && frame_inside) {
    if (KosDynNode_IS_FRAME(frame_inside)) {
      KosDynFrame *f = (KosDynFrame *) frame_inside;
      SAYF("[xfer] shift_capture segment_top=%p (prompt) frame_inside=%p kind=%u stack=%p len=%zu",
           (void *) segment_top, (void *) frame_inside,
           (unsigned) KosDynNode_KIND(frame_inside),
           (void *) KosDynFrame_STACK(f),
           KosDynFrame_STACK(f) ? (size_t) KosStackBlock_LEN(KosDynFrame_STACK(f)) : 0);
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

  KosStackBlock *resume_stack = resume_frame
      ? KosStackBlock_COW(KosDynFrame_STACK(resume_frame)) : NULL;
  KosContinuation *k = kos_continuation_create(
      captured_chain,
      resume_frame,
      resume_frame ? KosDynFrame_PC(resume_frame) : 0,
      resume_stack,
      prompt_id,
      1,  /* one-shot */
      KosContext_TOKEN(ctx) ? (PyObject *) KosContext_TOKEN(ctx) : NULL);
  if (! k) {
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  *out_k = k;
  return 0;
}


int kos_capture_cc(KosContext *ctx, KosContinuation **out_k) {

  KosDynCC *cc = kos_find_cc(ctx);
  if (! cc) {
    PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC: no CC block on dynamic chain");
    return -1;
  }

  if (kos_validate_capture_cc(ctx, cc) < 0)
    return -1;

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

  KosContinuation *k = kos_continuation_create(
      captured_chain,
      resume_frame,
      KosDynCC_RESUME_PC(cc),
      KosStackBlock_COW(KosDynFrame_STACK(resume_frame)),
      KOS_PROMPT_ID_UNDELIMITED,
      0,  /* multi-shot */
      KosContext_TOKEN(ctx) ? (PyObject *) KosContext_TOKEN(ctx) : NULL);
  if (! k) {
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  *out_k = k;
  return 0;
}


void kos_continuation_free(KosContinuation *k) {
  kos_continuation_free_exclude(k, NULL);
}


void kos_continuation_free_exclude(KosContinuation *k, PyObject *exclude_from_stack) {
  if (! k)
    return;

  Py_XDECREF(KosContinuation_TOKEN_REF(k));
  KosContinuation_TOKEN_REF(k) = NULL;

  /* Release the stack reference before freeing the captured chain. The
   * frames in the chain may share stack blocks with KosContinuation_RESUME_STACK(k) (COW);
   * freeing the chain would release those blocks and DECREF slot values,
   * which can re-enter the continuation wrapper's dealloc if the wrapper
   * was on the stack at capture time. Releasing with exclude first avoids
   * that. */
  if (KosContinuation_RESUME_STACK(k)) {
    KosStackBlock_ReleaseChainExclude(KosContinuation_RESUME_STACK(k), exclude_from_stack);
    KosContinuation_RESUME_STACK(k) = NULL;
  }

  /* Free the captured chain (frame stacks may still be non-NULL but are
   * either unshared or already released by resume_stack release above). */
  kos_dynchain_free(KosContinuation_CAPTURED_CHAIN(k), NULL);

  if (cont_freecount < CONT_MAX_FREE) {
    memset(k, 0, sizeof(KosContinuation));
    KosContinuation_CAPTURED_CHAIN(k) = (KosDynNode *) cont_freelist;
    cont_freelist = k;
    cont_freecount++;
  } else {
    PyMem_Free(k);
  }
}


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

  KosContinuation *clone = kos_continuation_create(
      cloned_chain,
      resume_frame,
      KosContinuation_RESUME_PC(k),
      KosStackBlock_COW(KosContinuation_RESUME_STACK(k)),
      KosContinuation_PROMPT_ID(k),
      KosContinuation_ONE_SHOT(k),
      KosContinuation_TOKEN_REF(k));
  if (! clone) {
    kos_dynchain_free(cloned_chain, NULL);
    return NULL;
  }

  return clone;
}


/* === LCA computation === */


static size_t kos_chain_depth(KosDynNode *node) {
  size_t depth = 0;
  while (node) {
    depth++;
    node = KosDynNode_PARENT(node);
  }
  return depth;
}


KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b) {
  if (! a || ! b)
    return NULL;

  size_t depth_a = kos_chain_depth(a);
  size_t depth_b = kos_chain_depth(b);

  /* Align to same depth; disjoint chains (e.g. severed template) hit NULL */
  while (depth_a > depth_b) {
    a = a->parent;
    depth_a--;
    if (! a)
      return NULL;
  }
  while (depth_b > depth_a) {
    b = b->parent;
    depth_b--;
    if (! b)
      return NULL;
  }

  /* Walk together until we find LCA.  If the chains are disjoint (e.g. target
   * is a severed continuation template), we eventually hit NULL. */
  while (a != b) {
    if (! a || ! b)
      return NULL;
    a = a->parent;
    b = b->parent;
  }

  return a;
}


/* === Transfer plan building === */


/**
   Allocate a transfer plan with action_count action slots. Plan and actions
   are zeroed. Returns NULL on failure (PyErr_NoMemory set).
*/
static KosTransferPlan *kos_transfer_plan_alloc(size_t action_count) {
  KosTransferPlan *plan = PyMem_Malloc(sizeof(KosTransferPlan));
  if (! plan) {
    PyErr_NoMemory();
    return NULL;
  }
  memset(plan, 0, sizeof(KosTransferPlan));

  KosTransferPlan_ACTIONS(plan) = PyMem_Malloc(sizeof(KosTransferAction) * action_count);
  if (! KosTransferPlan_ACTIONS(plan)) {
    PyMem_Free(plan);
    PyErr_NoMemory();
    return NULL;
  }
  memset(KosTransferPlan_ACTIONS(plan), 0, sizeof(KosTransferAction) * action_count);

  return plan;
}


int kos_build_transfer_plan(
    KosContext *ctx,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
  KosDynNode *lca = kos_compute_lca(current, target);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] build_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  /* Count actions needed */
  size_t action_count = 0;

  /* Count unwind (after) hooks from current to LCA */
  KosDynNode *node = current;
  while (node && node != lca) {
    if (KosDynNode_IS_WIND(node)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (KosDynWind_ENTERED(wind) && KosDynWind_AFTER_CODE(wind)) {
        action_count++;
      }
    }
    node = KosDynNode_PARENT(node);
  }

  /* Add INSTALL action */
  action_count++;

  /* Count wind (before) hooks from LCA to target */
  /* We need to collect these in reverse order, so we'll do two passes */
  size_t wind_count = 0;
  node = target;
  while (node && node != lca) {
    if (KosDynNode_IS_WIND(node)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_CODE(wind)) {
        wind_count++;
      }
    }
    node = KosDynNode_PARENT(node);
  }
  action_count += wind_count;

  /* Add INJECT action */
  action_count++;

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;  /* not set; set by kos_invoke_continuation */
  KosTransferPlan_RESUME_FRAME(plan) = NULL;       /* set by kos_invoke_continuation */
  KosTransferPlan_RESUME_STACK(plan) = NULL;      /* set by kos_invoke_continuation */

  /* Build actions */
  size_t idx = 0;

  /* Unwind hooks (inner to outer: current toward LCA) */
  node = current;
  while (node && node != lca) {
    if (KosDynNode_IS_WIND(node)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (KosDynWind_ENTERED(wind) && KosDynWind_AFTER_CODE(wind)) {
        KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[idx]) = KOS_XFER_RUN_HOOK;
        KosTransferAction_HOOK_CODE(&KosTransferPlan_ACTIONS(plan)[idx]) = KosDynWind_AFTER_CODE(wind);
        KosTransferAction_HOOK_PC(&KosTransferPlan_ACTIONS(plan)[idx]) = KosDynWind_AFTER_PC(wind);
        if (KOS_XFER_TRACE)
          SAYF("[xfer] plan action[%zu] = RUN_HOOK (unwind) node=%p",
               idx, (void *) node);
        idx++;
      }
    }
    node = KosDynNode_PARENT(node);
  }

  /* INSTALL action */
  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[idx]) = KOS_XFER_INSTALL;
  KosTransferAction_INSTALL_SEGMENT(&KosTransferPlan_ACTIONS(plan)[idx]) = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = INSTALL segment=%p",
         idx, (void *) target);
  idx++;

  /* Wind hooks (outer to inner: LCA toward target) */
  /* We need to reverse the order, so collect into temporary array */
  if (wind_count > 0) {
    KosDynWind **wind_nodes = PyMem_Malloc(sizeof(KosDynWind *) * wind_count);
    if (! wind_nodes) {
      PyMem_Free(KosTransferPlan_ACTIONS(plan));
      PyMem_Free(plan);
      PyErr_NoMemory();
      return -1;
    }

    size_t wi = 0;
    node = target;
    while (node && node != lca) {
      if (KosDynNode_IS_WIND(node)) {
        KosDynWind *wind = (KosDynWind *) node;
        if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_CODE(wind)) {
          wind_nodes[wi++] = wind;
        }
      }
      node = KosDynNode_PARENT(node);
    }

    /* Add in reverse order (outer to inner) */
    while (wi--) {
      KosDynWind *wind = wind_nodes[wi];
      KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[idx]) = KOS_XFER_RUN_HOOK;
      KosTransferAction_HOOK_CODE(&KosTransferPlan_ACTIONS(plan)[idx]) = KosDynWind_BEFORE_CODE(wind);
      KosTransferAction_HOOK_PC(&KosTransferPlan_ACTIONS(plan)[idx]) = KosDynWind_BEFORE_PC(wind);
      idx++;
    }

    PyMem_Free(wind_nodes);
  }

  /* INJECT action */
  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[idx]) = KOS_XFER_INJECT;
  KosTransferAction_INJECT_VALUE(&KosTransferPlan_ACTIONS(plan)[idx]) = value;
  KosTransferAction_INJECT_IS_THROW(&KosTransferPlan_ACTIONS(plan)[idx]) = is_throw;
  Py_XINCREF(value);
  idx++;

  *out_plan = plan;
  return 0;
}


void kos_free_transfer_plan(KosTransferPlan *plan) {
  if (! plan)
    return;

  /* DECREF any inject values we still hold */
  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    if (KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[i]) == KOS_XFER_INJECT) {
      Py_XDECREF(KosTransferAction_INJECT_VALUE(&KosTransferPlan_ACTIONS(plan)[i]));
    }
  }
  /* Release the multi-shot clone (if any) without DECREF'ing the continuation
   * so we do not free k before releasing KosTransferPlan_RESUME_STACK(plan). */
    if (KosTransferPlan_INVOKE_CLONE(plan)) {
    KosContinuation *clone = KosTransferPlan_INVOKE_CLONE(plan);
    KosTransferPlan_INVOKE_CLONE(plan) = NULL;
    if (KosContinuation_RESUME_STACK(clone)) {
      KosStackBlock_ReleaseChainExclude(KosContinuation_RESUME_STACK(clone),
                                        KosTransferPlan_CONTINUATION_REF(plan));
      KosContinuation_RESUME_STACK(clone) = NULL;
    }
    Py_XDECREF(KosContinuation_TOKEN_REF(clone));
    if (cont_freecount < CONT_MAX_FREE) {
      memset(clone, 0, sizeof(KosContinuation));
      clone->captured_chain = (KosDynNode *) cont_freelist;
      cont_freelist = clone;
      cont_freecount++;
    } else {
      PyMem_Free(clone);
    }
  }
  /* Release the retained resume_stack reference */
  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock *b = KosTransferPlan_RESUME_STACK(plan);
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }
  Py_XDECREF(KosTransferPlan_CONTINUATION_REF(plan));
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;

  PyMem_Free(KosTransferPlan_ACTIONS(plan));
  PyMem_Free(plan);
}


int kos_run_transfer_step(KosContext *ctx) {
  if (! KosContext_PLAN(ctx))
    return 1;  /* no plan, complete */

  if (KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)) >= KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx))) {
    return 1;  /* plan complete */
  }

  KosTransferAction *action = &KosTransferPlan_ACTIONS(KosContext_PLAN(ctx))[KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx))];
  if (KOS_XFER_TRACE)
    SAYF("[xfer] step %zu: plan=%p actions=%p step_type=%d (INSTALL=%d)",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (void *) KosContext_PLAN(ctx), (void *) KosTransferPlan_ACTIONS(KosContext_PLAN(ctx)),
         (int) KosTransferAction_STEP(action), (int) KOS_XFER_INSTALL);
  KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx))++;

  switch (KosTransferAction_STEP(action)) {
    case KOS_XFER_RUN_HOOK:
      /* Start hook execution - return 0 to indicate more work */
      return kos_begin_hook(ctx, KosTransferAction_HOOK_CODE(action), KosTransferAction_HOOK_PC(action));

    case KOS_XFER_INSTALL: {
      /* Instantiate from template: clone the segment so we evaluate a new
       * copy (no mutated stack or broken back-links). */
      KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
      KosDynNode *template_bottom = template_seg;
      while (template_bottom && KosDynNode_PARENT(template_bottom))
        template_bottom = KosDynNode_PARENT(template_bottom);
      KosDynNode *stop = template_bottom ? KosDynNode_PARENT(template_bottom) : NULL;
      KosDynNode *seg = kos_clone_chain_segment(template_seg, stop);
      if (! seg && template_seg) {
        if (KOS_XFER_TRACE)
          SAY("[xfer] INSTALL clone segment failed");
        KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx))--;  /* leave step for retry or caller */
        return -1;
      }
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INSTALL segment=%p (fresh) resume_pc=%u (before: dyn_top=%p frame_top=%p)",
             (void *) seg, (unsigned) KosTransferPlan_RESUME_PC(KosContext_PLAN(ctx)),
             (void *) KosContext_DYN_TOP(ctx), (void *) KosContext_FRAME_TOP(ctx));
      {
        KosDynNode *old_top = KosContext_DYN_TOP(ctx);
        KosContext_DYN_TOP(ctx) = seg;
        kos_update_frame_top(ctx);
        if (old_top)
          kos_dynchain_free(old_top, NULL);
      }
      /* New segment is the whole chain; topmost frame is the globals frame. */
      KosContext_GLOBALS_FRAME(ctx) = KosContext_FRAME_TOP(ctx);
      KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx) + 1;
      /* Restore stack from plan (fresh copy from template). */
      if (KosContext_FRAME_TOP(ctx) && KosTransferPlan_RESUME_STACK(KosContext_PLAN(ctx))) {
        KosStackBlock *new_stack = KosStackBlock_COW(KosTransferPlan_RESUME_STACK(KosContext_PLAN(ctx)));
        KosStackBlock *old_stack = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx));
        KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)) = new_stack;
        if (old_stack && old_stack != new_stack) {
          /* Do not DECREF the continuation wrapper (KosTransferPlan_CONTINUATION_REF(plan));
           * it may be on the old stack from CALL and would free k and the
           * captured chain while the plan still references them. */
          KosStackBlock_ReleaseChainExclude(old_stack,
                                            KosTransferPlan_CONTINUATION_REF(KosContext_PLAN(ctx)));
        }
      }
      if (KosTransferPlan_RESUME_PC(KosContext_PLAN(ctx)) != (uint32_t) -1 && KosContext_FRAME_TOP(ctx)) {
        KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosTransferPlan_RESUME_PC(KosContext_PLAN(ctx));
      }
      KosContext_CHAIN_VERSION(ctx)++;
      KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx);
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INSTALL after: dyn_top=%p frame_top=%p",
             (void *) KosContext_DYN_TOP(ctx), (void *) KosContext_FRAME_TOP(ctx));
      break;
    }

    case KOS_XFER_INJECT:
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INJECT frame_top=%p stack=%p is_throw=%d",
             (void *) KosContext_FRAME_TOP(ctx),
             KosContext_FRAME_TOP(ctx) ? (void *) KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)) : NULL,
             KosTransferAction_INJECT_IS_THROW(action));
      if (KosTransferAction_INJECT_IS_THROW(action)) {
        KosContext_INJECT_MODE(ctx) = KOS_INJECT_THROW;
        Py_XINCREF(KosTransferAction_INJECT_VALUE(action));
        Py_XDECREF(KosContext_INJECT_VALUE(ctx));
        KosContext_INJECT_VALUE(ctx) = KosTransferAction_INJECT_VALUE(action);
      } else {
        /* Push value onto frame's stack (PushRef allocates block if NULL) */
        if (KosContext_FRAME_TOP(ctx) &&
            KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                                  KosTransferAction_INJECT_VALUE(action)) < 0) {
          if (KOS_XFER_TRACE)
            SAY("[xfer] INJECT PushRef failed");
          /* PushRef already set exception */
          Py_XDECREF(KosTransferAction_INJECT_VALUE(action));
          KosTransferAction_INJECT_VALUE(action) = NULL;
          return -1;
        }
      }
      Py_XDECREF(KosTransferAction_INJECT_VALUE(action));
      KosTransferAction_INJECT_VALUE(action) = NULL;
      break;
  }

  /* Check if plan is complete */
  if (KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)) >= KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)))
    return 1;

  return 0;  /* more steps remain */
}


void kos_suspend_transfer_plan(KosContext *ctx) {
  if (! KosContext_PLAN(ctx))
    return;

  KosTransferPlan_SUSPENDED(KosContext_PLAN(ctx)) = KosContext_PLAN_STACK(ctx);
  KosContext_PLAN_STACK(ctx) = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;
}


int kos_resume_transfer_plan(KosContext *ctx) {
  if (! KosContext_PLAN_STACK(ctx))
    return 0;  /* nothing to resume */

  KosContext_PLAN(ctx) = KosContext_PLAN_STACK(ctx);
  KosContext_PLAN_STACK(ctx) = KosContext_PLAN(ctx)->suspended;
  KosContext_PLAN(ctx)->suspended = NULL;

  /* Check if chain has changed */
  if (KosContext_CHAIN_VERSION(ctx) != KosContext_PLAN(ctx)->chain_version) {
    /* Chain changed during nested transfer - would need to replan */
    /* For now, just continue with existing plan */
    /* TODO: implement replanning */
  }

  return 0;
}


/* === Hook execution === */


int kos_begin_hook(KosContext *ctx, KosCode *code, uint32_t pc) {
  if (KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "hook already active");
    return -1;
  }

  KosHookRunner_CODE(KosContext_HOOK_RUNNER(ctx)) = code;
  KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx)) = pc;
  KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx)) = NULL;  /* TODO: allocate scratch stack */
  KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx)) = 1;

  KosContext_FLAGS(ctx) |= KOS_CTX_IN_HOOK;

  return 0;
}


int kos_hook_active(KosContext *ctx) {
  return KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx));
}


int kos_end_hook(KosContext *ctx) {
  if (! KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "no hook active");
    return -1;
  }

  /* Clean up scratch stack */
  if (KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx))) {
    KosStackBlock *b = KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx));
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  KosHookRunner_CODE(KosContext_HOOK_RUNNER(ctx)) = NULL;
  KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx)) = 0;
  KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx)) = NULL;
  KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx)) = 0;

  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_HOOK;

  return 0;
}


/* === Continuation invocation === */


int kos_invoke_continuation(
    KosContext *ctx,
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
  if (KOS_XFER_TRACE)
    SAYF("[xfer] kos_invoke_continuation k=%p one_shot=%d captured_chain=%p",
         (void *) k, KosContinuation_ONE_SHOT(k), (void *) KosContinuation_CAPTURED_CHAIN(k));
  if (! KosContinuation_ONE_SHOT(k)) {
    to_use = kos_continuation_clone(k);
    if (KOS_XFER_TRACE)
      SAYF("[xfer] clone -> to_use=%p", (void *) to_use);
    if (! to_use) {
      if (KOS_XFER_TRACE)
        SAY("[xfer] clone failed");
      return -1;
    }
  }

  /* Build plan from template; INSTALL will clone the segment so we evaluate
   * a new copy each time (no mutated stack or broken back-links). */
  KosTransferPlan *plan = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] kos_build_transfer_plan current=%p target=%p (template)",
         (void *) KosContext_DYN_TOP(ctx), (void *) to_use->captured_chain);
  int res = kos_build_transfer_plan(ctx, to_use->captured_chain,
                                    value, is_throw, &plan);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] kos_build_transfer_plan -> res=%d plan=%p",
         res, (void *) plan);
  if (res < 0) {
    if (to_use != k)
      kos_continuation_free(to_use);
    return -1;
  }

  KosTransferPlan_RESUME_PC(plan) = KosContinuation_RESUME_PC(k);
  KosTransferPlan_RESUME_FRAME(plan) = to_use->resume_frame;  /* template; INSTALL uses fresh */
  /* COW ref to template stack for this invoke; released in kos_free_transfer_plan. */
  KosTransferPlan_RESUME_STACK(plan) = to_use->resume_stack
      ? KosStackBlock_COW(to_use->resume_stack) : NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan resume_frame=%p resume_stack=%p",
         (void *) KosTransferPlan_RESUME_FRAME(plan), (void *) KosTransferPlan_RESUME_STACK(plan));

  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  if (continuation_ref) {
    Py_INCREF(continuation_ref);
    KosTransferPlan_CONTINUATION_REF(plan) = continuation_ref;
  }

  if (to_use != k) {
    to_use->captured_chain = NULL;
    KosTransferPlan_INVOKE_CLONE(plan) = to_use;
    /* Do not release to_use->resume_stack here; that would DECREF the
     * continuation and free k before the plan runs. kos_free_transfer_plan
     * will release it with exclude=continuation_ref then free to_use. */
  }

  return 0;
}


/* === GC traversal === */


int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg) {
  if (! node)
    return 0;

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FRAME: {
      KosDynFrame *frame = (KosDynFrame *) node;
      Py_VISIT(KosDynFrame_NS(frame));
      /* Traverse stack */
      KosStackBlock *b = KosDynFrame_STACK(frame);
      while (b) {
        for (size_t i = b->len; i--; ) {
          Py_VISIT(b->objs[i]);
        }
        b = KosStackBlock_PARENT(b);
      }
      break;
    }
    case KOS_DYN_NS: {
      KosDynNs *ns_node = (KosDynNs *) node;
      Py_VISIT(KosDynNs_SAVED_NS(ns_node));
      break;
    }
    case KOS_DYN_PROMPT:
    case KOS_DYN_CC:
    case KOS_DYN_WIND:
    case KOS_DYN_PYCALL:
      /* No PyObjects to visit */
      break;
    case KOS_DYN_REGION: {
      KosDynRegion *region = (KosDynRegion *) node;
      Py_VISIT(KosDynRegion_RESULT(region));
      Py_VISIT(KosDynRegion_ITER(region));
      break;
    }
    case KOS_DYN_YIELD_FROM: {
      KosDynYieldFrom *yf = (KosDynYieldFrom *) node;
      Py_VISIT(KosDynYieldFrom_DELEGATE(yf));
      Py_VISIT(KosDynYieldFrom_PENDING_ARG(yf));
      Py_VISIT(KosDynYieldFrom_INFLIGHT(yf));
      break;
    }
  }

  return 0;
}


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


int kos_continuation_traverse(
    KosContinuation *k,
    visitproc visit,
    void *arg) {

  if (! k)
    return 0;

  Py_VISIT(KosContinuation_TOKEN_REF(k));
  return kos_dynchain_traverse(KosContinuation_CAPTURED_CHAIN(k), NULL, visit, arg);
}


int kos_transferplan_traverse(
    KosTransferPlan *plan,
    visitproc visit,
    void *arg) {

  if (! plan)
    return 0;

  Py_VISIT(KosTransferPlan_CONTINUATION_REF(plan));

  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    if (KosTransferPlan_ACTIONS(plan)[i].step == KOS_XFER_INJECT) {
      Py_VISIT(KosTransferPlan_ACTIONS(plan)[i].data.inject.value);
    }
  }

  return 0;
}


int kos_hookrunner_traverse(
    KosHookRunner *runner,
    visitproc visit,
    void *arg) {

  if (! runner || ! runner->active)
    return 0;

  /* Traverse scratch stack */
  KosStackBlock *b = runner->scratch;
  while (b) {
    for (size_t i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = KosStackBlock_PARENT(b);
  }

  return 0;
}


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod) {
  /* No Python types to register for dynchain */
  return 0;
}


int koskari_dynchain_clear(PyObject *mod) {
  /* Nothing to clean up */
  return 0;
}


// The end.
