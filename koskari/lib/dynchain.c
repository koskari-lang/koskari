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
   :ai-assistant: claude-4.6-sonnet-high via Cursor
*/


#include "lib.h"
#include "debug.h"
#include "tracing.h"

#include <stdint.h>


/* === Forward declarations === */


static void kos_update_frame_top(KosContext *ctx);
static int kos_dyn_uninstall_handler(KosContext *ctx, KosDynNode *node, KosEvent *out_ev);
static int kos_dyn_wind_on_control_returned(KosContext *ctx, KosDynWind *wind);
static int kos_dyn_with_on_control_returned(KosContext *ctx, KosDynWith *with_node, int do_pop_node);
static int kos_dyn_with_on_enter_control_returned(KosContext *ctx, KosDynWith *with_node);
int kos_dyn_push(KosContext *ctx, KosDynNode *node);


/* === Debug helper functions === */


static const char *kos_dynnode_kind_names[] = {
  "FRAME",
  "PROMPT",
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
  "DYN_WITH",
  "SHIFT",
  "CC_RAISE",
  "UNKNOWN",
};


const char *kos_dynnode_kind_name(KosDynKind kind) {
  if (kind < 0 || kind >= KOS_DYN_UNKNOWN) {
    return "UNKNOWN";
  }
  return kos_dynnode_kind_names[kind];
}


/* === DynNode base operations === */


static void kos_dynnode_init(
    KosDynNode *node,
    KosDynKind kind,
    KosDynNode *parent,
    uint32_t caps) {

  KosDynNode_PARENT(node) = parent;
  KosDynNode_KIND(node) = kind;
  KosDynNode_FLAGS(node) = KOS_DYNF_NONE;
  KosDynNode_CAPS(node) = caps;
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

  kos_dynnode_init(frame, KOS_DYN_FRAME, parent, 0);
  KosDynFrame_CODE(frame) = code;  /* borrowed */
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_STACK(frame) = stack;  /* ownership transferred */
  KosDynFrame_NS(frame) = ns;  /* ownership transferred (already stole ref) */
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
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

  kos_dynnode_init(clone, KOS_DYN_FRAME, NULL, KosDynNode_CAPS(frame));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(frame) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(frame);
  KosDynFrame_CODE(clone) = KosDynFrame_CODE(frame);  /* borrowed */
  KosDynFrame_PC(clone) = KosDynFrame_PC(frame);
  KosDynFrame_ACTING_PC(clone) = KosDynFrame_ACTING_PC(frame);
  KosDynFrame_STACK(clone) = cloned_stack;
  KosDynFrame_NS(clone) = KosDynFrame_NS(frame);
  Py_XINCREF(KosDynFrame_NS(clone));
  KosDynFrame_FLAGS(clone) = KosDynFrame_FLAGS(frame);
  KosDynFrame_NATIVE_HANDLER(clone) = KosDynFrame_NATIVE_HANDLER(frame);
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


KosDynFrame *kos_dyn_push_frame_empty(KosContext *ctx) {
  PyObject *ns = PyDict_New();
  if (! ns)
    return NULL;
  return kos_dyn_push_frame(ctx, NULL, ns);
}


void kos_dynframe_reset_for_tail(KosDynFrame *frame) {
  if (! frame)
    return;

  /* Release the stack block chain */
  KosStackBlock *block = KosDynFrame_STACK(frame);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }
  KosDynFrame_STACK(frame) = NULL;

  /* Replace ns with empty dict */
  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = PyDict_New();

  KosDynFrame_CODE(frame) = NULL;
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
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


/* === KosDynPrompt implementation === */


KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent,
                   KOS_DYN_CAP_BARRIER
                   | KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynPrompt_ID(prompt) = prompt_id;
  KosDynPrompt_STACK(prompt) = NULL;
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  return prompt;
}


static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_PROMPT, NULL, KosDynNode_CAPS(prompt));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(prompt) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(prompt);
  KosDynPrompt_ID(clone) = KosDynPrompt_ID(prompt);
  KosDynPrompt_STACK(clone) = KosDynPrompt_STACK(prompt);  /* shared, do not free */
  KosDynPrompt_PENDING_SHIFT_WRAPPER(clone) = NULL;
  return clone;
}


void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  Py_XDECREF(KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt));
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  kos_dynnode_return_to_freelist(prompt);
}


KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynprompt_new(KosContext_DYN_TOP(ctx), prompt_id);
  if (! prompt)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) prompt) < 0) {
    kos_dynprompt_free(prompt);
    return NULL;
  }

  return prompt;
}


void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    return;
  }

  kos_dyn_uninstall_handler(ctx, (KosDynNode *) prompt, NULL);

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(prompt);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynprompt_free(prompt);
}


/* === KosDynCC implementation === */


static KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = kos_dynnode_alloc();
  if (! cc)
    return NULL;

  kos_dynnode_init(cc, KOS_DYN_CC, parent, 0);
  KosDynCC_RESUME_PC(cc) = resume_pc;
  return cc;
}


static KosDynCC *kos_dyncc_clone(KosDynCC *cc) {
  if (! cc)
    return NULL;

  KosDynCC *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_CC, NULL, KosDynNode_CAPS(cc));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(cc) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(cc);
  KosDynCC_RESUME_PC(clone) = KosDynCC_RESUME_PC(cc);
  return clone;
}


static void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  kos_dynnode_return_to_freelist(cc);
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


/* === KosDynShift implementation === */


static KosDynShift *kos_dynshift_new(KosDynNode *parent) {
  KosDynShift *shift = (KosDynShift *) kos_dynnode_alloc();
  if (! shift)
    return NULL;

  kos_dynnode_init((KosDynNode *) shift, KOS_DYN_SHIFT, parent, 0);
  return shift;
}


static KosDynShift *kos_dynshift_clone(KosDynShift *shift) {
  if (! shift)
    return NULL;

  KosDynShift *clone = (KosDynShift *) kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_SHIFT, NULL,
                   KosDynNode_CAPS((KosDynNode *) shift));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(shift) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(shift);
  return clone;
}


static void kos_dynshift_free(KosDynShift *shift) {
  if (! shift)
    return;

  kos_dynnode_return_to_freelist(shift);
}


int kos_dyn_push_shift(KosContext *ctx) {
  KosDynShift *shift = kos_dynshift_new(KosContext_DYN_TOP(ctx));
  if (! shift)
    return -1;

  return kos_dyn_push(ctx, (KosDynNode *) shift);
}


/* === KosDynCCRaise implementation === */


static KosDynCCRaise *kos_dyn_cc_raise_new(KosDynNode *parent, PyObject *exc_type, PyObject *exc_val) {
  KosDynCCRaise *node = (KosDynCCRaise *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_CC_RAISE, parent,
                   KOS_DYN_CAP_NEEDS_UNWIND_ACTION);
  Py_INCREF(exc_type);
  KosDynCCRaise_EXC_TYPE(node) = exc_type;
  Py_INCREF(exc_val);
  KosDynCCRaise_EXC_VAL(node) = exc_val;
  return node;
}


static void kos_dyn_cc_raise_free(KosDynCCRaise *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynCCRaise_EXC_TYPE(node));
  KosDynCCRaise_EXC_TYPE(node) = NULL;
  Py_XDECREF(KosDynCCRaise_EXC_VAL(node));
  KosDynCCRaise_EXC_VAL(node) = NULL;
  kos_dynnode_return_to_freelist(node);
}


static int cc_raise_unwind(KosContext *ctx, KosDynNode *node,
                           KosEvent *out_ev) {
  (void) ctx;
  (void) out_ev;
  PyObject *exc_type = KosDynCCRaise_EXC_TYPE(node);
  PyObject *exc_val = KosDynCCRaise_EXC_VAL(node);
  if (exc_type && exc_val) {
    Py_INCREF(exc_val);
    PyErr_SetObject(exc_type, exc_val);
    Py_DECREF(exc_val);
  } else if (exc_val) {
    PyObject *typ = (PyObject *) Py_TYPE(exc_val);
    Py_INCREF(exc_val);
    PyErr_SetObject(typ, exc_val);
    Py_DECREF(exc_val);
  } else {
    PyErr_SetString(PyExc_RuntimeError,
                    "cc_raise_unwind: missing exception value");
  }
  return -1;
}


/* === KosDynWind implementation === */


KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynnode_alloc();
  if (! wind)
    return NULL;

  uint32_t wind_caps = 0;
  if (after_thunk)
    wind_caps |= KOS_DYN_CAP_NEEDS_UNWIND_ACTION;
  if (before_thunk)
    wind_caps |= KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION;
  if (before_thunk || after_thunk)
    wind_caps |= KOS_DYN_CAP_AWAITS_THUNK_RESULT;
  kos_dynnode_init(wind, KOS_DYN_WIND, parent, wind_caps);
  KosDynWind_BEFORE_THUNK(wind) = before_thunk;
  if (before_thunk)
    Py_INCREF(before_thunk);
  KosDynWind_AFTER_THUNK(wind) = after_thunk;
  if (after_thunk)
    Py_INCREF(after_thunk);
  KosDynWind_ENTERED(wind) = 0;
  KosDynWind_PHASE(wind) = KOS_WIND_PHASE_IDLE;
  return wind;
}


KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  KosDynWind *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_WIND, NULL, KosDynNode_CAPS(wind));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(wind) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(wind);
  KosDynWind_BEFORE_THUNK(clone) = KosDynWind_BEFORE_THUNK(wind);
  if (KosDynWind_BEFORE_THUNK(clone))
    Py_INCREF(KosDynWind_BEFORE_THUNK(clone));
  KosDynWind_AFTER_THUNK(clone) = KosDynWind_AFTER_THUNK(wind);
  if (KosDynWind_AFTER_THUNK(clone))
    Py_INCREF(KosDynWind_AFTER_THUNK(clone));
  KosDynWind_BODY_RESULT(clone) = NULL;
  KosDynWind_ENTERED(clone) = KosDynWind_ENTERED(wind);
  KosDynWind_PHASE(clone) = KosDynWind_PHASE(wind);
  return clone;
}


void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  Py_XDECREF(KosDynWind_BEFORE_THUNK(wind));
  KosDynWind_BEFORE_THUNK(wind) = NULL;
  Py_XDECREF(KosDynWind_AFTER_THUNK(wind));
  KosDynWind_AFTER_THUNK(wind) = NULL;
  Py_XDECREF(KosDynWind_BODY_RESULT(wind));
  KosDynWind_BODY_RESULT(wind) = NULL;
  kos_dynnode_return_to_freelist(wind);
}


int kos_dyn_wind_wind(KosContext *ctx, KosDynWind *node) {
  if (! node || ! KosDynWind_BEFORE_THUNK(node))
    return 0;

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  if (kos_callable_vcall(ctx, NULL, KosDynWind_BEFORE_THUNK(node), NULL, 0, NULL) < 0) {
    return -1;
  }

  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


int kos_dyn_wind_unwind(KosContext *ctx, KosDynWind *node) {
  if (! node || ! KosDynWind_ENTERED(node))
    return 0;

  if (! KosDynWind_AFTER_THUNK(node)) {
    /* No after-thunk: push stored body result and finish uninstall */
    KosDynFrame *frame_below = (KosDynFrame *) KosDynNode_PARENT(node);
    PyObject *body_result = KosDynWind_BODY_RESULT(node);
    KosDynWind_BODY_RESULT(node) = NULL;
    if (frame_below && KosDynFrame_STACK(frame_below) && body_result) {
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame_below), body_result) < 0) {
        KosDynWind_BODY_RESULT(node) = body_result;
        return -1;
      }
    }
    if (body_result)
      Py_DECREF(body_result);
    return 0;
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_AFTER;
  {
    PyObject *args = PyTuple_New(0);
    if (! args)
      return -1;
    if (kos_callable_call(ctx, NULL, KosDynWind_AFTER_THUNK(node), args, NULL) < 0) {
      Py_DECREF(args);
      return -1;
    }
    Py_DECREF(args);
  }
  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


static int kos_dyn_wind_on_control_returned(KosContext *ctx, KosDynWind *wind) {
  KosDynFrame *frame_below = (KosDynFrame *) KosDynNode_PARENT(wind);
  if (! frame_below || ! KosDynFrame_STACK(frame_below))
    return 0;

  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN) {
    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_BEGIN;
    KosDynWind_ENTERED(wind) = 1;
    PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_below));
    if (val)
      Py_DECREF(val);
    return 0;
  }
  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER) {
    PyObject *thunk_ret = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_below));
    if (thunk_ret)
      Py_DECREF(thunk_ret);

    PyObject *body_result = KosDynWind_BODY_RESULT(wind);
    KosDynWind_BODY_RESULT(wind) = NULL;

    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_AFTER;
    KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
    KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(wind);
    KosContext_CHAIN_VERSION(ctx)++;
    KosDynNode_PARENT(wind) = NULL;
    kos_dynwind_free(wind);
    kos_update_frame_top(ctx);

    if (body_result) {
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame_below), body_result) < 0) {
        Py_DECREF(body_result);
        return -1;
      }
      Py_DECREF(body_result);
    }
    return 0;
  }
  return 0;
}


int kos_dynnode_on_control_returned(KosContext *ctx, KosDynNode *node, KosEvent *out_ev) {
  (void) out_ev;
  /* When node is a frame, check if parent is WITH in pending phase. */
  if (KosDynNode_IS_FRAME(node)) {
    KosDynNode *parent = KosDynNode_PARENT(node);
    if (parent && KosDynNode_IS_WITH(parent)) {
      uint8_t phase = KosDynWith_PHASE((KosDynWith *) parent);
      if (phase == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        return kos_dyn_with_on_control_returned(ctx, (KosDynWith *) parent, 1);
      if (phase == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
        return kos_dyn_with_on_enter_control_returned(ctx, (KosDynWith *) parent);
    }
  }
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_WIND:
      return kos_dyn_wind_on_control_returned(ctx, (KosDynWind *) node);
    case KOS_DYN_WITH:
    case KOS_DYN_DYN_WITH:
      if (KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
        return kos_dyn_with_on_control_returned(ctx, (KosDynWith *) node, 1);
      return 0;
    default:
      return 0;
  }
}


KosDynWind *kos_dyn_push_wind(
    KosContext *ctx,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynwind_new(KosContext_DYN_TOP(ctx),
                                     before_thunk, after_thunk);
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


/* === KOS_DYN_TRY / FINALLY / LOOP / FOREACH === */


KosDynTry *kos_dyntry_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynTry *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_TRY, parent, KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynTry_HANDLER_PC(node) = handler_pc;
  KosDynTry_ENABLED(node) = 1;
  return node;
}


KosDynFinally *kos_dynfinally_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_FINALLY, parent, KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynFinally_HANDLER_PC(node) = handler_pc;
  KosDynFinally_ENABLED(node) = 1;
  KosDynFinally_HANDLING_RAISE(node) = 0;
  KosDynFinally_HANDLING_RESULT(node) = NULL;
  return node;
}


KosDynLoop *kos_dynloop_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynLoop *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_LOOP, parent, 0);
  KosDynLoop_BREAK_PC(node) = break_pc;
  KosDynLoop_CONTINUE_PC(node) = continue_pc;
  KosDynLoop_ENABLED(node) = 1;
  KosDynLoop_RESULT(node) = NULL;
  return node;
}


KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_FOREACH, parent, 0);
  KosDynForeach_BREAK_PC(node) = break_pc;
  KosDynForeach_CONTINUE_PC(node) = continue_pc;
  KosDynForeach_ENABLED(node) = 1;
  KosDynForeach_RESULT(node) = NULL;
  KosDynForeach_ITER(node) = NULL;
  KosDynForeach_EXHAUSTED(node) = 0;
  KosDynForeach_ITER_NEXT_ELSE_PC(node) = 0;
  return node;
}


KosDynTry *kos_dyntry_clone(KosDynTry *node) {
  KosDynTry *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_TRY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynTry_HANDLER_PC(clone) = KosDynTry_HANDLER_PC(node);
  KosDynTry_ENABLED(clone) = KosDynTry_ENABLED(node);
  return clone;
}


KosDynFinally *kos_dynfinally_clone(KosDynFinally *node) {
  KosDynFinally *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FINALLY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynFinally_HANDLER_PC(clone) = KosDynFinally_HANDLER_PC(node);
  KosDynFinally_ENABLED(clone) = KosDynFinally_ENABLED(node);
  KosDynFinally_HANDLING_RAISE(clone) = KosDynFinally_HANDLING_RAISE(node);
  KosDynFinally_HANDLING_RESULT(clone) = KosDynFinally_HANDLING_RESULT(node);
  Py_XINCREF(KosDynFinally_HANDLING_RESULT(clone));
  return clone;
}


KosDynLoop *kos_dynloop_clone(KosDynLoop *node) {
  KosDynLoop *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_LOOP, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynLoop_BREAK_PC(clone) = KosDynLoop_BREAK_PC(node);
  KosDynLoop_CONTINUE_PC(clone) = KosDynLoop_CONTINUE_PC(node);
  KosDynLoop_ENABLED(clone) = KosDynLoop_ENABLED(node);
  KosDynLoop_RESULT(clone) = KosDynLoop_RESULT(node);
  Py_XINCREF(KosDynLoop_RESULT(clone));
  return clone;
}


KosDynForeach *kos_dynforeach_clone(KosDynForeach *node) {
  KosDynForeach *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FOREACH, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynForeach_BREAK_PC(clone) = KosDynForeach_BREAK_PC(node);
  KosDynForeach_CONTINUE_PC(clone) = KosDynForeach_CONTINUE_PC(node);
  KosDynForeach_ENABLED(clone) = KosDynForeach_ENABLED(node);
  KosDynForeach_RESULT(clone) = KosDynForeach_RESULT(node);
  Py_XINCREF(KosDynForeach_RESULT(clone));
  KosDynForeach_ITER(clone) = KosDynForeach_ITER(node);
  Py_XINCREF(KosDynForeach_ITER(clone));
  KosDynForeach_EXHAUSTED(clone) = KosDynForeach_EXHAUSTED(node);
  KosDynForeach_ITER_NEXT_ELSE_PC(clone) = KosDynForeach_ITER_NEXT_ELSE_PC(node);
  return clone;
}


void kos_dyntry_free(KosDynTry *node) {
  kos_dynnode_return_to_freelist(node);
}


void kos_dynfinally_free(KosDynFinally *node) {
  Py_XDECREF(KosDynFinally_HANDLING_RESULT(node));
  kos_dynnode_return_to_freelist(node);
}


void kos_dynloop_free(KosDynLoop *node) {
  Py_XDECREF(KosDynLoop_RESULT(node));
  kos_dynnode_return_to_freelist(node);
}


void kos_dynforeach_free(KosDynForeach *node) {
  Py_XDECREF(KosDynForeach_RESULT(node));
  Py_XDECREF(KosDynForeach_ITER(node));
  kos_dynnode_return_to_freelist(node);
}


KosDynTry *kos_dyn_push_try(KosContext *ctx, uint32_t handler_pc) {
  KosDynTry *node = kos_dyntry_new(KosContext_DYN_TOP(ctx), handler_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


KosDynFinally *kos_dyn_push_finally(KosContext *ctx, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynfinally_new(KosContext_DYN_TOP(ctx), handler_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


KosDynLoop *kos_dyn_push_loop(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc) {
  KosDynLoop *node = kos_dynloop_new(KosContext_DYN_TOP(ctx), break_pc, continue_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


KosDynForeach *kos_dyn_push_foreach(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynforeach_new(KosContext_DYN_TOP(ctx), break_pc, continue_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


void kos_dyn_pop_try(KosContext *ctx, KosDynTry *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dyntry_free(node);
  }
}


void kos_dyn_pop_finally(KosContext *ctx, KosDynFinally *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dynfinally_free(node);
  }
}


void kos_dyn_pop_loop(KosContext *ctx, KosDynLoop *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dynloop_free(node);
  }
}


void kos_dyn_pop_foreach(KosContext *ctx, KosDynForeach *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dynforeach_free(node);
  }
}


/* === KosDynNs (namespace scope) implementation === */


static KosDynNs *kos_dynns_new(KosDynNode *parent, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynnode_alloc();
  if (! ns_node) {
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init(ns_node, KOS_DYN_NS, parent, 0);
  KosDynNs_SAVED_NS(ns_node) = saved_ns;  /* caller transferred ownership */
  return ns_node;
}


static KosDynNs *kos_dynns_clone(KosDynNs *ns_node) {
  if (! ns_node)
    return NULL;

  PyObject *saved = KosDynNs_SAVED_NS(ns_node);
  Py_INCREF(saved);
  KosDynNs *clone = kos_dynns_new(NULL, saved);
  if (clone)
    KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(ns_node);
  return clone;
}


static void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  Py_DECREF(KosDynNs_SAVED_NS(ns_node));
  kos_dynnode_return_to_freelist(ns_node);
}


void kos_dyn_ns_unwind(KosContext *ctx, KosDynNs *node) {
  if (! node)
    return;
  if (KosContext_FRAME_TOP(ctx)) {
    PyObject *prior = KosDynNs_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
  }
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


/* === KosDynCollect implementation === */


static KosDynCollect *kos_dyncollect_new(KosDynNode *parent, PyObject *saved_ns) {
  KosDynCollect *col_node = (KosDynCollect *) kos_dynnode_alloc();
  if (! col_node) {
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) col_node, KOS_DYN_COLLECT, parent, 0);
  KosDynCollect_SAVED_NS(col_node) = saved_ns;
  return col_node;
}


static KosDynCollect *kos_dyncollect_clone(KosDynCollect *col_node) {
  if (! col_node)
    return NULL;

  PyObject *saved = KosDynCollect_SAVED_NS(col_node);
  Py_INCREF(saved);
  KosDynCollect *clone = kos_dyncollect_new(NULL, saved);
  if (clone)
    KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID((KosDynNode *) col_node);
  return clone;
}


static void kos_dyncollect_free(KosDynCollect *col_node) {
  if (! col_node)
    return;

  Py_DECREF(KosDynCollect_SAVED_NS(col_node));
  kos_dynnode_return_to_freelist((KosDynNode *) col_node);
}


void kos_dyn_collect_unwind(KosContext *ctx, KosDynCollect *node) {
  if (! node)
    return;
  if (KosContext_FRAME_TOP(ctx)) {
    PyObject *prior = KosDynCollect_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
  }
}


KosDynCollect *kos_dyn_push_collect(KosContext *ctx, PyObject *saved_ns) {
  KosDynCollect *col_node = kos_dyncollect_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! col_node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) col_node;
  KosContext_CHAIN_VERSION(ctx)++;

  return col_node;
}


void kos_dyn_pop_collect(KosContext *ctx, KosDynCollect *node) {
  if (! KosContext_DYN_TOP(ctx) ||
      KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncollect_free(node);
}


/* === KosDynYieldFrom implementation === */


KosDynYieldFrom *kos_dynyieldfrom_new(
    KosDynNode *parent,
    PyObject *delegate,
    uint8_t kind) {

  KosDynYieldFrom *node = (KosDynYieldFrom *) kos_dynnode_alloc();
  if (! node) {
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_YIELD_FROM, parent,
                   KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP);
  Py_INCREF(delegate);
  KosDynYieldFrom_DELEGATE(node) = delegate;
  KosDynYieldFrom_KIND(node) = kind;
  KosDynYieldFrom_PENDING_ACTION(node) = 0;
  KosDynYieldFrom_PENDING_ARG(node) = NULL;
  KosDynYieldFrom_PENDING_ARG_TYPE(node) = NULL;
  KosDynYieldFrom_INFLIGHT(node) = NULL;
  return node;
}


static void kos_dynyieldfrom_free(KosDynYieldFrom *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynYieldFrom_DELEGATE(node));
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(node));
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(node));
  Py_XDECREF(KosDynYieldFrom_INFLIGHT(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static KosDynYieldFrom *kos_dynyieldfrom_clone(KosDynYieldFrom *node) {

  KosDynYieldFrom *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_YIELD_FROM, NULL,
                   KosDynNode_CAPS(node));

  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);

  KosDynYieldFrom_DELEGATE(clone) = KosDynYieldFrom_DELEGATE(node);
  Py_XINCREF(KosDynYieldFrom_DELEGATE(clone));

  KosDynYieldFrom_KIND(clone) = KosDynYieldFrom_KIND(node);
  KosDynYieldFrom_PENDING_ACTION(clone) = KosDynYieldFrom_PENDING_ACTION(node);
  KosDynYieldFrom_PENDING_ARG(clone) = KosDynYieldFrom_PENDING_ARG(node);
  KosDynYieldFrom_PENDING_ARG_TYPE(clone) = KosDynYieldFrom_PENDING_ARG_TYPE(node);

  if (KosDynYieldFrom_PENDING_ARG(clone))
    Py_INCREF(KosDynYieldFrom_PENDING_ARG(clone));
  if (KosDynYieldFrom_PENDING_ARG_TYPE(clone))
    Py_INCREF(KosDynYieldFrom_PENDING_ARG_TYPE(clone));

  KosDynYieldFrom_INFLIGHT(clone) = KosDynYieldFrom_INFLIGHT(node);
  if (KosDynYieldFrom_INFLIGHT(clone))
    Py_INCREF(KosDynYieldFrom_INFLIGHT(clone));

  return clone;
}


void kos_dyn_yieldfrom_unwind(KosContext *ctx, KosDynYieldFrom *node) {
  (void) ctx;
  if (node)
    kos_yieldfrom_close_delegate(node);
}


/* === KosDynParam implementation === */


/* Sentinel for "param was unset" in saved dict; value must not be a valid param value.
   Initialized in koskari_dynchain_init, cleared in koskari_dynchain_clear. */
static PyObject *_param_sentinel_unset = NULL;


static KosDynParam *kos_dynparam_new_from_bindings(
    KosDynNode *parent,
    PyObject *bindings) {

  KosDynParam *node = (KosDynParam *) kos_dynnode_alloc();
  if (! node) {
    Py_DECREF(bindings);
    return NULL;
  }

  kos_dynnode_init(
    (KosDynNode *) node,
    KOS_DYN_PARAM,
    parent,
    KOS_DYN_CAP_NEEDS_UNWIND_ACTION | KOS_DYN_CAP_NEEDS_INSTALL_WIND);

  KosDynParam_BINDINGS(node) = bindings;
  KosDynParam_SAVED(node) = NULL;

  return node;
}


static KosDynParam *kos_dynparam_new(
    KosDynNode *parent,
    uint32_t count,
    PyObject **params,
    PyObject **values) {

  if (count == 0) {
    PyErr_SetString(PyExc_ValueError, "KosDynParam requires at least one binding");
    return NULL;
  }

  PyObject *bindings = PyDict_New();
  if (! bindings)
    return NULL;

  for (uint32_t i = 0; i < count; i++) {
    PyObject *key = KosParamRef_KEY(params[i]);
    if (PyDict_SetItem(bindings, key, values[i]) < 0) {
      Py_DECREF(bindings);
      return NULL;
    }
  }

  return kos_dynparam_new_from_bindings(parent, bindings);
}


static KosDynParam *kos_dynparam_clone(KosDynParam *node) {

  if (! node)
    return NULL;

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *copy = PyDict_Copy(bindings);
  if (! copy)
    return NULL;

  KosDynParam *clone = (KosDynParam *) kos_dynnode_alloc();
  if (! clone) {
    Py_DECREF(copy);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_PARAM, NULL,
                   KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynParam_BINDINGS(clone) = copy;
  KosDynParam_SAVED(clone) = NULL;

  return clone;
}


static void kos_dynparam_free(KosDynParam *node) {

  if (! node)
    return;

  Py_XDECREF(KosDynParam_BINDINGS(node));
  KosDynParam_BINDINGS(node) = NULL;
  Py_XDECREF(KosDynParam_SAVED(node));
  KosDynParam_SAVED(node) = NULL;

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int kos_dynparam_enter(KosContext *ctx, KosDynParam *node) {

  PyObject *domain = KosContext_DOMAIN(ctx);
  if (! domain) {
    PyErr_SetString(PyExc_RuntimeError,
                    "param enter requires active domain");
    return -1;
  }

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *saved = PyDict_New();
  if (! saved)
    return -1;

  PyObject *sentinel = _param_sentinel_unset;
  if (! sentinel) {
    Py_DECREF(saved);
    return -1;
  }

  PyObject *key;
  PyObject *value;
  Py_ssize_t pos = 0;

  while (PyDict_Next(bindings, &pos, &key, &value)) {
    PyObject *cur = kos_domain_get_param(domain, key);
    PyObject *to_save = cur ? cur : sentinel;
    if (PyDict_SetItem(saved, key, to_save) < 0) {
      Py_XDECREF(cur);
      Py_DECREF(saved);
      return -1;
    }
    if (cur)
      Py_DECREF(cur);  /* SetItem holds its own ref */
    if (kos_domain_set_param(domain, key, value) < 0) {
      Py_DECREF(saved);
      return -1;
    }
  }

  KosDynParam_SAVED(node) = saved;
  return 0;
}


static void kos_dynparam_restore(KosContext *ctx, KosDynParam *node) {

  PyObject *domain = KosContext_DOMAIN(ctx);
  if (! domain)
    return;

  PyObject *saved = KosDynParam_SAVED(node);
  if (! saved)
    return;

  PyObject *sentinel = _param_sentinel_unset;
  PyObject *key;
  PyObject *old_val;
  Py_ssize_t pos = 0;

  while (PyDict_Next(saved, &pos, &key, &old_val)) {
    if (old_val == sentinel)
      kos_domain_clear_param(domain, key);
    else
      kos_domain_set_param(domain, key, old_val);
  }
}


int kos_dyn_param_wind(KosContext *ctx, KosDynParam *node) {
  return kos_dynparam_enter(ctx, node);
}


void kos_dyn_param_unwind(KosContext *ctx, KosDynParam *node) {
  if (KOS_XFER_TRACE)
    SAYF("[xfer] param unwind node=%p", (void *) node);
  kos_dynparam_restore(ctx, node);
}


KosDynParam *kos_dyn_push_param_dict(KosContext *ctx, PyObject *bindings) {

  if (! bindings) {
    PyErr_SetString(PyExc_ValueError, "kos_dyn_push_param_dict: bindings is NULL");
    return NULL;
  }
  if (! PyDict_CheckExact(bindings)) {
    PyErr_Format(PyExc_TypeError, "kos_dyn_push_param_dict expected dict, got %R",
                 bindings);
    return NULL;
  }
  if (PyDict_Size(bindings) == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "KosDynParam requires at least one binding");
    return NULL;
  }

  KosDynParam *node = kos_dynparam_new_from_bindings(
      KosContext_DYN_TOP(ctx), bindings);
  if (! node)
    return NULL;

  if (kos_dynparam_enter(ctx, node) < 0) {
    kos_dynparam_free(node);
    return NULL;
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


KosDynParam *kos_dyn_push_param(
    KosContext *ctx,
    uint32_t count,
    PyObject **params,
    PyObject **values) {

  KosDynParam *node = kos_dynparam_new(KosContext_DYN_TOP(ctx),
                                       count, params, values);
  if (! node)
    return NULL;

  if (kos_dynparam_enter(ctx, node) < 0) {
    kos_dynparam_free(node);
    return NULL;
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


void kos_dyn_pop_param(KosContext *ctx, KosDynParam *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  kos_dynparam_restore(ctx, node);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynparam_free(node);
}


/* === KosDynWith (single-shot context manager) === */


static KosDynWith *kos_dynwith_new(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent,
                   KOS_DYN_CAP_CATCH | KOS_DYN_CAP_NEEDS_UNWIND_ACTION);
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


static KosDynWith *kos_dyndynwith_new(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_DYN_WITH, parent,
                   KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION | KOS_DYN_CAP_NEEDS_UNWIND_ACTION
                   | KOS_DYN_CAP_NEEDS_INSTALL_WIND
                  );
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


KosDynWith *kos_dynwith_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* Invariant: do not clone a node in PENDING_EXIT_AWAIT or PENDING_ENTER_AWAIT */
  if (KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT
      || KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return NULL;

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, NULL,
                   KosDynNode_CAPS(src));
  KosDynNode_FLAGS(node) = KosDynNode_FLAGS(src) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(node) = KosDynNode_NODE_ID(src);
  KosDynWith_MGR(node) = KosDynWith_MGR(src);
  Py_XINCREF(KosDynWith_MGR(node));
  KosDynWith_ENTER(node) = KosDynWith_ENTER(src);
  Py_XINCREF(KosDynWith_ENTER(node));
  KosDynWith_EXIT(node) = KosDynWith_EXIT(src);
  Py_XINCREF(KosDynWith_EXIT(node));
  KosDynWith_ENTERED(node) = KosDynWith_ENTERED(src);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


KosDynWith *kos_dyndynwith_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* Invariant: do not clone a node in PENDING_EXIT_AWAIT or PENDING_ENTER_AWAIT */
  if (KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT
      || KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return NULL;

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_DYN_WITH, NULL,
                   KosDynNode_CAPS(src));
  KosDynNode_FLAGS(node) = KosDynNode_FLAGS(src) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(node) = KosDynNode_NODE_ID(src);
  KosDynWith_MGR(node) = KosDynWith_MGR(src);
  Py_XINCREF(KosDynWith_MGR(node));
  KosDynWith_ENTER(node) = KosDynWith_ENTER(src);
  Py_XINCREF(KosDynWith_ENTER(node));
  KosDynWith_EXIT(node) = KosDynWith_EXIT(src);
  Py_XINCREF(KosDynWith_EXIT(node));
  KosDynWith_ENTERED(node) = KosDynWith_ENTERED(src);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


static void kos_dynwith_free(KosDynWith *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynWith_MGR(node));
  KosDynWith_MGR(node) = NULL;
  Py_XDECREF(KosDynWith_ENTER(node));
  KosDynWith_ENTER(node) = NULL;
  Py_XDECREF(KosDynWith_EXIT(node));
  KosDynWith_EXIT(node) = NULL;
  Py_XDECREF(KosDynWith_NAME(node));
  KosDynWith_NAME(node) = NULL;
  if (KosDynWith_PHASE(node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    Py_XDECREF(KosDynWith_EXC_TYP(node));
    KosDynWith_EXC_TYP(node) = NULL;
    Py_XDECREF(KosDynWith_EXC_VAL(node));
    KosDynWith_EXC_VAL(node) = NULL;
    Py_XDECREF(KosDynWith_EXC_TB(node));
    KosDynWith_EXC_TB(node) = NULL;
  }

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


KosDynWith *kos_dyn_push_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


KosDynWith *kos_dyn_push_dyn_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dyndynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


KosDynWith *kos_dyn_push_with_provisional(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


KosDynWith *kos_dyn_push_dyn_with_provisional(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dyndynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


void kos_dyn_pop_with(KosContext *ctx, KosDynWith *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynwith_free(node);
}


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)  KosDynFrame_STACK(CURRENT_FRAME(ctx))


static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val) {
  KosDynFrame *frame = CURRENT_FRAME(ctx);
  if (! frame)
    return -1;
  PyObject *ns = KosDynFrame_NS(frame);
  if (KosNamespace_Check(ns))
    return KosNamespace_Store(ns, name, val);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


static int kos_dyn_with_on_enter_control_returned(KosContext *ctx, KosDynWith *with_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


static int kos_dyn_with_on_control_returned(KosContext *ctx, KosDynWith *with_node,
                                            int do_pop_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *typ = KosDynWith_EXC_TYP(with_node);
  PyObject *val = KosDynWith_EXC_VAL(with_node);
  PyObject *tb = KosDynWith_EXC_TB(with_node);
  KosDynWith_EXC_TYP(with_node) = NULL;
  KosDynWith_EXC_VAL(with_node) = NULL;
  KosDynWith_EXC_TB(with_node) = NULL;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;

  int had_exception = (typ != Py_None);

  if (had_exception && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_DECREF(result);
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
      return -1;
    if (KosContext_FRAME_TOP(ctx))
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
      return -1;
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
      PyErr_Restore(typ, val, tb);
      return -1;
    }
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  PyObject *body_val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! body_val)
    return -1;
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  if (KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
  if (do_pop_node) {
    kos_dyn_pop_with(ctx, with_node);
  }
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), body_val) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  Py_DECREF(body_val);
  return 0;
}


static int kos_dyn_dyn_with_wind(KosContext *ctx, KosDynWith *node, KosEvent *out_ev) {
  PyObject *enter_fn = KosDynWith_ENTER(node);
  int r = kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL);
  if (r == 1)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(node) = 1;
  return 0;
}


int finalize_provisional_with_enter(KosContext *ctx, PyObject *result,
                                    KosDynWith *with_node) {

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/* === Generic pending-exit APIs === */


static KosDynWith *find_pending_exit_with(KosDynNode *node) {
  for (; node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_WITH(node)
        && KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
      return (KosDynWith *) node;
  }
  return NULL;
}


KosDynNode *kos_dyn_find_pending_exit(KosDynNode *node) {
  KosDynWith *w = find_pending_exit_with(node);
  return w ? (KosDynNode *) w : NULL;
}


int kos_dyn_complete_pending_exit(KosContext *ctx, int do_pop_node) {
  KosDynNode *n = kos_dyn_find_pending_exit(KosContext_DYN_TOP(ctx));
  if (! n)
    return 0;
  return kos_dyn_with_on_control_returned(ctx, (KosDynWith *) n, do_pop_node);
}


void kos_dyn_clear_pending_exit(KosContext *ctx) {
  KosDynNode *n = KosContext_DYN_TOP(ctx);
  for (; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_WITH(n)
        && KosDynWith_PHASE((KosDynWith *) n) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
      Py_CLEAR(KosDynWith_EXC_TYP((KosDynWith *) n));
      Py_CLEAR(KosDynWith_EXC_VAL((KosDynWith *) n));
      Py_CLEAR(KosDynWith_EXC_TB((KosDynWith *) n));
      KosDynWith_PHASE((KosDynWith *) n) = KOS_WITH_PHASE_IDLE;
    }
  }
}


int kos_dyn_with_unwind(KosContext *ctx, KosDynWith *node,
                        KosEvent *out_ev) {

  PyObject *exit_fn = KosDynWith_EXIT(node);
  PyObject *typ = NULL;
  PyObject *val = NULL;
  PyObject *tb = NULL;

  if (PyErr_Occurred()) {
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_Clear();
    /* PyErr_Fetch returns new refs or NULL; substitute Py_None for any
       missing component so the caller can unconditionally DECREF all three. */
    if (! typ) { Py_INCREF(Py_None); typ = Py_None; }
    if (! val) { Py_INCREF(Py_None); val = Py_None; }
    if (! tb)  { Py_INCREF(Py_None); tb  = Py_None; }
  }
  else {
    Py_INCREF(Py_None); typ = Py_None;
    Py_INCREF(Py_None); val = Py_None;
    Py_INCREF(Py_None); tb  = Py_None;
  }

  PyObject *args = PyTuple_Pack(3, typ, val, tb);
  if (! args) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return -1;
  }

  int r = kos_callable_call(ctx, out_ev, exit_fn, args, NULL);
  Py_DECREF(args);

  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    /* Suspend occurred inside kos_callable_call. Pending state lives on node. */
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_EXIT_AWAIT;
    KosDynWith_EXC_TYP(node) = typ;
    KosDynWith_EXC_VAL(node) = val;
    KosDynWith_EXC_TB(node) = tb;
    KosDynWith_NORMAL_EXIT(node) = (typ == Py_None);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }

  if (r < 0) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return -1;
  }

  /* r == 0: result is on stack */
  KosDynFrame *frame = KosContext_FRAME_TOP(ctx);
  if (! frame || ! KosDynFrame_STACK(frame)) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return -1;
  }

  PyObject *result = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
  if (! result) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return -1;
  }

  if (typ != Py_None && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    Py_DECREF(result);
    /* Swallow: discard body value, drop block, push None. Caller pops node. */
    PyObject *body_val = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (body_val)
      Py_DECREF(body_val);
    if (KosStackBlock_DropBlock(&KosDynFrame_STACK(frame)) < 0)
      return -1;
    if (frame)
      KosDynFrame_PC(frame) = KosDynWith_RESUME_PC(node);
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), Py_None) < 0)
      return -1;
    return KOS_DYN_UNWIND_SWALLOWED;
  }
  else if (typ != Py_None) {
    PyErr_Restore(typ, val, tb);
  }
  else {
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
  }

  Py_DECREF(result);
  return 0;
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
    case KOS_DYN_DYN_WITH:
      kos_dynwith_free((KosDynWith *) node);
      break;
    case KOS_DYN_SHIFT:
      kos_dynshift_free((KosDynShift *) node);
      break;
    case KOS_DYN_CC_RAISE:
      kos_dyn_cc_raise_free((KosDynCCRaise *) node);
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


KosDynNode *kos_dynnode_clone(KosDynNode *node) {
  return kos_dyn_clone(node);
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


int kos_dyn_push(KosContext *ctx, KosDynNode *node) {
  if (! node)
    return -1;

  KosContext_DYN_TOP(ctx) = node;
  KosContext_CHAIN_VERSION(ctx)++;
  if (KosDynNode_IS_FRAME(node))
    KosContext_FRAME_TOP(ctx) = (KosDynFrame *) node;

  if (kos_dyn_install(ctx, node, NULL) < 0)
    return -1;
  return 0;
}


int kos_dyn_uninstall(KosContext *ctx, KosDynNode *node, KosEvent *out_ev) {
  if (! node || KosContext_DYN_TOP(ctx) != node) {
    PyErr_SetString(PyExc_RuntimeError, "kos_dyn_uninstall: node is not dyn_top");
    return -1;
  }

  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return -1;
  }

  int r = kos_dyn_uninstall_handler(ctx, node, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;

  int was_frame = KosDynNode_IS_FRAME(node);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  if (was_frame)
    kos_update_frame_top(ctx);
  KosDynNode_PARENT(node) = NULL;
  kos_dynnode_free(node);
  return 0;
}


int kos_dyn_push_clone(KosContext *ctx, KosDynNode *node) {
  if (! node)
    return -1;

  KosDynNode *clone = kos_dyn_clone(node);
  if (! clone)
    return -1;

  KosDynNode_PARENT(clone) = KosContext_DYN_TOP(ctx);
  return kos_dyn_push(ctx, clone);
}


/* === Transfer path wind/unwind helpers === */


/**
   Run INSTALL-phase wind (enter) for a node with NEEDS_INSTALL_WIND.
   Used by xfer_step_install and xfer_step_push.
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dynnode_run_wind(KosContext *ctx, KosDynNode *node,
                                KosEvent *out_ev) {
  return kos_dyn_wind(ctx, node, out_ev);
}


/**
   Run transfer-plan unwind for a node with NEEDS_UNWIND_ACTION.
   Uses kos_dyn_unwind; teardown (drop block, pop) for nodes with
   NEEDS_TRANSFER_TEARDOWN. Used by kos_run_transfer_step for EXTENT_ACTION.
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dynnode_run_transfer_unwind(KosContext *ctx, KosDynNode *node,
                                           KosEvent *out_ev) {
  int r = kos_dyn_unwind(ctx, node, out_ev);
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
int kos_dyn_install(KosContext *ctx, KosDynNode *node, KosEvent *out_ev) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT: {
      if (! KosContext_FRAME_TOP(ctx))
        return 0;
      if (KOS_XFER_TRACE) {
        size_t len_before = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
            ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
        SAYF("[xfer] prompt_install node=%p: pushing stack block, len before=%zu",
             (void *) node, len_before);
        (void) len_before;
      }
      int result = KosStackBlock_PushBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
      if (KOS_XFER_TRACE && result == 0) {
        size_t len_after = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
            ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
        SAYF("[xfer] prompt_install node=%p done, len after=%zu", (void *) node, len_after);
        (void) len_after;
      }
      return result;
    }
    case KOS_DYN_PARAM:
      return kos_dynparam_enter(ctx, (KosDynParam *) node);
    case KOS_DYN_DYN_WITH:
      return kos_dyn_dyn_with_wind(ctx, (KosDynWith *) node, out_ev);
    case KOS_DYN_COLLECT:
      /* PUSH_COLLECT handler installs collector as frame ns before pushing
         the node; no install action needed. */
      return 0;
    default:
      return 0;
  }
}


/**
   Run the pop handler for a node being popped off the chain.  Types with
   no pop action return 0 immediately via the default case.

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
static int kos_dyn_uninstall_handler(KosContext *ctx, KosDynNode *node, KosEvent *out_ev) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT: {
      if (! KosContext_FRAME_TOP(ctx))
        return 0;
      if (KOS_XFER_TRACE) {
        size_t len_before = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
            ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
        SAYF("[xfer] prompt_pop node=%p: dropping stack block, len before=%zu",
             (void *) node, len_before);
        (void) len_before;
      }
      int result = KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
      if (KOS_XFER_TRACE && result == 0) {
        size_t len_after = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
            ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
        SAYF("[xfer] prompt_pop node=%p done, len after=%zu", (void *) node, len_after);
        (void) len_after;
      }
      return result;
    }
    case KOS_DYN_WIND:
      return kos_dyn_unwind(ctx, node, out_ev);
    case KOS_DYN_TRY:
    case KOS_DYN_FINALLY:
    case KOS_DYN_LOOP:
    case KOS_DYN_FOREACH:
      (void) out_ev;
      if (KosContext_FRAME_TOP(ctx))
        KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
      return 0;
    case KOS_DYN_NS:
      (void) out_ev;
      kos_dyn_ns_unwind(ctx, (KosDynNs *) node);
      return 0;
    case KOS_DYN_COLLECT: {
      (void) out_ev;
      if (KosContext_FRAME_TOP(ctx)
          && KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0)
        return -1;
      kos_dyn_collect_unwind(ctx, (KosDynCollect *) node);
      return 0;
    }
    case KOS_DYN_PARAM:
      (void) out_ev;
      kos_dyn_param_unwind(ctx, (KosDynParam *) node);
      return 0;
    case KOS_DYN_YIELD_FROM:
      (void) out_ev;
      kos_dyn_yieldfrom_unwind(ctx, (KosDynYieldFrom *) node);
      return 0;
    case KOS_DYN_WITH:
    case KOS_DYN_DYN_WITH: {
      KosDynWith *with_node = (KosDynWith *) node;
      int unw = kos_dyn_unwind(ctx, node, out_ev);
      if (unw == KOS_DYN_UNWIND_ASYNC_STARTED)
        return KOS_DYN_UNWIND_ASYNC_STARTED;
      if (unw == KOS_DYN_UNWIND_SWALLOWED)
        return 0;
      if (unw < 0)
        return -1;
      if (! PyErr_Occurred()) {
        KosDynFrame *frame = KosContext_FRAME_TOP(ctx);
        if (frame)
          KosDynFrame_PC(frame) = KosDynWith_RESUME_PC(with_node);
      }
      if (KosContext_FRAME_TOP(ctx)) {
        KosStackBlock **stack = &KosDynFrame_STACK(KosContext_FRAME_TOP(ctx));
        PyObject *body_val = KosStackBlock_PopRef(stack);
        if (! body_val)
          return -1;
        if (KosStackBlock_DropBlock(stack) < 0) {
          Py_DECREF(body_val);
          return -1;
        }
        if (! PyErr_Occurred() &&
            KosStackBlock_PushRef(stack, body_val) < 0) {
          Py_DECREF(body_val);
          return -1;
        }
        Py_DECREF(body_val);
      }
      return 0;
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
int kos_dyn_wind(KosContext *ctx, KosDynNode *node, KosEvent *out_ev) {
  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PARAM:
      return kos_dyn_param_wind(ctx, (KosDynParam *) node);
    case KOS_DYN_DYN_WITH:
      return kos_dyn_dyn_with_wind(ctx, (KosDynWith *) node, out_ev);
    case KOS_DYN_WIND:
      return kos_dyn_wind_wind(ctx, (KosDynWind *) node);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_wind: node kind %u has no wind",
                   (unsigned) KosDynNode_KIND(node));
      return -1;
  }
}


/**
   Run unwind (exit) for an extent node.  May start async for WITH/DYN_WITH.

   :param out_ev: event output; may be NULL for nodes that do not suspend
   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED when async
*/
int kos_dyn_unwind(
    KosContext *ctx,
    KosDynNode *node,
    KosEvent *out_ev) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_SHIFT:
      (void) out_ev;
      return 0;
    case KOS_DYN_PARAM:
      (void) out_ev;
      kos_dyn_param_unwind(ctx, (KosDynParam *) node);
      return 0;
    case KOS_DYN_WIND:
      (void) out_ev;
      return kos_dyn_wind_unwind(ctx, (KosDynWind *) node);
    case KOS_DYN_WITH:
    case KOS_DYN_DYN_WITH:
      return kos_dyn_with_unwind(ctx, (KosDynWith *) node, out_ev);
    case KOS_DYN_CC_RAISE:
      (void) out_ev;
      return cc_raise_unwind(ctx, node, NULL);
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dyn_unwind: node kind %u has no unwind",
                   (unsigned) KosDynNode_KIND(node));
      return -1;
  }
}


/**
   Called when exception propagation reaches a HANDLES_EXCEPTIONS node
   that refused to pop normally.  Returns 0 to continue unwinding, 1 if
   the exception was swallowed and execution should resume,
   KOS_DYN_UNWIND_ASYNC_STARTED if async, or -1 on error.
*/
int kos_dyn_exception_unwind(
    KosContext *ctx,
    KosDynNode *node,
    KosEvent *out_ev) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT:
      (void) out_ev;
      if (KOS_XFER_TRACE)
        SAYF("[xfer] prompt_unwind_exception node=%p: unlinking prompt", (void *) node);
      /* kos_dyn_pop_prompt runs the PROMPT pop (drops the stack block),
         then unlinks and frees the node. */
      kos_dyn_pop_prompt(ctx, (KosDynPrompt *) node);
      /* Exception propagates past the prompt boundary; continue unwinding */
      return 0;
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
      return (KosDynNode *) kos_dynwith_clone((KosDynWith *) node);
    case KOS_DYN_DYN_WITH:
      return (KosDynNode *) kos_dyndynwith_clone((KosDynWith *) node);
    case KOS_DYN_SHIFT:
      return (KosDynNode *) kos_dynshift_clone((KosDynShift *) node);
    case KOS_DYN_CC_RAISE:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot clone CC_RAISE sentinel node");
      return NULL;
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
int kos_dyn_pop_top(KosContext *ctx, KosEvent *out_ev) {

  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return -1;
  }

  if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return -1;
  }

  return kos_dyn_uninstall(ctx, top, out_ev);
}


int kos_dyn_unwind_to_node(KosContext *ctx, KosDynNode *target,
                           KosEvent *out_ev) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: target=%p kind=%s",
         (void *) target, target ? kos_dynnode_kind_name(KosDynNode_KIND(target)) : "NULL");

  while (KosContext_DYN_TOP(ctx) != NULL
         && KosContext_DYN_TOP(ctx) != target) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_node: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    int pop_res;
    if (KosDynNode_IS_PROMPT(top)) {
      kos_dyn_pop_prompt(ctx, (KosDynPrompt *) top);
      pop_res = 0;
    } else {
      pop_res = kos_dyn_pop_top(ctx, out_ev);
      if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return pop_res;
    }
    if (pop_res != 0)
      return -1;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: done, dyn_top=%p", (void *) KosContext_DYN_TOP(ctx));
  return 0;
}


int kos_dyn_unwind_to_frame(KosContext *ctx, KosDynFrame *frame,
                            KosEvent *out_ev) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: target frame=%p", (void *) frame);

  while (KosContext_DYN_TOP(ctx) != NULL
         && KosContext_DYN_TOP(ctx) != (KosDynNode *) frame) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_frame: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    int pop_res;
    if (KosDynNode_IS_PROMPT(top)) {
      kos_dyn_pop_prompt(ctx, (KosDynPrompt *) top);
      pop_res = 0;
    } else {
      pop_res = kos_dyn_pop_top(ctx, out_ev);
      if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
        KosContext_UNWIND_TARGET_FRAME(ctx) = frame;
        return pop_res;
      }
    }
    if (pop_res != 0)
      return -1;
  }
  KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: done, dyn_top=%p", (void *) KosContext_DYN_TOP(ctx));
  return 0;
}


KosDynNode *kos_find_node_for_raise(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);
  while (node) {
    if ((KosDynNode_IS_TRY(node) && KosDynTry_ENABLED((KosDynTry *) node)) ||
        (KosDynNode_IS_FINALLY(node) && KosDynFinally_ENABLED((KosDynFinally *) node)))
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


KosDynNode *kos_find_node_for_loop(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);
  while (node) {
    if (KosDynNode_IS_LOOP(node) || KosDynNode_IS_FOREACH(node))
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
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


int kos_shift_capture(
    KosContext *ctx,
    uint32_t prompt_id,
    uint32_t resume_pc,
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
   * the frames below it (ns, frame).  Include the frame so resume_frame is
   * found and the push segment contains a frame for kos_update_frame_top.
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
  if (! resume_frame) {
    kos_dynchain_free(captured_chain, NULL);
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_SHIFT: no frame in captured chain");
    return -1;
  }

  KosStackBlock *resume_stack = resume_frame
      ? KosStackBlock_COW(KosDynFrame_STACK(resume_frame)) : NULL;
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


static KosDynNode *kos_find_node_by_id(KosDynNode *chain, uint32_t id) {
  KosDynNode *node = chain;
  while (node) {
    if (KosDynNode_NODE_ID(node) == id)
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


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
    KosContext *ctx,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
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
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_unwind_actions(plan, &idx, current, lca);
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0)
    return -1;
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
    KosContext *ctx,
    KosDynNode *segment,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosTransferPlan *plan = kos_transfer_plan_alloc(2);  /* INSTALL + INJECT */
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = 2;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = segment;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
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
    KosContext *ctx,
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
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = segment;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  if (xfer_plan_fill_wind_actions(plan, &idx, segment, NULL, wind_count) < 0)
    return -1;
  xfer_plan_fill_push_action(plan, &idx, segment);
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


/**
   Build a transfer plan that skips all unwind actions (direct install).
   Use when the path from current to target crosses a barrier (e.g. PROMPT)
   that kos_dyn_pop_top cannot traverse.  Plan has: INSTALL, wind before
   hooks (if any), INJECT.  Caller must set RESUME_PC, RESUME_FRAME,
   RESUME_STACK on the plan after this returns.
   Returns 0 on success, -1 on error.
*/
int kos_build_direct_install_plan(
    KosContext *ctx,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
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
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0)
    return -1;
  xfer_plan_fill_inject_action(plan, &idx, value, is_throw ? (PyObject *) Py_TYPE(value) : NULL, is_throw);

  *out_plan = plan;
  return 0;
}


int kos_build_cc_unwind_plan(
    KosContext *ctx,
    PyObject *exc_type,
    PyObject *exc_val,
    KosTransferPlan **out_plan) {

  /* Insert the sentinel at the bottom (root) of the chain so that the
     transfer plan unwinds all existing WITH/finally nodes first and hits the
     KosDynCCRaise re-raise step last.  We walk to the current chain root and
     attach the new node below it. */
  KosDynCCRaise *raise_node = kos_dyn_cc_raise_new(NULL, exc_type, exc_val);
  if (! raise_node)
    return -1;

  KosDynNode *cur = KosContext_DYN_TOP(ctx);
  if (! cur) {
    /* Empty chain: the sentinel becomes dyn_top directly. */
    KosContext_DYN_TOP(ctx) = (KosDynNode *) raise_node;
    KosContext_CHAIN_VERSION(ctx)++;
  } else {
    /* Walk to the bottom of the chain and attach below it. */
    while (KosDynNode_PARENT(cur))
      cur = KosDynNode_PARENT(cur);
    KosDynNode_PARENT(cur) = (KosDynNode *) raise_node;
    KosContext_CHAIN_VERSION(ctx)++;
  }

  /* Build a plan that unwinds from dyn_top all the way to NULL, running
     WITH/finally teardowns along the way.  kos_build_transfer_plan with
     target=NULL treats lca=NULL, so it emits EXTENT_ACTION steps for every
     node that has NEEDS_UNWIND_ACTION (including our sentinel at the bottom),
     then INSTALL(NULL), then INJECT.  When the plan reaches the KosDynCCRaise
     EXTENT_ACTION, cc_raise_unwind sets PyErr and returns -1, which causes
     run_transfer_plan_to_completion to package it as KOS_EV_RAISE. */
  return kos_build_transfer_plan(ctx, NULL, Py_None, 0, 0, out_plan);
}


int kos_dyn_build_pop_shift_plan(KosContext *ctx, KosDynPrompt *prompt,
                                 PyObject *value, KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  size_t unwind_count = xfer_plan_count_unwind_and_params(current, prompt_node);
  size_t action_count = unwind_count + 1;  /* TRUNCATE_AND_INJECT */

  KosTransferPlan *plan = kos_transfer_plan_alloc(action_count);
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = prompt_node;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
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


int kos_dyn_build_push_shift_unwind_plan(KosContext *ctx, KosDynPrompt *prompt,
                                         KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
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
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = pop_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = prompt_node;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
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
    KosContext *ctx,
    KosDynNode *target,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosContext_DYN_TOP(ctx);
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
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = target;
  KosTransferPlan_CHAIN_VERSION(plan) = KosContext_CHAIN_VERSION(ctx);
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  size_t idx = 0;
  xfer_plan_fill_install_action(plan, &idx, target);
  if (xfer_plan_fill_wind_actions(plan, &idx, target, lca, wind_count) < 0)
    return -1;

  *out_plan = plan;
  return 0;
}


/**
   Release the multi-shot continuation clone held by the plan, if any.
   The clone was used for a re-invocation; we release its resume_stack
   with exclude=continuation_ref so we do not DECREF the original
   continuation (and free the captured chain) while the plan may still
   reference them.  Then we either return the clone to the continuation
   freelist or PyMem_Free it.  Called from kos_free_transfer_plan.
*/
static void xfer_plan_release_invoke_clone(KosTransferPlan *plan) {
  if (! KosTransferPlan_INVOKE_CLONE(plan))
    return;

  KosContinuation *clone = KosTransferPlan_INVOKE_CLONE(plan);
  KosTransferPlan_INVOKE_CLONE(plan) = NULL;
  if (KosContinuation_RESUME_STACK(clone)) {
    KosStackBlock_ReleaseChainExclude(KosContinuation_RESUME_STACK(clone),
                                      KosTransferPlan_CONTINUATION_REF(plan));
    KosContinuation_RESUME_STACK(clone) = NULL;
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
    } else if (KosTransferAction_STEP(a) == KOS_XFER_TRUNCATE_AND_INJECT) {
      Py_XDECREF(KosTransferAction_TRUNCATE_VALUE(a));
    }
  }
  xfer_plan_release_invoke_clone(plan);
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
static int xfer_step_extent_action(KosContext *ctx, KosTransferAction *action,
                                   KosEvent *out_ev) {
  KosDynNode *node = KosTransferAction_EXTENT_NODE(action);
  int phase = KosTransferAction_EXTENT_PHASE(action);

  if (phase == KOS_XFER_EXTENT_PHASE_WIND)
    return kos_dyn_wind(ctx, node, out_ev);
  return kos_dynnode_run_transfer_unwind(ctx, node, out_ev);
}


#define KOS_STACK_BUF_SIZE 8


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
static int xfer_step_install(KosContext *ctx, KosTransferAction *action,
                             KosEvent *out_ev) {
  KosTransferPlan *plan = KosContext_PLAN(ctx);
  KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
  KosDynNode *template_bottom = template_seg;
  while (template_bottom && KosDynNode_PARENT(template_bottom))
    template_bottom = KosDynNode_PARENT(template_bottom);
  KosDynNode *stop = template_bottom ? KosDynNode_PARENT(template_bottom) : NULL;
  KosDynNode *seg = kos_clone_chain_segment(template_seg, stop);
  if (! seg && template_seg) {
    if (KOS_XFER_TRACE)
      SAY("[xfer] INSTALL clone segment failed");
    KosTransferPlan_ACTION_INDEX(plan)--;  /* leave step for retry or caller */
    return -1;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] INSTALL segment=%p (fresh) resume_pc=%u (before: dyn_top=%p frame_top=%p)",
         (void *) seg, (unsigned) KosTransferPlan_RESUME_PC(plan),
         (void *) KosContext_DYN_TOP(ctx), (void *) KosContext_FRAME_TOP(ctx));
  {
    KosDynNode *old_top = KosContext_DYN_TOP(ctx);
    KosContext_DYN_TOP(ctx) = seg;
    kos_update_frame_top(ctx);
    if (old_top)
      kos_dynchain_free(old_top, NULL);
  }
  KosContext_GLOBALS_FRAME(ctx) = KosContext_FRAME_TOP(ctx);
  KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx) + 1;
  if (KosContext_FRAME_TOP(ctx) && KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock *new_stack = KosStackBlock_COW(KosTransferPlan_RESUME_STACK(plan));
    KosStackBlock *old_stack = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx));
    KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)) = new_stack;
    if (old_stack && old_stack != new_stack) {
      KosStackBlock_ReleaseChainExclude(old_stack,
                                        KosTransferPlan_CONTINUATION_REF(plan));
    }
  }
  if (KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1 && KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosTransferPlan_RESUME_PC(plan);
  KosContext_CHAIN_VERSION(ctx)++;
  if (KOS_XFER_TRACE && KosContext_FRAME_TOP(ctx))
    SAYF("[xfer] INSTALL done: frame_top=%p stack=%p stack_len=%zu resume_pc=%u",
         (void *) KosContext_FRAME_TOP(ctx),
         (void *) KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
         KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
             ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0,
         (unsigned) KosTransferPlan_RESUME_PC(plan));
  KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx);
  /* Run install wind for nodes that need it (in reverse order) */
  {
    /* First pass: count nodes that need wind */
    size_t wind_count = 0;
    KosDynNode *walk = seg;
    KosDynNode *chain_bottom = seg;
    while (chain_bottom && KosDynNode_PARENT(chain_bottom))
      chain_bottom = KosDynNode_PARENT(chain_bottom);
    KosDynNode *chain_stop = chain_bottom ? KosDynNode_PARENT(chain_bottom) : NULL;
    while (walk && walk != chain_stop) {
      if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND))
        wind_count++;
      walk = KosDynNode_PARENT(walk);
    }

    /* Second pass: collect nodes into C array (stack buffer for common case) */
    if (wind_count > 0) {
      KosDynNode *stack_buf[KOS_STACK_BUF_SIZE];
      KosDynNode **wind_nodes;
      if (wind_count <= KOS_STACK_BUF_SIZE) {
        wind_nodes = stack_buf;
      } else {
        wind_nodes = PyMem_Malloc(wind_count * sizeof(KosDynNode *));
        if (! wind_nodes) {
          PyErr_NoMemory();
          return -1;
        }
      }
      size_t idx = 0;
      walk = seg;
      while (walk && walk != chain_stop) {
        if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND))
          wind_nodes[idx++] = walk;
        walk = KosDynNode_PARENT(walk);
      }

      /* Third pass: run wind in reverse order */
      for (size_t i = wind_count; i > 0; i--) {
        int r = kos_dynnode_run_wind(ctx, wind_nodes[i - 1], out_ev);
        if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          return KOS_DYN_UNWIND_ASYNC_STARTED;
        }
        if (r < 0) {
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          if (KOS_XFER_TRACE)
            SAY("[xfer] INSTALL wind failed");
          return -1;
        }
      }
      if (wind_nodes != stack_buf)
        PyMem_Free(wind_nodes);
    }
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] INSTALL after: dyn_top=%p frame_top=%p",
         (void *) KosContext_DYN_TOP(ctx), (void *) KosContext_FRAME_TOP(ctx));
  return 0;
}


/**
   Execute a PUSH step: append a clone of the segment to the current chain,
   grafting the segment bottom to dyn_top.  Does not free the current chain.
   Restores stack/PC from plan and runs param wind for pushed nodes.
   Returns 0 on success, -1 on error,
   KOS_DYN_UNWIND_ASYNC_STARTED when async wind started.
*/
static int xfer_step_push(KosContext *ctx, KosTransferAction *action,
                          KosEvent *out_ev) {
  KosTransferPlan *plan = KosContext_PLAN(ctx);
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
         (void *) seg, (void *) KosContext_DYN_TOP(ctx),
         (void *) KosContext_FRAME_TOP(ctx));

  /* Graft bottom of segment to current dyn_top */
  KosDynNode *current_top = KosContext_DYN_TOP(ctx);
  KosDynNode *seg_bottom = seg;
  while (seg_bottom && KosDynNode_PARENT(seg_bottom))
    seg_bottom = KosDynNode_PARENT(seg_bottom);
  if (seg_bottom)
    KosDynNode_PARENT(seg_bottom) = current_top;

  KosContext_DYN_TOP(ctx) = seg;
  kos_update_frame_top(ctx);
  KosContext_GLOBALS_FRAME(ctx) = KosContext_FRAME_TOP(ctx);
  KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx) + 1;

  if (KosContext_FRAME_TOP(ctx) && KosTransferPlan_RESUME_STACK(plan)) {
    KosStackBlock *new_stack =
        KosStackBlock_COW(KosTransferPlan_RESUME_STACK(plan));
    KosStackBlock *old_stack =
        KosDynFrame_STACK(KosContext_FRAME_TOP(ctx));
    KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)) = new_stack;
    if (old_stack && old_stack != new_stack) {
      KosStackBlock_ReleaseChainExclude(old_stack,
                                        KosTransferPlan_CONTINUATION_REF(plan));
    }
  }
  if (KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) =
        KosTransferPlan_RESUME_PC(plan);
  KosContext_CHAIN_VERSION(ctx)++;

  /* Run install wind for nodes that need it (in reverse order) */
  if (seg) {
    /* First pass: count nodes that need wind */
    size_t wind_count = 0;
    KosDynNode *walk = seg;
    KosDynNode *chain_stop = current_top;
    while (walk && walk != chain_stop) {
      if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND))
        wind_count++;
      walk = KosDynNode_PARENT(walk);
    }

    /* Second pass: collect nodes into C array (stack buffer for common case) */
    if (wind_count > 0) {
      KosDynNode *stack_buf[KOS_STACK_BUF_SIZE];
      KosDynNode **wind_nodes;
      if (wind_count <= KOS_STACK_BUF_SIZE) {
        wind_nodes = stack_buf;
      } else {
        wind_nodes = PyMem_Malloc(wind_count * sizeof(KosDynNode *));
        if (! wind_nodes) {
          PyErr_NoMemory();
          return -1;
        }
      }
      size_t idx = 0;
      walk = seg;
      while (walk && walk != chain_stop) {
        if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND))
          wind_nodes[idx++] = walk;
        walk = KosDynNode_PARENT(walk);
      }

      /* Third pass: run wind in reverse order */
      for (size_t i = wind_count; i > 0; i--) {
        int r = kos_dynnode_run_wind(ctx, wind_nodes[i - 1], out_ev);
        if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          return KOS_DYN_UNWIND_ASYNC_STARTED;
        }
        if (r < 0) {
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          if (KOS_XFER_TRACE)
            SAY("[xfer] PUSH wind failed");
          return -1;
        }
      }
      if (wind_nodes != stack_buf)
        PyMem_Free(wind_nodes);
    }
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] PUSH after: dyn_top=%p frame_top=%p",
         (void *) KosContext_DYN_TOP(ctx), (void *) KosContext_FRAME_TOP(ctx));
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
static int xfer_step_inject(KosContext *ctx, KosTransferAction *action) {
  if (KOS_XFER_TRACE)
    SAYF("[xfer] INJECT frame_top=%p stack=%p stack_len=%zu is_throw=%d",
         (void *) KosContext_FRAME_TOP(ctx),
         KosContext_FRAME_TOP(ctx) ? (void *) KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)) : NULL,
         (KosContext_FRAME_TOP(ctx) && KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)))
             ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0,
         KosTransferAction_INJECT_IS_THROW(action));
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
    if (KosContext_FRAME_TOP(ctx) &&
        KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                              KosTransferAction_INJECT_VALUE(action)) < 0) {
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
static int xfer_step_pop_one(KosContext *ctx, KosEvent *out_ev) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "POP_ONE: empty chain");
    return -1;
  }
  if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_ONE: cannot pop barrier node");
    return -1;
  }
  return kos_dyn_uninstall(ctx, top, out_ev);
}


/**
   Execute a TRUNCATE_AND_INJECT step: free from current top through the
   given node (inclusive), set dyn_top to node's parent, then inject value.
   If the plan has RESUME_PC set, updates the new top frame's PC.
*/
static int xfer_step_truncate_and_inject(KosContext *ctx,
                                          KosTransferAction *action) {
  KosTransferPlan *plan = KosContext_PLAN(ctx);
  KosDynNode *stop_node = KosTransferAction_TRUNCATE_NODE(action);
  KosDynNode *new_top = stop_node ? KosDynNode_PARENT(stop_node) : NULL;
  KosDynNode *current = KosContext_DYN_TOP(ctx);

  if (! current) {
    PyErr_SetString(PyExc_RuntimeError, "TRUNCATE_AND_INJECT: empty chain");
    return -1;
  }

  if (KOS_XFER_TRACE)
    SAYF("[xfer] TRUNCATE_AND_INJECT: freeing from current=%p to stop_node=%p (inclusive), new_top will be=%p",
         (void *) current, (void *) stop_node, (void *) new_top);

  /* Teardown PROMPT and DYN_WITH nodes before freeing: drop stack blocks.
   * The flatten removed NEEDS_TRANSFER_TEARDOWN but TRUNCATE does not run
   * uninstall handlers; we must drop blocks here so the frame's stack shape
   * is correct when we inject the value onto new_top's frame. */
  {
    KosDynNode *node = current;
    while (node && node != new_top) {
      KosDynNode *parent = KosDynNode_PARENT(node);
      if (KosDynNode_IS_PROMPT(node) || KosDynNode_IS_DYN_WITH(node)) {
        if (KosContext_FRAME_TOP(ctx)
            && KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0)
          return -1;
      }
      node = parent;
    }
  }

  kos_dynchain_free(current, new_top);
  KosContext_DYN_TOP(ctx) = new_top;
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);

  if (plan && KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosContext_FRAME_TOP(ctx)) {
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosTransferPlan_RESUME_PC(plan);
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
  } else if (KosContext_FRAME_TOP(ctx)) {
    size_t stack_len_before = 0;
    if (KOS_XFER_TRACE) {
      stack_len_before = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
          ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
      SAYF("[xfer] TRUNCATE_AND_INJECT: pushing value %R onto frame=%p stack (len before=%zu)",
           KosTransferAction_TRUNCATE_VALUE(action),
           (void *) KosContext_FRAME_TOP(ctx), stack_len_before);
    }
    (void) stack_len_before;
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                              KosTransferAction_TRUNCATE_VALUE(action)) < 0) {
      Py_DECREF(KosTransferAction_TRUNCATE_VALUE(action));
      return -1;
    }
    if (KOS_XFER_TRACE) {
      size_t stack_len_after = KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))
          ? KosStackBlock_LEN(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) : 0;
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
int kos_run_transfer_step(KosContext *ctx, KosEvent *out_ev) {
  if (! KosContext_PLAN(ctx))
    return 1;  /* no plan, complete */

  if (KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)) >= KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)))
    return 1;  /* plan complete */

  KosTransferAction *action = &KosTransferPlan_ACTIONS(KosContext_PLAN(ctx))[KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx))];
  if (KOS_XFER_TRACE)
    SAYF("[xfer] step %zu: plan=%p actions=%p step_type=%d (INSTALL=%d)",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (void *) KosContext_PLAN(ctx), (void *) KosTransferPlan_ACTIONS(KosContext_PLAN(ctx)),
         (int) KosTransferAction_STEP(action), (int) KOS_XFER_INSTALL);

  switch (KosTransferAction_STEP(action)) {
    case KOS_XFER_EXTENT_ACTION: {
      int res = xfer_step_extent_action(ctx, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return KOS_XFER_SUSPENDED_AWAIT;
      if (res < 0)
        return -1;
      break;
    }

    case KOS_XFER_INSTALL: {
      int res = xfer_step_install(ctx, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return KOS_XFER_SUSPENDED_AWAIT;
      if (res < 0)
        return -1;
      break;
    }

    case KOS_XFER_PUSH: {
      int res = xfer_step_push(ctx, action, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return KOS_XFER_SUSPENDED_AWAIT;
      if (res < 0)
        return -1;
      break;
    }

    case KOS_XFER_POP_ONE: {
      int res = xfer_step_pop_one(ctx, out_ev);
      if (res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return KOS_XFER_SUSPENDED_AWAIT;
      if (res < 0)
        return -1;
      break;
    }

    case KOS_XFER_TRUNCATE_AND_INJECT: {
      int res = xfer_step_truncate_and_inject(ctx, action);
      if (res < 0)
        return -1;
      break;
    }

    case KOS_XFER_INJECT: {
      int res = xfer_step_inject(ctx, action);
      if (res < 0)
        return -1;
      break;
    }
  }

  /* Advance only when step completed normally (no early return -1 or KOS_XFER_SUSPENDED_AWAIT). */
  KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx))++;
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

  /* Delimited k(value): use PUSH so caller stays on chain; return path
   * unwinds to DELIMITED_RETURN_FRAME when continuation hits POP_PROMPT. */
  int is_delimited = (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);
  KosDynNode *push_segment = NULL;
  if (is_delimited) {
    KosDynNode *captured = KosContinuation_CAPTURED_CHAIN(to_use);
    /* Segment to push: frames below the prompt (exclude prompt from push) */
    push_segment = captured ? KosDynNode_PARENT(captured) : NULL;
    KosContext_DELIMITED_RETURN_FRAME(ctx) = KosContext_FRAME_TOP(ctx);
    KosContext_DELIMITED_RETURN_PC(ctx) = KosContext_FRAME_TOP(ctx)
        ? KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) : (uint32_t) -1;
    /* Return to current dyn_top, not frame_top, so we don't unwind past
       nodes that were pushed after the continuation was invoked (e.g. PARAM) */
    KosContext_DELIMITED_RETURN_NODE(ctx) = KosContext_DYN_TOP(ctx);
    if (KOS_XFER_TRACE)
      SAYF("[xfer] invoke: set DELIMITED_RETURN dyn_top=%p kind=%s frame_top=%p return_node=%p",
           (void *) KosContext_DYN_TOP(ctx),
           KosContext_DYN_TOP(ctx) ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx))) : "NULL",
           (void *) KosContext_FRAME_TOP(ctx),
           (void *) KosContext_DELIMITED_RETURN_NODE(ctx));
    if (KosContext_RETURN_TARGET_CHAIN(ctx)) {
      kos_dynchain_free(KosContext_RETURN_TARGET_CHAIN(ctx), NULL);
      KosContext_RETURN_TARGET_CHAIN(ctx) = NULL;
    }
    KosContext_RETURN_TARGET_PC(ctx) = (uint32_t) -1;
  }

  /* Build plan: PUSH for delimited; full transfer for undelimited. */
  KosTransferPlan *plan = NULL;
  int res;
  if (is_delimited) {
    if (KOS_XFER_TRACE)
      SAYF("[xfer] kos_build_delimited_push_plan (delimited) segment=%p",
           (void *) push_segment);
    res = kos_build_delimited_push_plan(ctx, push_segment, value, is_throw,
                                        &plan);
  } else {
    if (KOS_XFER_TRACE)
      SAYF("[xfer] kos_build_transfer_plan current=%p target=%p (template)",
           (void *) KosContext_DYN_TOP(ctx), (void *) to_use->captured_chain);
    res = kos_build_transfer_plan(ctx, to_use->captured_chain,
                                  value, is_throw,
                                  KosContinuation_GRAFT_POINT_ID(to_use),
                                  &plan);
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan build -> res=%d plan=%p", res, (void *) plan);
  if (res < 0) {
    if (is_delimited) {
      KosContext_DELIMITED_RETURN_FRAME(ctx) = NULL;
      KosContext_DELIMITED_RETURN_PC(ctx) = (uint32_t) -1;
      KosContext_DELIMITED_RETURN_NODE(ctx) = NULL;
    }
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

  /* Plan replacement: one active plan at a time; discard any existing plan */
  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
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


/**
   GC traverse a KOS_DYN_FRAME: visit the frame's namespace and every
   object on the frame's stack block chain.  Used by kos_dynnode_traverse.
*/
static int dynnode_traverse_frame(KosDynNode *node, visitproc visit, void *arg) {
  KosDynFrame *frame = (KosDynFrame *) node;
  Py_VISIT(KosDynFrame_NS(frame));
  KosStackBlock *b = KosDynFrame_STACK(frame);
  while (b) {
    for (size_t i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = KosStackBlock_PARENT(b);
  }
  return 0;
}


/**
   GC traverse a KOS_DYN_WIND: visit the before and after thunk callables,
   and the stored body result.
*/
static int dynnode_traverse_wind(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWind *wind = (KosDynWind *) node;
  Py_VISIT(KosDynWind_BEFORE_THUNK(wind));
  Py_VISIT(KosDynWind_AFTER_THUNK(wind));
  Py_VISIT(KosDynWind_BODY_RESULT(wind));
  return 0;
}


/**
   GC traverse a KOS_DYN_NS: visit the saved namespace reference.
*/
static int dynnode_traverse_ns(KosDynNode *node, visitproc visit, void *arg) {
  KosDynNs *ns_node = (KosDynNs *) node;
  Py_VISIT(KosDynNs_SAVED_NS(ns_node));
  return 0;
}


/**
   GC traverse a TRY/FINALLY/LOOP/FOREACH node payload.
*/
static int dynnode_traverse_region(KosDynNode *node, visitproc visit, void *arg) {
  if (KosDynNode_IS_FINALLY(node))
    Py_VISIT(KosDynFinally_HANDLING_RESULT((KosDynFinally *) node));
  else if (KosDynNode_IS_LOOP(node))
    Py_VISIT(KosDynLoop_RESULT((KosDynLoop *) node));
  else if (KosDynNode_IS_FOREACH(node)) {
    Py_VISIT(KosDynForeach_RESULT((KosDynForeach *) node));
    Py_VISIT(KosDynForeach_ITER((KosDynForeach *) node));
  }
  return 0;
}


/**
   GC traverse a KOS_DYN_YIELD_FROM: visit the delegate subiterator and
   any pending or in-flight value references.
*/
static int dynnode_traverse_yield_from(KosDynNode *node, visitproc visit, void *arg) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) node;
  Py_VISIT(KosDynYieldFrom_DELEGATE(yf));
  Py_VISIT(KosDynYieldFrom_PENDING_ARG(yf));
  Py_VISIT(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
  Py_VISIT(KosDynYieldFrom_INFLIGHT(yf));
  return 0;
}


/**
   GC traverse a KOS_DYN_PARAM: visit the parameter bindings dict and
   the saved bindings from outer scopes.
*/
static int dynnode_traverse_param(KosDynNode *node, visitproc visit, void *arg) {
  KosDynParam *param_node = (KosDynParam *) node;
  Py_VISIT(KosDynParam_BINDINGS(param_node));
  Py_VISIT(KosDynParam_SAVED(param_node));
  return 0;
}


/**
   GC traverse a KOS_DYN_WITH: visit mgr, enter, exit, and name.
*/
static int dynnode_traverse_with(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWith *with_node = (KosDynWith *) node;
  Py_VISIT(KosDynWith_MGR(with_node));
  Py_VISIT(KosDynWith_ENTER(with_node));
  Py_VISIT(KosDynWith_EXIT(with_node));
  Py_VISIT(KosDynWith_NAME(with_node));
  if (KosDynWith_PHASE(with_node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    Py_VISIT(KosDynWith_EXC_TYP(with_node));
    Py_VISIT(KosDynWith_EXC_VAL(with_node));
    Py_VISIT(KosDynWith_EXC_TB(with_node));
  }
  return 0;
}


/**
   GC traverse a single dynamic chain node.  Dispatches on node kind to
   visit all PyObject references reachable from the node (namespaces,
   stack slots, region state, param bindings, etc.).  Prompt, CC, and
   wind nodes hold no such references and are no-ops.  Used by
   kos_dynchain_traverse and by continuation/plan traversals.
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
      Py_VISIT(KosDynPrompt_PENDING_SHIFT_WRAPPER((KosDynPrompt *) node));
      break;
    case KOS_DYN_CC:
    case KOS_DYN_SHIFT:
      break;
    case KOS_DYN_WIND:
      return dynnode_traverse_wind(node, visit, arg);
    case KOS_DYN_CC_RAISE:
      Py_VISIT(KosDynCCRaise_EXC_VAL(node));
      break;
    case KOS_DYN_TRY:
    case KOS_DYN_FINALLY:
    case KOS_DYN_LOOP:
    case KOS_DYN_FOREACH:
      return dynnode_traverse_region(node, visit, arg);
    case KOS_DYN_YIELD_FROM:
      return dynnode_traverse_yield_from(node, visit, arg);
    case KOS_DYN_PARAM:
      return dynnode_traverse_param(node, visit, arg);
    case KOS_DYN_WITH:
    case KOS_DYN_DYN_WITH:
      return dynnode_traverse_with(node, visit, arg);
    case KOS_DYN_COLLECT:
      Py_VISIT(KosDynCollect_SAVED_NS((KosDynCollect *) node));
      break;
    default:
      PyErr_Format(PyExc_RuntimeError,
                   "kos_dynnode_traverse: unknown node kind %u",
                   (unsigned) KosDynNode_KIND(node));
      return -1;
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
  Py_VISIT(KosTransferPlan_STEP_AWAITABLE(plan));
  Py_VISIT(KosTransferPlan_STEP_RESUME_INJECT(plan));

  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    if (KosTransferPlan_ACTIONS(plan)[i].step == KOS_XFER_INJECT) {
      Py_VISIT(KosTransferPlan_ACTIONS(plan)[i].data.inject.value);
    }
  }

  return 0;
}


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod) {
  Py_CLEAR(_param_sentinel_unset);
  _param_sentinel_unset = Py_BuildValue("(s)", "<param was unset>");
  if (! _param_sentinel_unset)
    return -1;
  return 0;
}


int koskari_dynchain_clear(PyObject *mod) {
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
