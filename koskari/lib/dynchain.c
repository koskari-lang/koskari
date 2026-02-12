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


/* Set to 1 to trace transfer plan steps (for debugging).  All trace output
 * goes through SAYF from debug.h. */
#define KOS_XFER_TRACE 0


/* === DynNode base operations === */


static void kos_dynnode_init(
    KosDynNode *node,
    KosDynKind kind,
    KosDynNode *parent) {

  node->parent = parent;
  node->kind = kind;
  node->flags = KOS_DYNF_NONE;
}


/* === KosDynFrame implementation === */


KosDynFrame *kos_dynframe_new(
    KosDynNode *parent,
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack) {

  KosDynFrame *frame = PyMem_Malloc(sizeof(KosDynFrame));
  if (! frame) {
    PyErr_NoMemory();
    Py_XDECREF(ns);
    return NULL;
  }

  kos_dynnode_init(&frame->base, KOS_DYN_FRAME, parent);

  frame->code = code;  /* borrowed */
  frame->pc = 0;
  frame->stack = stack;  /* ownership transferred */
  frame->ns = ns;  /* ownership transferred (already stole ref) */
  frame->frame_flags = 0;

  return frame;
}


KosDynFrame *kos_dynframe_clone(KosDynFrame *frame) {
  if (! frame)
    return NULL;

  /* Use COW for stack - just increment refcount on all blocks */
  KosStackBlock *cloned_stack = KosStackBlock_COW(frame->stack);

  KosDynFrame *clone = PyMem_Malloc(sizeof(KosDynFrame));
  if (! clone) {
    PyErr_NoMemory();
    /* Release the COW'd stack */
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

  kos_dynnode_init(&clone->base, KOS_DYN_FRAME, NULL);
  clone->base.flags = frame->base.flags | KOS_DYNF_CLONED;

  clone->code = frame->code;  /* borrowed */
  clone->pc = frame->pc;
  clone->stack = cloned_stack;
  clone->ns = frame->ns;
  Py_XINCREF(clone->ns);
  clone->frame_flags = frame->frame_flags;

  return clone;
}


void kos_dynframe_free(KosDynFrame *frame) {
  if (! frame)
    return;

  /* Release the stack block chain */
  KosStackBlock *block = frame->stack;
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  Py_XDECREF(frame->ns);
  /* code is borrowed, don't decref */

  PyMem_Free(frame);
}


/* === KosDynPrompt implementation === */


KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id) {

  KosDynPrompt *prompt = PyMem_Malloc(sizeof(KosDynPrompt));
  if (! prompt) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&prompt->base, KOS_DYN_PROMPT, parent);

  prompt->prompt_id = prompt_id;
  prompt->handler_code = NULL;
  prompt->handler_pc = 0;

  return prompt;
}


static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = PyMem_Malloc(sizeof(KosDynPrompt));
  if (! clone) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&clone->base, KOS_DYN_PROMPT, NULL);
  clone->base.flags = prompt->base.flags | KOS_DYNF_CLONED;

  clone->prompt_id = prompt->prompt_id;
  clone->handler_code = prompt->handler_code;  /* borrowed */
  clone->handler_pc = prompt->handler_pc;

  return clone;
}


void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  /* handler_code is borrowed */
  PyMem_Free(prompt);
}


/* === KosDynCC implementation === */


KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = PyMem_Malloc(sizeof(KosDynCC));
  if (! cc) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&cc->base, KOS_DYN_CC, parent);
  cc->resume_pc = resume_pc;

  return cc;
}


static KosDynCC *kos_dyncc_clone(KosDynCC *cc) {
  if (! cc)
    return NULL;

  KosDynCC *clone = PyMem_Malloc(sizeof(KosDynCC));
  if (! clone) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&clone->base, KOS_DYN_CC, NULL);
  clone->base.flags = cc->base.flags | KOS_DYNF_CLONED;
  clone->resume_pc = cc->resume_pc;

  return clone;
}


void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  PyMem_Free(cc);
}


/* === KosDynWind implementation === */


KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    KosCode *before_code,
    uint32_t before_pc,
    KosCode *after_code,
    uint32_t after_pc) {

  KosDynWind *wind = PyMem_Malloc(sizeof(KosDynWind));
  if (! wind) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&wind->base, KOS_DYN_WIND, parent);

  wind->before_code = before_code;  /* borrowed */
  wind->before_pc = before_pc;
  wind->after_code = after_code;  /* borrowed */
  wind->after_pc = after_pc;
  wind->entered = 0;

  return wind;
}


KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  KosDynWind *clone = PyMem_Malloc(sizeof(KosDynWind));
  if (! clone) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&clone->base, KOS_DYN_WIND, NULL);
  clone->base.flags = wind->base.flags | KOS_DYNF_CLONED;

  clone->before_code = wind->before_code;  /* borrowed */
  clone->before_pc = wind->before_pc;
  clone->after_code = wind->after_code;  /* borrowed */
  clone->after_pc = wind->after_pc;
  clone->entered = wind->entered;

  return clone;
}


void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  /* code pointers are borrowed */
  PyMem_Free(wind);
}


/* === KosDynRegion implementation === */


KosDynRegion *kos_dynregion_new(
        KosDynNode *parent,
        KosRegionStyle style,
        uint32_t break_pc,
        uint32_t continue_pc) {

  KosDynRegion *region = PyMem_Malloc(sizeof(KosDynRegion));
  if (! region) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&region->base, KOS_DYN_REGION, parent);

  region->style = style;
  region->break_pc = break_pc;
  region->continue_pc = continue_pc;
  region->enabled = 1;

  /* Payload fields initialized to defaults; caller sets for LOOP/FOREACH */
  region->result = NULL;
  region->iter = NULL;
  region->exhausted = 0;

  return region;
}


KosDynRegion *kos_dynregion_clone(KosDynRegion *region) {
  if (! region)
    return NULL;

  KosDynRegion *clone = PyMem_Malloc(sizeof(KosDynRegion));
  if (! clone) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(&clone->base, KOS_DYN_REGION, NULL);
  clone->base.flags = region->base.flags | KOS_DYNF_CLONED;

  clone->style = region->style;
  clone->break_pc = region->break_pc;
  clone->continue_pc = region->continue_pc;
  clone->enabled = region->enabled;

  /* Clone payload with INCREF for owned references */
  clone->result = region->result;
  Py_XINCREF(clone->result);
  clone->iter = region->iter;
  Py_XINCREF(clone->iter);
  clone->exhausted = region->exhausted;

  return clone;
}


void kos_dynregion_free(KosDynRegion *region) {
  if (! region)
    return;

  /* DECREF payload owned references */
  Py_XDECREF(region->result);
  Py_XDECREF(region->iter);

  PyMem_Free(region);
}


/* === KosDynNs (namespace scope) implementation === */


KosDynNs *kos_dynns_new(KosDynNode *parent, PyObject *saved_ns) {

  KosDynNs *ns_node = PyMem_Malloc(sizeof(KosDynNs));
  if (! ns_node) {
    PyErr_NoMemory();
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init(&ns_node->base, KOS_DYN_NS, parent);
  ns_node->saved_ns = saved_ns;  /* caller transferred ownership */

  return ns_node;
}


KosDynNs *kos_dynns_clone(KosDynNs *ns_node) {
  if (! ns_node)
    return NULL;

  PyObject *saved = ns_node->saved_ns;
  Py_INCREF(saved);
  return kos_dynns_new(NULL, saved);
}


void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  Py_DECREF(ns_node->saved_ns);
  PyMem_Free(ns_node);
}


/* === KosDynPycall (boundary marker) implementation === */


KosDynNode *kos_dynpycall_new(KosDynNode *parent) {

  KosDynNode *node = PyMem_Malloc(sizeof(KosDynNode));
  if (! node) {
    PyErr_NoMemory();
    return NULL;
  }

  kos_dynnode_init(node, KOS_DYN_PYCALL, parent);
  node->flags |= KOS_DYNF_BOUNDARY;

  return node;
}


void kos_dynpycall_free(KosDynNode *node) {
  if (! node)
    return;

  PyMem_Free(node);
}


/* === Generic DynNode operations === */


void kos_dynnode_free(KosDynNode *node) {
  if (! node)
    return;

  switch (node->kind) {
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
  }
}


KosDynNode *kos_dynnode_clone(KosDynNode *node) {
  if (! node)
    return NULL;

  switch (node->kind) {
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
  }

  PyErr_Format(PyExc_RuntimeError, "cannot clone dynnode kind %u",
               (unsigned) node->kind);
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
           (void *) node, (unsigned) node->kind);
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
    node = node->parent;
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
  KosDynNode *node = ctx->dyn_top;
  while (node) {
    if (KosDynNode_IS_FRAME(node)) {
      ctx->frame_top = (KosDynFrame *) node;
      return;
    }
    node = node->parent;
  }
  ctx->frame_top = NULL;
}


KosDynFrame *kos_dyn_push_frame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  KosDynFrame *frame = kos_dynframe_new(ctx->dyn_top, code, ns, NULL);
  if (! frame)
    return NULL;

  ctx->dyn_top = (KosDynNode *) frame;
  ctx->frame_top = frame;
  ctx->chain_version++;

  return frame;
}


int kos_dyn_pop_frame(KosContext *ctx) {
  if (! ctx->dyn_top) {
    PyErr_SetString(PyExc_RuntimeError, "no node to pop");
    return -1;
  }

  if (! KosDynNode_IS_FRAME(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }

  KosDynNode *old_top = ctx->dyn_top;
  ctx->dyn_top = old_top->parent;
  ctx->chain_version++;

  kos_dynnode_free(old_top);
  kos_update_frame_top(ctx);

  return 0;
}


/**
   Attach an existing frame to the context's dynamic chain (no allocation).

   Reparents the frame so its parent is ctx->dyn_top, then sets it as
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
  frame->base.parent = ctx->dyn_top;
  ctx->dyn_top = (KosDynNode *) frame;
  ctx->frame_top = frame;
  ctx->chain_version++;
  return 0;
}


/**
   Detach the top frame from the context's dynamic chain without freeing it.

   Sets ctx->dyn_top to the frame's parent and updates frame_top. The frame
   is returned in *frame_out; caller may set frame->base.parent = NULL and
   keep the frame (e.g. for async gen storage in agen->ctx).

   :param ctx: execution context
   :param frame_out: output; set to the detached frame (may be NULL)

   :return: 0 on success, -1 if top is not a frame
*/
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out) {
  if (! ctx->dyn_top) {
    PyErr_SetString(PyExc_RuntimeError, "no node to detach");
    return -1;
  }
  if (! KosDynNode_IS_FRAME(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }
  KosDynFrame *frame = (KosDynFrame *) ctx->dyn_top;
  ctx->dyn_top = frame->base.parent;
  ctx->chain_version++;
  kos_update_frame_top(ctx);
  if (frame_out)
    *frame_out = frame;
  return 0;
}


KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynprompt_new(ctx->dyn_top, prompt_id);
  if (! prompt)
    return NULL;

  ctx->dyn_top = (KosDynNode *) prompt;
  ctx->chain_version++;

  return prompt;
}


void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt) {
  if (! ctx->dyn_top || ctx->dyn_top != (KosDynNode *) prompt) {
    /* Prompt is not at top - this is an error but we handle gracefully */
    return;
  }

  ctx->dyn_top = prompt->base.parent;
  ctx->chain_version++;
  kos_dynprompt_free(prompt);
}


KosDynCC *kos_dyn_push_cc(KosContext *ctx, uint32_t resume_pc) {

  KosDynCC *cc = kos_dyncc_new(ctx->dyn_top, resume_pc);
  if (! cc)
    return NULL;

  ctx->dyn_top = (KosDynNode *) cc;
  ctx->chain_version++;

  return cc;
}


void kos_dyn_pop_cc(KosContext *ctx, KosDynCC *cc) {
  if (! ctx->dyn_top || ctx->dyn_top != (KosDynNode *) cc) {
    return;
  }

  ctx->dyn_top = cc->base.parent;
  ctx->chain_version++;
  kos_dyncc_free(cc);
}


KosDynWind *kos_dyn_push_wind(
    KosContext *ctx,
    KosCode *before_code,
    uint32_t before_pc,
    KosCode *after_code,
    uint32_t after_pc) {

  KosDynWind *wind = kos_dynwind_new(ctx->dyn_top,
                                     before_code, before_pc,
                                     after_code, after_pc);
  if (! wind)
    return NULL;

  ctx->dyn_top = (KosDynNode *) wind;
  ctx->chain_version++;

  return wind;
}


int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind) {
  if (! ctx->dyn_top || ctx->dyn_top != (KosDynNode *) wind) {
    PyErr_SetString(PyExc_RuntimeError, "wind node is not at top of chain");
    return -1;
  }

  ctx->dyn_top = wind->base.parent;
  ctx->chain_version++;
  kos_dynwind_free(wind);

  return 0;
}


KosDynRegion *kos_dyn_push_region(
        KosContext *ctx,
        KosRegionStyle style,
        uint32_t break_pc,
        uint32_t continue_pc) {

  KosDynRegion *region = kos_dynregion_new(ctx->dyn_top,
                                           style, break_pc, continue_pc);
  if (! region)
    return NULL;

  ctx->dyn_top = (KosDynNode *) region;
  ctx->chain_version++;

  return region;
}


void kos_dyn_pop_region(KosContext *ctx, KosDynRegion *region) {
  if (! ctx->dyn_top || ctx->dyn_top != (KosDynNode *) region) {
    return;
  }

  ctx->dyn_top = region->base.parent;
  ctx->chain_version++;
  kos_dynregion_free(region);
}


KosDynNs *kos_dyn_push_ns(KosContext *ctx, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynns_new(ctx->dyn_top, saved_ns);
  if (! ns_node)
    return NULL;

  ctx->dyn_top = (KosDynNode *) ns_node;
  ctx->chain_version++;

  return ns_node;
}


void kos_dyn_pop_ns(KosContext *ctx, KosDynNs *node) {
  if (! ctx->dyn_top || ctx->dyn_top != (KosDynNode *) node) {
    return;
  }

  ctx->dyn_top = node->base.parent;
  ctx->chain_version++;
  kos_dynns_free(node);
}


/**
   Pop the top node of the dynamic chain. Dispatches on node kind.
   For NS, restores frame_top->ns from the node's saved_ns before popping.

   :return: 0 on success, -1 on error or if top is PROMPT/PYCALL (cannot unwind)
*/
int kos_dyn_pop_top(KosContext *ctx) {
  KosDynNode *top = ctx->dyn_top;
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return -1;
  }

  switch (top->kind) {
  case KOS_DYN_FRAME:
    return kos_dyn_pop_frame(ctx);

  case KOS_DYN_WIND:
    return kos_dyn_pop_wind(ctx, (KosDynWind *) top);

  case KOS_DYN_REGION:
    kos_dyn_pop_region(ctx, (KosDynRegion *) top);
    return 0;

  case KOS_DYN_NS: {
    KosDynNs *ns_node = (KosDynNs *) top;
    if (ctx->frame_top) {
      PyObject *prior = ns_node->saved_ns;
      Py_INCREF(prior);
      Py_DECREF(ctx->frame_top->ns);
      ctx->frame_top->ns = prior;
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
  }

  PyErr_SetString(PyExc_RuntimeError, "unknown dynamic chain node kind");
  return -1;
}


KosDynRegion *kos_find_region_for_raise(KosContext *ctx) {
  KosDynNode *node = ctx->dyn_top;

  while (node) {
    if (KosDynNode_IS_REGION(node)) {
      KosDynRegion *region = (KosDynRegion *) node;
      if (region->enabled &&
          (region->style == KOS_REGION_TRY ||
           region->style == KOS_REGION_FINALLY)) {
        return region;
      }
    }
    node = node->parent;
  }

  return NULL;
}


KosDynRegion *kos_find_region_for_loop(KosContext *ctx) {
  KosDynNode *node = ctx->dyn_top;

  while (node) {
    if (KosDynNode_IS_REGION(node)) {
      KosDynRegion *region = (KosDynRegion *) node;
      if (region->style == KOS_REGION_LOOP ||
          region->style == KOS_REGION_FOREACH) {
        return region;
      }
    }
    node = node->parent;
  }

  return NULL;
}


KosDynNode *kos_dyn_push_pycall(KosContext *ctx) {

  KosDynNode *marker = kos_dynpycall_new(ctx->dyn_top);
  if (! marker)
    return NULL;

  ctx->dyn_top = marker;
  ctx->chain_version++;

  return marker;
}


void kos_dyn_pop_pycall(KosContext *ctx, KosDynNode *marker) {
  if (! ctx->dyn_top || ctx->dyn_top != marker) {
    return;
  }

  ctx->dyn_top = marker->parent;
  ctx->chain_version++;
  kos_dynpycall_free(marker);
}


/* === Prompt lookup and validation === */


KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id) {
  KosDynNode *node = ctx->dyn_top;

  while (node) {
    if (KosDynNode_IS_PROMPT(node)) {
      KosDynPrompt *prompt = (KosDynPrompt *) node;
      if (prompt->prompt_id == prompt_id) {
        return prompt;
      }
    }
    node = node->parent;
  }

  return NULL;
}


int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt) {
  KosDynNode *node = ctx->dyn_top;

  while (node && node != (KosDynNode *) prompt) {
    if (KosDynNode_IS_PYCALL(node)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot capture continuation across Python boundary");
      return -1;
    }
    node = node->parent;
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "prompt not found in chain");
    return -1;
  }

  return 0;
}


KosDynCC *kos_find_cc(KosContext *ctx) {
  KosDynNode *node = ctx->dyn_top;

  while (node) {
    if (KosDynNode_IS_CC(node))
      return (KosDynCC *) node;
    node = node->parent;
  }

  return NULL;
}


static int kos_validate_capture_cc(KosContext *ctx, KosDynCC *cc) {
  KosDynNode *node = ctx->dyn_top;

  while (node && node != (KosDynNode *) cc) {
    if (KosDynNode_IS_PYCALL(node)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot capture continuation across Python boundary");
      return -1;
    }
    node = node->parent;
  }

  if (! node) {
    PyErr_SetString(PyExc_RuntimeError, "CC block not found in chain");
    return -1;
  }

  return 0;
}


/* === Continuation capture and free === */


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
  KosDynNode *frame_inside = (KosDynNode *) prompt->base.parent;
  KosDynNode *segment_top = (KosDynNode *) prompt;
  KosDynNode *stop = frame_inside ? frame_inside->parent : NULL;
  if (KOS_XFER_TRACE && frame_inside) {
    if (KosDynNode_IS_FRAME(frame_inside)) {
      KosDynFrame *f = (KosDynFrame *) frame_inside;
      SAYF("[xfer] shift_capture segment_top=%p (prompt) frame_inside=%p kind=%u stack=%p len=%zu",
           (void *) segment_top, (void *) frame_inside,
           (unsigned) KosDynNode_KIND(frame_inside),
           (void *) f->stack,
           f->stack ? (size_t) KosStackBlock_LEN(f->stack) : 0);
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
    node = node->parent;
  }

  /* Allocate continuation struct */
  KosContinuation *k = PyMem_Malloc(sizeof(KosContinuation));
  if (! k) {
    PyErr_NoMemory();
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  k->captured_chain = captured_chain;
  k->resume_frame = resume_frame;
  k->resume_pc = resume_frame ? resume_frame->pc : 0;
  /* Store stack as a template (COW).  At invoke the plan takes a COW ref;
   * INSTALL gives the frame a COW ref; the first push/pop copies on write so
   * each application sees an unmutated logical state. */
  k->resume_stack = resume_frame
      ? KosStackBlock_COW(resume_frame->stack) : NULL;
  k->prompt_id = prompt_id;
  k->one_shot = 1;  /* one-shot for now; multi-shot clone needs segment-only clone */
  k->token_ref = ctx->token ? (PyObject *) ctx->token : NULL;
  if (k->token_ref)
    Py_INCREF(k->token_ref);

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
  KosDynNode *segment_top = cc->base.parent;
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
    node = node->parent;
  }

  if (! resume_frame) {
    kos_dynchain_free(captured_chain, NULL);
    PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC: no frame in captured segment");
    return -1;
  }

  KosContinuation *k = PyMem_Malloc(sizeof(KosContinuation));
  if (! k) {
    PyErr_NoMemory();
    kos_dynchain_free(captured_chain, NULL);
    return -1;
  }

  k->captured_chain = captured_chain;
  k->resume_frame = resume_frame;
  k->resume_pc = cc->resume_pc;
  k->resume_stack = KosStackBlock_COW(resume_frame->stack);
  k->prompt_id = KOS_PROMPT_ID_UNDELIMITED;
  k->one_shot = 0;  /* multi-shot by default */
  k->token_ref = ctx->token ? (PyObject *) ctx->token : NULL;
  if (k->token_ref)
    Py_INCREF(k->token_ref);

  *out_k = k;
  return 0;
}


void kos_continuation_free(KosContinuation *k) {
  kos_continuation_free_exclude(k, NULL);
}


void kos_continuation_free_exclude(KosContinuation *k, PyObject *exclude_from_stack) {
  if (! k)
    return;

  Py_XDECREF(k->token_ref);
  k->token_ref = NULL;

  /* Free the captured chain */
  kos_dynchain_free(k->captured_chain, NULL);

  /* Release the stack reference. When called from the Python wrapper's
   * dealloc, exclude_from_stack is the wrapper so we do not DECREF it
   * (it is currently being deallocated). */
  if (k->resume_stack) {
    KosStackBlock_ReleaseChainExclude(k->resume_stack, exclude_from_stack);
    k->resume_stack = NULL;
  }

  PyMem_Free(k);
}


KosContinuation *kos_continuation_clone(KosContinuation *k) {
  if (! k)
    return NULL;

  /* Clone only the continuation's segment (do not clone into context below
   * the prompt). Find the bottom of the captured chain and use its parent
   * as stop so we don't clone context nodes. */
  KosDynNode *bottom = k->captured_chain;
  while (bottom && bottom->parent)
    bottom = bottom->parent;
  KosDynNode *stop = bottom ? bottom->parent : NULL;

  KosDynNode *cloned_chain = kos_clone_chain_segment(k->captured_chain, stop);
  if (! cloned_chain && k->captured_chain) {
    return NULL;
  }

  /* Find the resume frame in the cloned chain */
  KosDynFrame *resume_frame = NULL;
  KosDynNode *node = cloned_chain;
  while (node) {
    if (KosDynNode_IS_FRAME(node)) {
      resume_frame = (KosDynFrame *) node;
      break;
    }
    node = node->parent;
  }

  /* Allocate new continuation */
  KosContinuation *clone = PyMem_Malloc(sizeof(KosContinuation));
  if (! clone) {
    PyErr_NoMemory();
    kos_dynchain_free(cloned_chain, NULL);
    return NULL;
  }

  clone->captured_chain = cloned_chain;
  clone->resume_frame = resume_frame;
  clone->resume_pc = k->resume_pc;
  clone->resume_stack = KosStackBlock_COW(k->resume_stack);
  clone->prompt_id = k->prompt_id;
  clone->one_shot = k->one_shot;
  clone->token_ref = k->token_ref;
  if (clone->token_ref)
    Py_INCREF(clone->token_ref);

  return clone;
}


/* === LCA computation === */


static size_t kos_chain_depth(KosDynNode *node) {
  size_t depth = 0;
  while (node) {
    depth++;
    node = node->parent;
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


int kos_build_transfer_plan(
    KosContext *ctx,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosDynNode *current = ctx->dyn_top;
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
      if (wind->entered && wind->after_code) {
        action_count++;
      }
    }
    node = node->parent;
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
      if (! wind->entered && wind->before_code) {
        wind_count++;
      }
    }
    node = node->parent;
  }
  action_count += wind_count;

  /* Add INJECT action */
  action_count++;

  /* Allocate plan and actions; zero so every action.step is 0 until set.
   * This avoids garbage or padding making step read as RUN_HOOK. */
  KosTransferPlan *plan = PyMem_Malloc(sizeof(KosTransferPlan));
  if (! plan) {
    PyErr_NoMemory();
    return -1;
  }
  memset(plan, 0, sizeof(KosTransferPlan));

  plan->actions = PyMem_Malloc(sizeof(KosTransferAction) * action_count);
  if (! plan->actions) {
    PyMem_Free(plan);
    PyErr_NoMemory();
    return -1;
  }
  memset(plan->actions, 0, sizeof(KosTransferAction) * action_count);

  plan->action_count = action_count;
  plan->action_index = 0;
  plan->original_target = target;
  plan->chain_version = ctx->chain_version;
  plan->resume_pc = (uint32_t) -1;  /* not set; set by kos_invoke_continuation */
  plan->resume_frame = NULL;       /* set by kos_invoke_continuation */
  plan->resume_stack = NULL;      /* set by kos_invoke_continuation */

  /* Build actions */
  size_t idx = 0;

  /* Unwind hooks (inner to outer: current toward LCA) */
  node = current;
  while (node && node != lca) {
    if (KosDynNode_IS_WIND(node)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (wind->entered && wind->after_code) {
        plan->actions[idx].step = KOS_XFER_RUN_HOOK;
        plan->actions[idx].data.hook.code = wind->after_code;
        plan->actions[idx].data.hook.pc = wind->after_pc;
        if (KOS_XFER_TRACE)
          SAYF("[xfer] plan action[%zu] = RUN_HOOK (unwind) node=%p",
               idx, (void *) node);
        idx++;
      }
    }
    node = node->parent;
  }

  /* INSTALL action */
  plan->actions[idx].step = KOS_XFER_INSTALL;
  plan->actions[idx].data.install.segment = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = INSTALL segment=%p",
         idx, (void *) target);
  idx++;

  /* Wind hooks (outer to inner: LCA toward target) */
  /* We need to reverse the order, so collect into temporary array */
  if (wind_count > 0) {
    KosDynWind **wind_nodes = PyMem_Malloc(sizeof(KosDynWind *) * wind_count);
    if (! wind_nodes) {
      PyMem_Free(plan->actions);
      PyMem_Free(plan);
      PyErr_NoMemory();
      return -1;
    }

    size_t wi = 0;
    node = target;
    while (node && node != lca) {
      if (KosDynNode_IS_WIND(node)) {
        KosDynWind *wind = (KosDynWind *) node;
        if (! wind->entered && wind->before_code) {
          wind_nodes[wi++] = wind;
        }
      }
      node = node->parent;
    }

    /* Add in reverse order (outer to inner) */
    while (wi--) {
      KosDynWind *wind = wind_nodes[wi];
      plan->actions[idx].step = KOS_XFER_RUN_HOOK;
      plan->actions[idx].data.hook.code = wind->before_code;
      plan->actions[idx].data.hook.pc = wind->before_pc;
      idx++;
    }

    PyMem_Free(wind_nodes);
  }

  /* INJECT action */
  plan->actions[idx].step = KOS_XFER_INJECT;
  plan->actions[idx].data.inject.value = value;
  plan->actions[idx].data.inject.is_throw = is_throw;
  Py_XINCREF(value);
  idx++;

  *out_plan = plan;
  return 0;
}


void kos_free_transfer_plan(KosTransferPlan *plan) {
  if (! plan)
    return;

  /* DECREF any inject values we still hold */
  for (size_t i = plan->action_index; i < plan->action_count; i++) {
    if (plan->actions[i].step == KOS_XFER_INJECT) {
      Py_XDECREF(plan->actions[i].data.inject.value);
    }
  }

  /* Release the multi-shot clone (if any) without DECREF'ing the continuation
   * so we do not free k before releasing plan->resume_stack. */
  if (plan->invoke_clone) {
    KosContinuation *clone = plan->invoke_clone;
    plan->invoke_clone = NULL;
    if (clone->resume_stack) {
      KosStackBlock_ReleaseChainExclude(clone->resume_stack,
                                        plan->continuation_ref);
      clone->resume_stack = NULL;
    }
    Py_XDECREF(clone->token_ref);
    PyMem_Free(clone);
  }

  /* Release the retained resume_stack reference */
  if (plan->resume_stack) {
    KosStackBlock *b = plan->resume_stack;
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  Py_XDECREF(plan->continuation_ref);
  plan->continuation_ref = NULL;

  PyMem_Free(plan->actions);
  PyMem_Free(plan);
}


int kos_run_transfer_step(KosContext *ctx) {
  if (! ctx->plan)
    return 1;  /* no plan, complete */

  if (ctx->plan->action_index >= ctx->plan->action_count)
    return 1;  /* plan complete */

  KosTransferAction *action = &ctx->plan->actions[ctx->plan->action_index];
  if (KOS_XFER_TRACE)
    SAYF("[xfer] step %zu: plan=%p actions=%p step_type=%d (INSTALL=%d)",
         (size_t) (ctx->plan->action_index),
         (void *) ctx->plan, (void *) ctx->plan->actions,
         (int) action->step, (int) KOS_XFER_INSTALL);
  ctx->plan->action_index++;

  switch (action->step) {
    case KOS_XFER_RUN_HOOK:
      /* Start hook execution - return 0 to indicate more work */
      return kos_begin_hook(ctx, action->data.hook.code, action->data.hook.pc);

    case KOS_XFER_INSTALL: {
      /* Instantiate from template: clone the segment so we evaluate a new
       * copy (no mutated stack or broken back-links). */
      KosDynNode *template_seg = action->data.install.segment;
      KosDynNode *template_bottom = template_seg;
      while (template_bottom && template_bottom->parent)
        template_bottom = template_bottom->parent;
      KosDynNode *stop = template_bottom ? template_bottom->parent : NULL;
      KosDynNode *seg = kos_clone_chain_segment(template_seg, stop);
      if (! seg && template_seg) {
        if (KOS_XFER_TRACE)
          SAY("[xfer] INSTALL clone segment failed");
        ctx->plan->action_index--;  /* leave step for retry or caller */
        return -1;
      }
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INSTALL segment=%p (fresh) resume_pc=%u (before: dyn_top=%p frame_top=%p)",
             (void *) seg, (unsigned) ctx->plan->resume_pc,
             (void *) ctx->dyn_top, (void *) ctx->frame_top);
      ctx->dyn_top = seg;
      kos_update_frame_top(ctx);
      /* New segment is the whole chain; topmost frame is the globals frame. */
      ctx->globals_frame = ctx->frame_top;
      ctx->globals_cache_version = ctx->chain_version + 1;
      /* Restore stack from plan (fresh copy from template). */
      if (ctx->frame_top && ctx->plan->resume_stack) {
        KosStackBlock *new_stack = KosStackBlock_COW(ctx->plan->resume_stack);
        KosStackBlock *old_stack = ctx->frame_top->stack;
        ctx->frame_top->stack = new_stack;
        if (old_stack && old_stack != new_stack) {
          KosStackBlock *b = old_stack;
          while (b) {
            KosStackBlock *p = KosStackBlock_PARENT(b);
            KosStackBlock_Release(b);
            b = p;
          }
        }
      }
      if (ctx->plan->resume_pc != (uint32_t) -1 && ctx->frame_top) {
        ctx->frame_top->pc = ctx->plan->resume_pc;
      }
      ctx->chain_version++;
      ctx->globals_cache_version = ctx->chain_version;
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INSTALL after: dyn_top=%p frame_top=%p",
             (void *) ctx->dyn_top, (void *) ctx->frame_top);
      break;
    }

    case KOS_XFER_INJECT:
      if (KOS_XFER_TRACE)
        SAYF("[xfer] INJECT frame_top=%p stack=%p is_throw=%d",
             (void *) ctx->frame_top,
             ctx->frame_top ? (void *) ctx->frame_top->stack : NULL,
             action->data.inject.is_throw);
      if (action->data.inject.is_throw) {
        ctx->pending_throw = 1;
        Py_XINCREF(action->data.inject.value);
        Py_XDECREF(ctx->pending_exc);
        ctx->pending_exc = action->data.inject.value;
      } else {
        /* Push value onto frame's stack (PushRef allocates block if NULL) */
        if (ctx->frame_top &&
            KosStackBlock_PushRef(&ctx->frame_top->stack,
                                  action->data.inject.value) < 0) {
          if (KOS_XFER_TRACE)
            SAY("[xfer] INJECT PushRef failed");
          /* PushRef already set exception */
          Py_XDECREF(action->data.inject.value);
          action->data.inject.value = NULL;
          return -1;
        }
      }
      Py_XDECREF(action->data.inject.value);
      action->data.inject.value = NULL;
      break;
  }

  /* Check if plan is complete */
  if (ctx->plan->action_index >= ctx->plan->action_count)
    return 1;

  return 0;  /* more steps remain */
}


void kos_suspend_transfer_plan(KosContext *ctx) {
  if (! ctx->plan)
    return;

  ctx->plan->suspended = ctx->plan_stack;
  ctx->plan_stack = ctx->plan;
  ctx->plan = NULL;
}


int kos_resume_transfer_plan(KosContext *ctx) {
  if (! ctx->plan_stack)
    return 0;  /* nothing to resume */

  ctx->plan = ctx->plan_stack;
  ctx->plan_stack = ctx->plan->suspended;
  ctx->plan->suspended = NULL;

  /* Check if chain has changed */
  if (ctx->chain_version != ctx->plan->chain_version) {
    /* Chain changed during nested transfer - would need to replan */
    /* For now, just continue with existing plan */
    /* TODO: implement replanning */
  }

  return 0;
}


/* === Hook execution === */


int kos_begin_hook(KosContext *ctx, KosCode *code, uint32_t pc) {
  if (ctx->hook_runner.active) {
    PyErr_SetString(PyExc_RuntimeError, "hook already active");
    return -1;
  }

  ctx->hook_runner.code = code;
  ctx->hook_runner.pc = pc;
  ctx->hook_runner.scratch = NULL;  /* TODO: allocate scratch stack */
  ctx->hook_runner.active = 1;

  ctx->flags |= KOS_CTX_IN_HOOK;

  return 0;
}


int kos_hook_active(KosContext *ctx) {
  return ctx->hook_runner.active;
}


int kos_end_hook(KosContext *ctx) {
  if (! ctx->hook_runner.active) {
    PyErr_SetString(PyExc_RuntimeError, "no hook active");
    return -1;
  }

  /* Clean up scratch stack */
  if (ctx->hook_runner.scratch) {
    KosStackBlock *b = ctx->hook_runner.scratch;
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  ctx->hook_runner.code = NULL;
  ctx->hook_runner.pc = 0;
  ctx->hook_runner.scratch = NULL;
  ctx->hook_runner.active = 0;

  ctx->flags &= ~KOS_CTX_IN_HOOK;

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
         (void *) k, k->one_shot, (void *) k->captured_chain);
  if (! k->one_shot) {
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
         (void *) ctx->dyn_top, (void *) to_use->captured_chain);
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

  plan->resume_pc = k->resume_pc;
  plan->resume_frame = to_use->resume_frame;  /* template; INSTALL uses fresh */
  /* COW ref to template stack for this invoke; released in kos_free_transfer_plan. */
  plan->resume_stack = to_use->resume_stack
      ? KosStackBlock_COW(to_use->resume_stack) : NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan resume_frame=%p resume_stack=%p",
         (void *) plan->resume_frame, (void *) plan->resume_stack);

  ctx->plan = plan;
  ctx->flags |= KOS_CTX_IN_TRANSFER;
  plan->continuation_ref = NULL;
  if (continuation_ref) {
    Py_INCREF(continuation_ref);
    plan->continuation_ref = continuation_ref;
  }

  if (to_use != k) {
    to_use->captured_chain = NULL;
    plan->invoke_clone = to_use;
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

  switch (node->kind) {
    case KOS_DYN_FRAME: {
      KosDynFrame *frame = (KosDynFrame *) node;
      Py_VISIT(frame->ns);
      /* Traverse stack */
      KosStackBlock *b = frame->stack;
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
      Py_VISIT(ns_node->saved_ns);
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
      Py_VISIT(region->result);
      Py_VISIT(region->iter);
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
    node = node->parent;
  }

  return 0;
}


int kos_continuation_traverse(
    KosContinuation *k,
    visitproc visit,
    void *arg) {

  if (! k)
    return 0;

  Py_VISIT(k->token_ref);
  return kos_dynchain_traverse(k->captured_chain, NULL, visit, arg);
}


int kos_transferplan_traverse(
    KosTransferPlan *plan,
    visitproc visit,
    void *arg) {

  if (! plan)
    return 0;

  Py_VISIT(plan->continuation_ref);

  for (size_t i = plan->action_index; i < plan->action_count; i++) {
    if (plan->actions[i].step == KOS_XFER_INJECT) {
      Py_VISIT(plan->actions[i].data.inject.value);
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
