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
   koskari/lib/eval.c

   Evaluator core implementation: delimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   This version uses the DynNode chain infrastructure from dynchain.h
   for unified dynamic extent tracking.

   Invariants:

   1. Context ownership: A KosContext is owned by exactly one Python
      wrapper (KosCoroutine or KosPyAsyncGen) during its lifetime.

   2. Chain ownership: The dynamic chain is owned by the context.
      On continuation capture, chains are cloned for multi-shot semantics.

   3. Suspension: When the VM suspends (AWAIT), the context's py_await_iter
      holds the iterator. Resumption injects the result onto the stack.

   4. Multi-shot: Continuations clone all state on invocation, allowing
      multiple independent resumptions.

   5. In-loop flattening: Koskari-to-Koskari CALL and async gen stepping
      run in the same koskari_eval loop. When the current frame has a
      parent, YIELD and RETURN detach and push value onto the parent
      (no boundary event); only the top-level frame produces KOS_EV_*
      on yield/return/raise/await.

   6. Cross-context continuation invocation (segmented transfer): when a
      KoskariContinuationApply exception targets a different eval context
      (foreign token), process_raise intercepts it via
      handle_continuation_apply.  The exception is cleared, a
      KosDynCCRaise sentinel is inserted at the root of the dynamic chain,
      and a NULL-target transfer plan is built.  The plan unwinds all
      WITH/finally extents in the current context (calling their exit
      handlers with no active exception), and then the sentinel's unwind
      callback re-raises the original exception.  The eval loop exits with
      KOS_EV_RAISE, propagating the exception upward through the Python
      async call chain to the owning eval context.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: claude-4.6-sonnet-high via Cursor
*/


#include "lib.h"
#include "hints.h"
#include "debug.h"
#include "tracing.h"

#include <stdint.h>
#include <frameobject.h>


/* Every handler has (ctx, out_ev, insn).  Cast unused parameters to (void)
   to silence warnings and make intent explicit, e.g. (void) out_ev; (void) insn; */

/* eval.c-local static inline helpers (not the KosEvalState_* API block below). */

/**
  Map KOS_LOP_POST tag to the int historically passed to eval_phase_post_opcode.
*/
static inline int kos_op_post_tag_to_res(uint8_t tag) {

  if (tag == KOS_OP_POST_TAG_SUSPEND)
    return 1;
  if (tag == KOS_OP_POST_TAG_CONTROL_RETURNED)
    return KOS_OP_RES_CONTROL_RETURNED;
  if (tag == KOS_OP_POST_TAG_ERR)
    return -1;
  if (tag == KOS_OP_POST_TAG_PARENT_RETURN)
    return KOS_OP_RES_PARENT_RETURN_DELIVER;
  return -1;
}


/* --- KosEvalState helpers (link symbols for other TUs; same-TU calls inline at -O2) --- */

void KosEvalState_Init(KosEvalState *es, KosContext *ctx) {

  KosEvalState_CONTEXT(es) = ctx;
  KosEvalState_FRAME(es) = KosContext_FRAME_TOP(ctx);
  KosEvalState_BLOCK(es) = KosContext_BLOCK(ctx);

  if (KosEvalState_FRAME(es)) {
    KosEvalState_CODE(es) = KosDynFrame_CODE(KosEvalState_FRAME(es));
    es->pc = KosDynFrame_PC(KosEvalState_FRAME(es));
    es->acting_pc = KosDynFrame_ACTING_PC(KosEvalState_FRAME(es));
  } else {
    KosEvalState_CODE(es) = NULL;
    es->pc = 0;
    es->acting_pc = 0;
  }
}


void KosEvalState_SyncFromContext(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (ctx == NULL)
    return;

  KosEvalState_FRAME(es) = KosContext_FRAME_TOP(ctx);
  KosEvalState_BLOCK(es) = KosContext_BLOCK(ctx);
  if (KosEvalState_FRAME(es)) {
    KosEvalState_CODE(es) = KosDynFrame_CODE(KosEvalState_FRAME(es));
    es->pc = KosDynFrame_PC(KosEvalState_FRAME(es));
    es->acting_pc = KosDynFrame_ACTING_PC(KosEvalState_FRAME(es));
  } else {
    KosEvalState_CODE(es) = NULL;
    es->pc = 0;
    es->acting_pc = 0;
  }
}


KosCode *KosEvalState_CurrentCode(const KosEvalState *es) {

  KosDynFrame *f = KosEvalState_FRAME(es);
  return f ? KosDynFrame_CODE(f) : NULL;
}


PyObject *KosEvalState_CurrentNamespace(const KosEvalState *es) {

  KosDynFrame *f = KosEvalState_FRAME(es);
  return f? KosDynFrame_NS(f): NULL;
}


uint32_t KosEvalState_GetFramePc(const KosEvalState *es) {

  return KosEvalState_FRAME(es) ? es->pc : (uint32_t) 0;
}


int KosEvalState_SetFramePc(KosEvalState *es, uint32_t pc) {
  KosDynFrame *f = KosEvalState_FRAME(es);

  if (unlikely(! f)) {
    PyErr_SetString(PyExc_SystemError, "KosEvalState_SetFramePc: no frame");
    return -1;
  }

  es->pc = pc;
  KosDynFrame_PC(f) = pc;
  return 0;
}


uint32_t KosEvalState_GetActingPc(const KosEvalState *es) {

  return KosEvalState_FRAME(es) ? es->acting_pc : (uint32_t) 0;
}


int KosEvalState_SetActingPc(KosEvalState *es, uint32_t pc) {
  KosDynFrame *f = KosEvalState_FRAME(es);

  if (unlikely(! f)) {
    PyErr_SetString(PyExc_SystemError,
                    "KosEvalState_SetActingPc: no frame");
    return -1;
  }

  es->acting_pc = pc;
  KosDynFrame_ACTING_PC(f) = pc;
  return 0;
}


PyObject *KosEvalState_GetConstant(const KosEvalState *es, uint32_t index) {

  KosCode *code = KosEvalState_CODE(es);
  return code? KosCode_GET_CONST(code, index): NULL;
}


int KosEvalState_StackPushRef(KosEvalState *es, PyObject *val) {

  return KosStackBlock_PushRef(KosEvalState_BLOCK(es), val);
}


PyObject *KosEvalState_StackPopRef(KosEvalState *es) {

  return KosStackBlock_PopRef(KosEvalState_BLOCK(es));
}


PyObject *KosEvalState_GetFast(const KosEvalState *es, size_t index) {

  KosDynFrame *f = KosEvalState_FRAME(es);
  PyObject *ns = f? KosDynFrame_NS(f): NULL;
  return KosNamespace_FastDeref(ns, index);
}


int KosEvalState_SetFast(
  KosEvalState *es,
  size_t index,
  PyObject *value) {

  KosDynFrame *f = KosEvalState_FRAME(es);
  PyObject *ns = f? KosDynFrame_NS(f): NULL;
  return KosNamespace_FastStore(ns, index, value);
}


PyObject *KosEvalState_GetVar(const KosEvalState *es, PyObject *key) {

  KosDynFrame *f = KosEvalState_FRAME(es);
  PyObject *ns = f? KosDynFrame_NS(f): NULL;
  return KosNamespace_Deref(ns, key);
}


int KosEvalState_StackSteal_Tail(
    KosEvalState *es,
    KosStackSteal *s,
    size_t count) {

  return KosStackSteal_Tail(s, count, KosEvalState_BLOCK(es));
}


int KosEvalState_StackSteal_TailPrepend(
    KosEvalState *es,
    KosStackSteal *s,
    PyObject *prefix,
    size_t count) {

  return KosStackSteal_TailPrepend(s, prefix, count, KosEvalState_BLOCK(es));
}


int KosEvalState_StackPUSH(KosEvalState *es, PyObject *val) {

  return KosStackBlock_PUSH(KosEvalState_BLOCK(es), val);
}


static KosStackBlock **kos_evalstate_parent_stack_slot(KosEvalState *es) {

  KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
  KosDynNode *node;
  if (! owner) {
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
  if (! slot)
    return -1;
  return KosStackBlock_PushRef(slot, val);
}


int KosEvalState_ParentStackPUSH(KosEvalState *es, PyObject *val) {

  KosStackBlock **slot = kos_evalstate_parent_stack_slot(es);
  if (! slot) {
    Py_XDECREF(val);
    return -1;
  }
  return KosStackBlock_PUSH(slot, val);
}


PyObject *KosEvalState_StackPopTail(KosEvalState *es, size_t count) {

  return KosStackBlock_PopTail(KosEvalState_BLOCK(es), count);
}


PyObject *const *KosEvalState_StackTailPtr(KosEvalState *es, size_t count) {

  return KosStackBlock_TailPtr(*KosEvalState_BLOCK(es), count);
}


size_t KosEvalState_StackLen(KosEvalState *es) {

  return KosStackBlock_LEN(*KosEvalState_BLOCK(es));
}


int KosEvalState_StackDrop(KosEvalState *es, size_t count) {

  return KosStackBlock_Drop(KosEvalState_BLOCK(es), count);
}


int KosEvalState_StackDrop1(KosEvalState *es) {

  return KosStackBlock_Drop1(KosEvalState_BLOCK(es));
}


void KosEvalState_StackDropNoErr(KosEvalState *es, size_t count) {

  (void) KosStackBlock_DropNoErr(KosEvalState_BLOCK(es), count);
}


int KosEvalState_StackClearBlock(KosEvalState *es) {

  return KosStackBlock_ClearBlock(KosEvalState_BLOCK(es));
}


int KosEvalState_StackDuplicate(KosEvalState *es, size_t count) {

  return KosStackBlock_Duplicate(KosEvalState_BLOCK(es), count);
}


PyObject *KosEvalState_StackPopTailDict(KosEvalState *es, size_t count) {

  return KosStackBlock_PopTailDict(KosEvalState_BLOCK(es), count);
}


PyObject *KosEvalState_StackPopTailDictNames(KosEvalState *es, size_t count) {

  return KosStackBlock_PopTailDictNames(KosEvalState_BLOCK(es), count);
}


PyObject *KosEvalState_StackPopTailDictFromNames(
    KosEvalState *es,
    PyObject *names) {

  return KosStackBlock_PopTailDictFromNames(KosEvalState_BLOCK(es), names);
}


PyObject *KosEvalState_StackPeek(KosEvalState *es) {

  return KosStackBlock_Peek(*KosEvalState_BLOCK(es));
}


PyObject *KosEvalState_StackPeekAt(KosEvalState *es, Py_ssize_t index) {

  return KosStackBlock_PeekAt(*KosEvalState_BLOCK(es), index);
}


int KosEvalState_StackPopTailPtr(KosEvalState *es, size_t count, PyObject **dest) {

  return KosStackBlock_PopTailPtr(KosEvalState_BLOCK(es), count, dest);
}


typedef struct {
  PyObject *left;
  PyObject *right;
} KosEvalStackLR;


typedef struct {
  PyObject *container;
  PyObject *value;
} KosEvalStackContains;


/* Module-level string constants for await/send/throw/close.  Initialized
   in koskari_eval_init, cleared in koskari_eval_clear. */
static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;
static PyObject *str_asend = NULL;
static PyObject *str_athrow = NULL;
static PyObject *str_aclose = NULL;
static PyObject *str_enter = NULL;
static PyObject *str_exit = NULL;
static PyObject *str_aenter = NULL;
static PyObject *str_aexit = NULL;
static PyObject *str_class = NULL;
static PyObject *str_getattr = NULL;
static PyObject *str_name = NULL;
static PyObject *str_package = NULL;


/* Sentinel fromlist for IMPORT / IMPORT_REL opcodes (handlers.incl).  Passing
   ("*",) as fromlist to PyImport_ImportModuleLevelObject causes it to return
   the deepest (leaf) submodule rather than the topmost package, matching
   importlib.import_module semantics.  Initialized in koskari_eval_init. */
static PyObject *_import_fromlist = NULL;


/* === Static forward declarations === */

static void inject_eval_frame_traceback(KosContext *ctx,
    PyObject **exc_type, PyObject **exc_val, PyObject **exc_tb);

static void kos_eval_mark_context_dead(KosContext *ctx);

static int try_local_exception_handler(KosEvalState *es, KosEvent *out_ev);


/**
  Map KosEvalState_Call / KosCallable_Call legacy int r to KosOpOutcome.  When
  r==0 after install, forces PROLOGUE if a transfer plan or native handler
  is present on the current frame.
*/
static inline KosOpOutcome kos_op_outcome_from_call_int(KosEvalState *es, int r) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (r < 0)
    return KOS_OP_POST_ERR();
  if (r == KOS_OP_RES_CONTROL_RETURNED)
    return KOS_OP_POST_CONTROL();
  if (r == KOS_OP_RES_PARENT_RETURN_DELIVER)
    return KOS_OP_POST_PARENT_RETURN();
  if (r == 1)
    return KOS_OP_POST_SUSPEND();
  if (r == 0) {
    if (KosContext_PLAN(ctx))
      return KOS_OP_PROLOGUE();
    if (KosEvalState_FRAME(es)
        && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)))
      return KOS_OP_PROLOGUE();
    return KOS_OP_RUN();
  }
  return KOS_OP_POST_ERR();
}


/**
  Map push_or_await / kos_push_or_await return (0 push, 1 suspend, -1 err).
*/
static inline KosOpOutcome kos_op_outcome_from_push_int(int r) {

  if (r < 0)
    return KOS_OP_POST_ERR();
  if (r == 1)
    return KOS_OP_POST_SUSPEND();
  return KOS_OP_RUN();
}


/**
  Map legacy opcode-helper int to KosOpOutcome (0 run, 1 suspend, 2 control,
  -1 err).
*/
static inline KosOpOutcome kos_op_outcome_from_int_step(int r) {

  if (r < 0)
    return KOS_OP_POST_ERR();
  if (r == 1)
    return KOS_OP_POST_SUSPEND();
  if (r == KOS_OP_RES_CONTROL_RETURNED)
    return KOS_OP_POST_CONTROL();
  if (r == KOS_OP_RES_PARENT_RETURN_DELIVER)
    return KOS_OP_POST_PARENT_RETURN();
  return KOS_OP_RUN();
}


/**
   Clear the context's transfer plan and free it.  Plan pointer is nulled
   before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
   triggering Python dealloc/__del__) does not see the plan while it is freed.
   For PUSH_SHIFT unwind plans, transfers continuation_ref to the prompt's
   pending_shift_wrapper so the opcode can push the handler on re-entry.
*/
static void clear_and_free_transfer_plan(KosContext *ctx) {
  KosTransferPlan *p = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  if (p && KosTransferPlan_CONTINUATION_REF(p)
      && KosTransferPlan_ORIGINAL_TARGET(p)
      && KosDynNode_IS_PROMPT(KosTransferPlan_ORIGINAL_TARGET(p))) {
    KosDynPrompt *prompt = (KosDynPrompt *) KosTransferPlan_ORIGINAL_TARGET(p);
    KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = KosTransferPlan_CONTINUATION_REF(p);
    KosTransferPlan_CONTINUATION_REF(p) = NULL;
  }
  kos_free_transfer_plan(p);
}


/**
   Run the current transfer plan to completion.  Returns 0 on success (plan
   cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
   caller should return 0 from koskari_eval).
*/
static int run_transfer_plan_to_completion(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  while (KosContext_PLAN(ctx)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)));
    int plan_res = kos_run_transfer_step(es, out_ev);

    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      kos_eval_emit_raise_from_pyerr(ctx, out_ev);
      clear_and_free_transfer_plan(ctx);
      return -1;
    }

    if (plan_res == KOS_XFER_SUSPENDED_AWAIT) {
      SAY("[eval] transfer step suspended on await\n");
      return 0;  /* do not clear plan; driver will resume and feed inject */
    }

    if (plan_res == 0)
      continue;

    SAYF("[eval] transfer complete dyn_top=%p\n",
         (void *) KosContext_DYN_TOP(ctx));
    clear_and_free_transfer_plan(ctx);
  }

  return 0;
}


/**
   Handle delimited k(value) return: build plan, install it, run to completion.
   Caller has popped val; we consume it. Return 0 on success, -1 on error.
*/
static int kos_return_delimited(
    KosEvalState *es,
    KosDynDelim *delim,
    PyObject *val,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *ret_chain = KosDynDelim_RETURN_CHAIN(delim);
  uint32_t ret_pc = KosDynDelim_RETURN_PC(delim);

  SAYF("[RETURN] taking return-target path: return_pc=%u",
       (unsigned) ret_pc);
  KosTransferPlan *plan = NULL;
  if (kos_build_return_plan(es, ret_chain,
                            val, 0, &plan) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  KosTransferPlan_RESUME_PC(plan) = ret_pc;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;
  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  if (ret_chain) {
    kos_dynchain_free(ret_chain, NULL);
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }
  KosDynDelim_MODE(delim) = KOS_DYN_DELIM_MODE_NONE;
  KosDynDelim_RETURN_PC(delim) = (uint32_t)-1;
  KosDynDelim_RETURN_FRAME_NODE(delim) = NULL;
  KosDynDelim_RETURN_NODE(delim) = NULL;
  if (KosContext_DYN_TOP(ctx) == (KosDynNode *) delim)
    kos_dyn_pop_delim(es, delim);
  if (run_transfer_plan_to_completion(es, out_ev) < 0)
    return -1;
  return 0;
}


/**
   On exception: if we are inside a Koskari async gen step (NEXT or ITER_NEXT),
   detach the gen frame, close the gen, and clear NEXT / FOREACH boundary state.
*/
static void clear_kos_agen_boundary_on_error(KosContext *ctx) {

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosDynFrame *cf = KosEvalState_FRAME(&es);
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  KosDynNext *dn;
  KosDynForeach *fe;
  KosDynYieldFrom *yf = NULL;
  KosDynNode *w;
  KosPyAsyncGen *agen;

  if (! cf || ! top || ! KosDynNode_IS_FRAME(top))
    return;
  {
    KosDynNode *par = KosDynNode_PARENT(top);
    if (! par || ! KosDynNode_IS_GEN(par))
      return;
  }

  dn = kos_dyn_find_next_above_frame(&es);
  fe = kos_dyn_find_awaiting_foreach(&es);
  if (! dn && ! fe) {
    for (w = KosDynNode_PARENT((KosDynNode *) cf); w; w = KosDynNode_PARENT(w)) {
      if (KosDynNode_IS_YIELD_FROM(w)
          && KosDynNode_HAS_CAP(w, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)
          && KosDynYieldFrom_KIND((KosDynYieldFrom *) w) == (uint8_t) 4) {
        yf = (KosDynYieldFrom *) w;
        break;
      }
    }
  }

  if (! dn && ! fe && ! yf)
    return;

  if (dn)
    agen = (KosPyAsyncGen *) KosDynNext_AGEN_OBJ((KosDynNode *) dn);
  else if (fe)
    agen = (KosPyAsyncGen *) KosDynForeach_ITER(fe);
  else
    agen = (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);

  {
    KosDynFrame *detached_frame = NULL;
    if (kos_dyn_detach_top_frame(&es, &detached_frame) == 0 && detached_frame) {
      agen->closed = 1;
      agen->in_progress = 0;
      kos_dynframe_free(detached_frame);
    }
  }

  if (fe)
    KosDynForeach_ITER_STEP_PHASE(fe) = KOS_FOREACH_ITER_STEP_IDLE;
  if (dn)
    kos_dyn_unlink_next_node(&es, dn);
  if (yf)
    kos_dyn_pop_yield_from(&es, yf);
}


/**
   Revive the active transfer plan with send_val or throw_exc. Consumes
   send_val/throw_exc for the plan. May set exception in context. :return:
   KOS_MODE_TRANSFER to continue draining plan, KOS_MODE_EXCEPTION if
   throw_exc set, KOS_MODE_NORMAL if plan done
*/
static KosEvalMode revive_plan(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *plan = KosContext_PLAN(ctx);

  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }

  if (send_val != NULL && plan) {
    Py_XINCREF(send_val);
    Py_XDECREF(KosTransferPlan_STEP_RESUME_INJECT(plan));
    KosTransferPlan_STEP_RESUME_INJECT(plan) = send_val;
  }

  return KOS_MODE_TRANSFER;
}


/**
   Revive from AWAIT: push await result or set throw_exc. Use PY_RESUME_INJECT
   when set (driver pre-stored await result, e.g. coro.c
   throw-into-awaitable), else send_val. Complete pending WITH exit if
   applicable.

   When throw_exc is set and py_await_iter is non-NULL, resolves a callable
   throw and forwards with single-arg throw(exc) before the VM exception path
   (align with coroutine_throw WAITING_PY). On StopIteration, completes the
   await like a normal resume. On non-NULL throw result, fills out_ev with
   KOS_EV_AWAIT (re-suspend). out_ev must be non-NULL whenever throw_exc is
   non-NULL.
*/
static KosEvalMode revive_await(
        KosEvalState *es,
        PyObject *send_val,
        PyObject *throw_typ,
        PyObject *throw_exc,
        KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (throw_exc != NULL) {
    SAYF("[await] revive_await throw typ=%p exc=%p\n",
         (void *) throw_typ, (void *) throw_exc);
    if (! out_ev) {
      PyErr_SetString(PyExc_RuntimeError,
                      "revive_await: throw_exc set but out_ev is NULL");
      return KOS_MODE_EXCEPTION;
    }
    Py_XDECREF(KosEvent_EXC_TYPE(out_ev));
    KosEvent_EXC_TYPE(out_ev) = NULL;
    Py_XDECREF(KosEvent_PAYLOAD(out_ev));
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_KIND(out_ev) = KOS_EV_NONE;

    if (! KosContext_PY_AWAIT_ITER(ctx)) {
      PyErr_SetObject(throw_typ ? throw_typ : (PyObject *) Py_TYPE(throw_exc),
                      throw_exc);
      return KOS_MODE_EXCEPTION;
    }
    {
      PyObject *typ = throw_typ ? throw_typ
          : (PyObject *) Py_TYPE(throw_exc);
      PyObject *val = throw_exc;
      PyObject *tb = Py_None;
      PyObject *res;
      PyObject *throw_meth;
      Py_INCREF(typ);
      Py_INCREF(val);
      Py_INCREF(tb);
      PyErr_NormalizeException(&typ, &val, &tb);
      throw_meth = PyObject_GetAttr(KosContext_PY_AWAIT_ITER(ctx), str_throw);
      if (! throw_meth) {
        Py_DECREF(typ);
        Py_DECREF(val);
        Py_XDECREF(tb);
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
          PyErr_Clear();
          Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
          PyErr_SetObject(throw_typ ? throw_typ : (PyObject *) Py_TYPE(throw_exc),
                          throw_exc);
          return KOS_MODE_EXCEPTION;
        }
        Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
        return KOS_MODE_EXCEPTION;
      }
      if (! PyCallable_Check(throw_meth)) {
        PyObject *await_iter = KosContext_PY_AWAIT_ITER(ctx);
        PyErr_Format(PyExc_TypeError,
                     "%R object attribute throw is not callable",
                     await_iter);
        Py_DECREF(throw_meth);
        Py_DECREF(typ);
        Py_DECREF(val);
        Py_XDECREF(tb);
        Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
        return KOS_MODE_EXCEPTION;
      }
      /* Single-arg throw(exc) avoids deprecated (typ, val, tb) on generators */
      res = PyObject_CallOneArg(throw_meth, val);
      Py_DECREF(throw_meth);
      Py_DECREF(typ);
      Py_DECREF(val);
      Py_XDECREF(tb);
      if (res) {
        KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
        KosEvent_PAYLOAD(out_ev) = res;
        PyErr_Clear();
        return KOS_MODE_AWAIT;
      }
      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyObject *await_result = NULL;
        if (PyGen_FetchStopIterationValue(&await_result) < 0) {
          Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
          return KOS_MODE_EXCEPTION;
        }
        if (! await_result) {
          await_result = Py_None;
          Py_INCREF(await_result);
        }
        Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
        Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
        KosContext_PY_RESUME_INJECT(ctx) = await_result;
        KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_AWAIT;
        PyErr_Clear();
        /* Fall through: push inject like a normal successful resume */
      }
      else {
        Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
        return KOS_MODE_EXCEPTION;
      }
    }
  }
  PyObject *to_push = KosContext_PY_RESUME_INJECT(ctx);
  if (to_push != NULL) {
    KosContext_PY_RESUME_INJECT(ctx) = NULL;
  } else if (send_val != NULL) {
    to_push = send_val;
  }
  if (to_push != NULL && KosEvalState_FRAME(es)) {
    if (KosEvalState_StackPushRef(es, to_push) < 0)
      return KOS_MODE_EXCEPTION;
  }
  {
    if (kos_dyn_complete_pending_exit(es, 1) < 0)
      return KOS_MODE_EXCEPTION;
  }
  return KOS_MODE_NORMAL;
}


/**
   Revive from YIELD: pop install placeholder (Py_None from kos_dyn_gen_install),
   then push send_val or set throw_exc. When send_val is NULL (e.g. __anext__),
   push Py_None per async gen protocol. Caller must have called
   kos_dyn_gen_install before koskari_eval (e.g. KosPyAsyncGen drivers, tests).
*/
static KosEvalMode revive_yield(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *to_push;

  SAYF("[gen] revive_yield ENTER suspend=%d send_val=%p "
       "throw_typ=%p throw_exc=%p dyn_top=%p frame_top=%p\n",
       KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es)),
       (void *) send_val, (void *) throw_typ, (void *) throw_exc,
       (void *) KosContext_DYN_TOP(KosEvalState_CONTEXT(es)),
       (void *) KosEvalState_FRAME(es));

  if (throw_exc != NULL) {
    if (KosEvalState_FRAME(es))
      (void) KosEvalState_StackPopRef(es);
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (KosEvalState_FRAME(es)) {
    (void) KosEvalState_StackPopRef(es);
    to_push = (send_val != NULL) ? send_val : Py_None;
    if (KosEvalState_StackPushRef(es, to_push) < 0)
      return KOS_MODE_EXCEPTION;
  }
  SAYF("[gen] revive_yield DONE frame_top=%p dyn_top=%p\n",
       (void *) KosEvalState_FRAME(es),
       (void *) KosContext_DYN_TOP(KosEvalState_CONTEXT(es)));
  return KOS_MODE_NORMAL;
}


static void decorate_exception_with_dynchain(
    KosContext *ctx,
    PyObject *exc_val) {

  if (! ctx || ! exc_val || ! PyExceptionInstance_Check(exc_val)) {
    return;
  }

  PyObject *frames = PyList_New(0);
  if (! frames) {
    PyErr_Clear();
    return;
  }

  for (KosDynNode *node = KosContext_DYN_TOP(ctx); node; node = KosDynNode_PARENT(node)) {
    if (! KosDynNode_IS_FRAME(node)) {
      continue;
    }
    KosDynFrame *frame = (KosDynFrame *) node;
    PyObject *meta = PyDict_New();
    if (! meta) {
      PyErr_Clear();
      continue;
    }

    KosCode *code = KosDynFrame_CODE(frame);
    uint16_t line = 0, col = 0;
    uint32_t pc = KosDynFrame_ACTING_PC(frame);
    if (code)
      kos_loc_lookup((PyObject *) code, pc, &line, &col);

    PyObject *name = code ? KosCode_NAME(code) : Py_None;
    PyObject *filename = code ? KosCode_FILENAME(code) : Py_None;

    PyDict_SetItemString(meta, "name", name ? name : Py_None);
    PyDict_SetItemString(meta, "filename", filename ? filename : Py_None);
    PyObject *line_o = PyLong_FromUnsignedLong((unsigned long) line);
    PyObject *col_o = PyLong_FromUnsignedLong((unsigned long) col);
    PyObject *pc_o = PyLong_FromUnsignedLong((unsigned long) pc);
    if (line_o && col_o && pc_o) {
      PyDict_SetItemString(meta, "line", line_o);
      PyDict_SetItemString(meta, "col", col_o);
      PyDict_SetItemString(meta, "pc", pc_o);
    }
    Py_XDECREF(line_o);
    Py_XDECREF(col_o);
    Py_XDECREF(pc_o);
    PyList_Append(frames, meta);
    Py_DECREF(meta);
  }

  if (PyObject_SetAttrString(exc_val, "__koskari_dynchain__", frames) < 0) {
    PyErr_Clear();
  }

  Py_DECREF(frames);
}


static void inject_eval_frame_traceback(
    KosContext *ctx,
    PyObject **exc_type,
    PyObject **exc_val,
    PyObject **exc_tb) {

  if (! ctx || ! exc_type || ! exc_val || ! *exc_val) {
    return;
  }

  if (KoskariContinuationApply && *exc_type
      && PyObject_IsSubclass(*exc_type, KoskariContinuationApply)) {
    return;
  }

  #if PY_MINOR_VERSION < 12
  // I cannot tell you how long this took to debug...
  PyErr_NormalizeException(exc_type, exc_val, exc_tb);
  #endif

  decorate_exception_with_dynchain(ctx, *exc_val);

  KosEvalState _inj_es;
  KosEvalState_Init(&_inj_es, ctx);
  KosDynFrame *frame_top = KosEvalState_FRAME(&_inj_es);
  if (! frame_top || ! KosDynFrame_CODE(frame_top)) {
    return;
  }

  PyCodeObject *faux = KosCode_Faux((PyObject *) KosDynFrame_CODE(frame_top));
  if (! faux) {
    PyErr_Clear();
    return;
  }

  PyObject *globals = PyDict_New();
  if (! globals) {
    PyErr_Clear();
    return;
  }
  PyObject *modname = PyUnicode_FromString("koskari.native");
  if (modname) {
    PyDict_SetItemString(globals, "__name__", modname);
    Py_DECREF(modname);
  }
  PyObject *trace_data = PyObject_GetAttrString(*exc_val, "__koskari_dynchain__");
  if (trace_data) {
    PyDict_SetItemString(globals, "__koskari_trace__", trace_data);
    Py_DECREF(trace_data);
  } else {
    PyErr_Clear();
  }

  PyFrameObject *pyframe = PyFrame_New(PyThreadState_Get(), faux, globals, NULL);
  Py_DECREF(globals);
  if (! pyframe) {
    SAY("[eval] inject_eval_frame_traceback: PyFrame_New failed\n");
    PyErr_Clear();
    return;
  }

  PyErr_Restore(*exc_type, *exc_val, *exc_tb);
  *exc_type = NULL;
  *exc_val = NULL;
  *exc_tb = NULL;

  if (PyTraceBack_Here(pyframe) < 0) {
    SAY("[eval] inject_eval_frame_traceback: PyTraceBack_Here failed\n");
    PyErr_Clear();
  }
  Py_DECREF(pyframe);

  SAY("[eval] inject_eval_frame_traceback: PyTraceBack_Here succeeded\n");
  PyErr_Fetch(exc_type, exc_val, exc_tb);

  /* PyTraceBack_Here updates the thread-state traceback only; the exception
     value's __traceback__ is not updated. Attach the extended chain to the
     value so it survives PyErr_SetObject in coro.c (which uses value as-is). */
  if (*exc_val && *exc_tb && PyExceptionInstance_Check(*exc_val)) {
    if (PyObject_SetAttrString(*exc_val, "__traceback__", *exc_tb) < 0)
      PyErr_Clear();
  }
}


static void kos_eval_mark_context_dead(KosContext *ctx) {

  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
}


/**
   Move current PyErr into out_ev as KOS_EV_RAISE with injected traceback,
   clear thread error, and mark the context dead.  Call only when PyErr is set.
*/
void kos_eval_emit_raise_from_pyerr(
    KosContext *ctx,
    KosEvent *out_ev) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  PyErr_Fetch(&typ, &val, &tb);
  inject_eval_frame_traceback(ctx, &typ, &val, &tb);
  Py_XDECREF(tb);
  KosEvent_KIND(out_ev) = KOS_EV_RAISE;
  KosEvent_EXC_TYPE(out_ev) = typ;
  KosEvent_PAYLOAD(out_ev) = val;
  PyErr_Clear();
  kos_eval_mark_context_dead(ctx);
}


/**
   Intercept a KoskariContinuationApply exception and install the appropriate
   transfer plan.  PyErr must be set to KoskariContinuationApply on entry.

   Own-token path: the continuation targets this eval context.  Extract the
   continuation and resume value, clear PyErr, and call
   kos_invoke_continuation to install a direct-apply transfer plan.

   Foreign-token path: the continuation targets a different eval context.
   Fetch and clear PyErr, insert a KosDynCCRaise sentinel at the root of the
   dynamic chain, and build a NULL-target transfer plan that unwinds all local
   WITH/finally extents and then re-raises the original exception via the
   sentinel's unwind callback.  The eval loop then exits with KOS_EV_RAISE so
   the exception propagates upward to the owning context.

   :return: KOS_MODE_TRANSFER on success, KOS_MODE_RAISE on error
*/
static KosEvalMode handle_continuation_apply(
    KosEvalState *es,
    KosEvent *out_ev) {

  (void) out_ev;

  KosContext *ctx = KosEvalState_CONTEXT(es);

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  Py_XDECREF(tb);

  if (! val) {
    Py_XDECREF(typ);
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply: missing exception value");
    return KOS_MODE_RAISE;
  }

  PyObject *exc_target = PyObject_GetAttrString(val, "target");
  if (! exc_target) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return KOS_MODE_RAISE;
  }

  int is_own = (exc_target == (PyObject *) KosContext_TOKEN(ctx));
  Py_DECREF(exc_target);

  if (is_own) {
    PyObject *cont_obj = PyObject_GetAttrString(val, "continuation");
    PyObject *args_obj = PyObject_GetAttrString(val, "args");

    if (cont_obj && PyObject_TypeCheck(cont_obj, KOS_CALLABLE_PYTYPE(KosPyContinuationType))) {
      KosContinuation *k = ((KosPyContinuation *) cont_obj)->captured;
      PyObject *resume_val = Py_None;

      if (args_obj && PyTuple_Check(args_obj) && PyTuple_GET_SIZE(args_obj) > 0) {
        resume_val = PyTuple_GET_ITEM(args_obj, 0);
        Py_INCREF(resume_val);
      } else {
        Py_INCREF(resume_val);
      }

      Py_XDECREF(args_obj);
      PyErr_Clear();

      if (k && kos_invoke_continuation(es, k, resume_val, 0, cont_obj) == 0) {
        Py_DECREF(resume_val);
        Py_DECREF(typ);
        Py_DECREF(val);
        Py_DECREF(cont_obj);
        return KOS_MODE_TRANSFER;
      }
      Py_DECREF(resume_val);
    } else {
      Py_XDECREF(args_obj);
    }

    Py_DECREF(typ);
    Py_DECREF(val);
    Py_XDECREF(cont_obj);

    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "KoskariContinuationApply: invalid continuation");
    return KOS_MODE_RAISE;
  }

  /* Foreign-token path: unwind this context to null and re-raise. */
  PyErr_Clear();
  KosTransferPlan *plan = NULL;
  if (kos_build_cc_unwind_plan(es, typ, val, &plan) < 0) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return KOS_MODE_RAISE;
  }
  Py_DECREF(typ);
  Py_DECREF(val);

  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  return KOS_MODE_TRANSFER;
}


/**
   Process the current exception (in PyErr or context). Handles
   KoskariContinuationApply (invoke continuation or unwind-and-reraise), local
   handlers (try/except/finally, WITH exit, unwind), or returns RAISE. Call
   only when mode is EXCEPTION.

   :return: KOS_MODE_NORMAL or KOS_MODE_TRANSFER to continue, KOS_MODE_RAISE
            to propagate (out_ev set to KOS_EV_RAISE)
*/
static KosEvalMode process_raise(KosEvalState *es, KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! PyErr_Occurred()) {
    KosEvent_KIND(out_ev) = KOS_EV_RAISE;
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_EXC_TYPE(out_ev) = NULL;
    return KOS_MODE_RAISE;
  }

  if (KoskariContinuationApply && PyErr_ExceptionMatches(KoskariContinuationApply))
    return handle_continuation_apply(es, out_ev);
  clear_kos_agen_boundary_on_error(ctx);

  {
    int tl = try_local_exception_handler(es, out_ev);
    if (tl > 0)
      return KOS_MODE_NORMAL;
    /* tl<0: PyErr set; tl==0: out_ev is KOS_EV_RAISE, PyErr cleared */
    return KOS_MODE_RAISE;
  }
}


/**
   Resolve KosOp_ARG(insn) as a prompt_id constant from code.  Returns -1 on
   error, 0 on success with *out_prompt_id set.
*/
static int get_prompt_id_from_insn(
    KosCode *code,
    KosOp *insn,
    uint32_t *out_prompt_id) {

  PyObject *cid = KosCode_GetConstant((PyObject *) code, KosOp_ARG(insn));
  if (! cid)
    return -1;

  long pid = PyLong_AsLong(cid);
  if (pid < 0 && PyErr_Occurred())
    return -1;

  *out_prompt_id = (uint32_t) pid;
  return 0;
}


#if KOS_STACK_TRACE
/**
   Print current stack block address/id and length when KOS_ENABLE_DEBUG;
   also print top two slot values as integers when PyLong.
*/
static void trace_stack(const char *prefix, KosStackBlock *stack) {
  SAYF("[eval] %s stack=%p", prefix, (void *) stack);
  if (stack) {
    size_t n = KosStackBlock_LEN(stack);
    SAYF(" len=%zu", n);
    for (size_t i = (n >= 2 ? n - 2 : 0); i < n; i++) {
      PyObject *o = stack->objs[i];
      if (PyLong_Check(o))
        SAYF(" [%zu]=%ld", i, PyLong_AsLong(o));
    }
  }
  SAY("\n");
}
#endif


/**
   Resolve enter/exit on mgr. Prefer __aenter__/__aexit__ if present (async
   protocol), else __enter__/__exit__ (sync). Returns new refs in *out_enter
   and *out_exit.

   :return: 0 on success, -1 on error
*/
static int resolve_context_enter_exit(
    PyObject *mgr,
    PyObject **out_enter,
    PyObject **out_exit) {

  PyObject *enter = PyObject_GetAttr(mgr, str_aenter);
  PyObject *exit = NULL;
  if (enter) {
    exit = PyObject_GetAttr(mgr, str_aexit);
  }

  if (! enter || ! exit) {
    Py_XDECREF(enter);
    PyErr_Clear();

    enter = PyObject_GetAttr(mgr, str_enter);
    if (! enter)
      return -1;

    exit = PyObject_GetAttr(mgr, str_exit);
    if (! exit) {
      Py_DECREF(enter);
      return -1;
    }
  }

  *out_enter = enter;
  *out_exit = exit;
  return 0;
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


KosEvaluatorToken *KosEvaluatorToken_New(void) {
  KosEvaluatorToken *token = PyObject_New(KosEvaluatorToken,
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


/* === Exception types === */

PyObject *KoskariContinuationDead = NULL;
PyObject *KoskariContinuationApply = NULL;
PyObject *KoskariContinuationError = NULL;


/* === KosFrame implementation (wrapper around KosDynFrame) === */


KosFrame *kos_frame_new(
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack,
    KosDynNode *parent) {

  return kos_dynframe_new(parent, code, ns, stack);
}


KosFrame *kos_frame_clone(KosFrame *frame) {
  return kos_dynframe_clone(frame);
}


void kos_frame_free(KosFrame *frame) {
  kos_dynframe_free(frame);
}


int kos_frame_traverse(
    KosFrame *frame,
    visitproc visit,
    void *arg) {

  if (! frame)
    return 0;

  Py_VISIT(KosDynFrame_NS(frame));

  if (KosDynFrame_STACK(frame)) {
    KosStackBlock *b = KosDynFrame_STACK(frame);
    while (b) {
      for (size_t i = b->len; i--; ) {
        Py_VISIT(b->objs[i]);
      }
      b = KosStackBlock_PARENT(b);
    }
  }

  return 0;
}


/* === KosContext implementation === */

#define CONTEXT_MAX_FREE 16  /* cap for cached KosContext; tunable */

static size_t context_freecount = 0;
static KosContext *context_freelist = NULL;


KosContext *kos_context_new(KosDynFrame *top) {

  KosContext *ctx = context_freelist;
  if (ctx) {
    context_freelist = KosContext_FREELIST_NEXT(ctx);
    context_freecount--;
    KOS_TRACE_FL_ALLOC("context", 1);
    memset(ctx, 0, sizeof(KosContext));
  } else {
    KOS_TRACE_FL_ALLOC("context", 0);
    ctx = PyMem_Malloc(sizeof(KosContext));
    if (! ctx) {
      PyErr_NoMemory();
      return NULL;
    }
    memset(ctx, 0, sizeof(KosContext));
  }

  KosContext_TOKEN(ctx) = KosEvaluatorToken_New();
  if (! KosContext_TOKEN(ctx)) {
    if (context_freecount < CONTEXT_MAX_FREE) {
      KOS_TRACE_FL_RETURN("context", 0);
      memset(ctx, 0, sizeof(KosContext));
      KosContext_FREELIST_NEXT(ctx) = context_freelist;
      context_freelist = ctx;
      context_freecount++;
    } else {
      KOS_TRACE_FL_RETURN("context", 1);
      PyMem_Free(ctx);
    }
    return NULL;
  }

  {
    PyObject *domain = NULL;
    KosContext *active = kos_ctx_current();

    if (active && KosContext_DOMAIN(active))
      domain = KosContext_DOMAIN(active);
    else
      domain = kos_domain_current();
    if (domain) {
      ctx->domain = domain;
      Py_INCREF(ctx->domain);
    } else {
      ctx->domain = (PyObject *) KosDomain_New();
      if (! ctx->domain) {
        Py_DECREF(KosContext_TOKEN(ctx));
        KosContext_TOKEN(ctx) = NULL;
        if (context_freecount < CONTEXT_MAX_FREE) {
          KOS_TRACE_FL_RETURN("context", 0);
          memset(ctx, 0, sizeof(KosContext));
          KosContext_FREELIST_NEXT(ctx) = context_freelist;
          context_freelist = ctx;
          context_freecount++;
        } else {
          KOS_TRACE_FL_RETURN("context", 1);
          PyMem_Free(ctx);
        }
        return NULL;
      }
    }
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  KosContext_FRAME_TOP(ctx) = top;
  KosContext_GLOBALS_FRAME(ctx) = top;
  KosContext_GLOBALS_CACHE_VERSION(ctx) = 0;

  KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;

  /* Initialize transfer plan state */
  KosContext_PLAN(ctx) = NULL;
  KosContext_PLAN_STACK(ctx) = NULL;
  KosContext_CHAIN_VERSION(ctx) = 0;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));

  /* Initialize Python await bridging */
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  /* Initialize exception injection */
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_TYPE(ctx) = NULL;
  KosContext_INJECT_VALUE(ctx) = NULL;

  KosContext_FLAGS(ctx) = 0;

  {
    KosEvalState es;
    KosEvalState_Init(&es, ctx);
    KosEvalState_RefreshDynCaches(&es);
  }

  return ctx;
}


void kos_context_free(KosContext *ctx) {

  if (! ctx)
    return;

  Py_XDECREF(KosContext_TOKEN(ctx));
  KosContext_TOKEN(ctx) = NULL;

  /* Free the dynamic chain */
  kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);

  /* Free any active transfer plan */
  kos_free_transfer_plan(KosContext_PLAN(ctx));
  while (KosContext_PLAN_STACK(ctx)) {
    KosTransferPlan *p = KosContext_PLAN_STACK(ctx);
    KosContext_PLAN_STACK(ctx) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  Py_XDECREF(KosContext_INJECT_TYPE(ctx));
  KosContext_INJECT_TYPE(ctx) = NULL;
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  Py_XDECREF(KosContext_PENDING_RETURN_VALUE(ctx));
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;

  /* Clear pending WITH state (exc_* on any WITH in PENDING_EXIT_AWAIT) */
  kos_clear_pending_with_exit(ctx);

  if (context_freecount < CONTEXT_MAX_FREE) {
    KOS_TRACE_FL_RETURN("context", 0);
    memset(ctx, 0, sizeof(KosContext));
    KosContext_FREELIST_NEXT(ctx) = context_freelist;
    context_freelist = ctx;
    context_freecount++;
  } else {
    KOS_TRACE_FL_RETURN("context", 1);
    PyMem_Free(ctx);
  }
}


int kos_context_traverse(
    KosContext *ctx,
    visitproc visit,
    void *arg) {

  if (! ctx)
    return 0;

  Py_VISIT(KosContext_TOKEN(ctx));
  Py_VISIT(KosContext_DOMAIN(ctx));
  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_TYPE(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));

  /* Traverse the dynamic chain */
  int res = kos_dynchain_traverse(KosContext_DYN_TOP(ctx), NULL, visit, arg);
  if (res)
    return res;

  /* Traverse transfer plan */
  res = kos_transferplan_traverse(KosContext_PLAN(ctx), visit, arg);
  if (res)
    return res;

  return 0;
}


/* === GC helper for stack blocks === */


int kos_stackblock_traverse(
    KosStackBlock *b,
    visitproc visit,
    void *arg) {

  size_t i;
  while (b) {
    for (i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = b->back;
  }

  return 0;
}


/* === Koskari call operations === */


int kos_tail_koscall(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  KosEvalState es;
  KosEvalState_Init(&es, ctx);

  if (! ctx || ! KosEvalState_FRAME(&es)) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = KosEvalState_FRAME(&es);
  KosDynNode *parent = KosDynNode_PARENT(old);

  /* Pop the old frame */
  KosContext_DYN_TOP(ctx) = parent;

  /* Create new frame with same parent */
  KosDynFrame *frame = kos_dynframe_new(parent, code, ns, NULL);
  if (! frame) {
    /* Restore old frame on failure */
    KosContext_DYN_TOP(ctx) = (KosDynNode *) old;
    return -1;
  }

  /* Free old frame */
  kos_dynframe_free(old);

  /* Install new frame */
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;
  KosEvalState_Init(&es, ctx);
  KosEvalState_RefreshDynCaches(&es);

  return 0;
}


/* === Drive loop === */


/**
   Attempt eager-step of a Python awaitable.

   This tries to get __await__() and call send(None) once.
   If the awaitable completes immediately (StopIteration), the value
   is returned. If it yields, we store the iterator and return the
   yielded value with *suspended = 1.

   :param awaitable: the Python awaitable object
   :param ctx: context to store await state if suspended
   :param suspended: output flag, set to 1 if we need to suspend
   :param yielded: output, the yielded value if suspended

   :return: completed value on immediate completion, NULL on suspend or error
*/
static PyObject *eager_step_awaitable(
    PyObject *awaitable,
    KosContext *ctx,
    int *suspended,
    PyObject **yielded) {

  *suspended = 0;
  *yielded = NULL;

  if (! awaitable) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: awaitable is NULL");
    return NULL;
  }

  /* Get __await__ */
  PyObject *await_iter = NULL;

  PyObject *await_method = PyObject_GetAttr(awaitable, str_await);
  if (! await_method) {
    /* Maybe it's already an iterator? */
    PyErr_Clear();
    await_iter = awaitable;
    Py_INCREF(await_iter);

  } else {
    await_iter = PyObject_CallNoArgs(await_method);
    Py_DECREF(await_method);
    if (! await_iter)
      return NULL;
  }

  if (! await_iter) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: __await__() returned NULL");
    return NULL;
  }

  SAYF("[eval] eager_step_awaitable before send(None) (await_iter=%d)\n",
       await_iter ? 1 : 0);

  /* Try first send(None) */
  PyObject *result = PyObject_CallMethodObjArgs(await_iter, str_send,
                                                Py_None, NULL);

  if (result) {
    /* Yielded a value - need to suspend */
    *suspended = 1;

    /* Store yielded value */
    *yielded = result;

    /* Store iterator for later resumption */
    Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
    KosContext_PY_AWAIT_ITER(ctx) = await_iter;

    /* signal suspend */
    return NULL;
  }

  /* Check for StopIteration (immediate completion) */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *value = NULL;
    if (PyGen_FetchStopIterationValue(&value) < 0) {
      /* FetchStopIterationValue failed and set a new exception; propagate. */
      Py_DECREF(await_iter);
      return NULL;
    }
    Py_DECREF(await_iter);

    if (! value) {
      value = Py_None;
      Py_INCREF(value);
    }

    return value;
  }

  /* Other exception */
  Py_DECREF(await_iter);
  return NULL;
}


/**
   Get the awaitable for one step of an async iterator (__anext__).
   Mirror of sibilant-ceval get_await_next: tp_as_async->am_anext then
   PyObject_GetAwaitable. Caller must Py_DECREF the result.

   :return: new reference to awaitable, or NULL on error or if not async
*/
static PyObject *get_await_next(PyObject *val) {
  PyTypeObject *type = Py_TYPE(val);
  unaryfunc anext = NULL;

  if (type->tp_as_async)
    anext = type->tp_as_async->am_anext;

  if (! anext)
    return NULL;

  PyObject *thing = anext(val);
  if (! thing)
    return NULL;

  PyObject *awaitable = PyObject_GetAwaitable(thing);
  Py_DECREF(thing);
  return awaitable;
}


/**
   Push val onto the frame stack. No auto-await; val is used as-is.
   val is consumed (new reference). Use this for all values that are not
   the direct result of a call (e.g. CONST, load, getattr, subscript).

   :return: 0 continue, -1 error
*/
KOS_OP_INLINE int push_result(KosEvalState *es, PyObject *val) {
  if (unlikely(! val))
    return -1;
  return KosEvalState_StackPUSH(es, val);
}


/**
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_EV_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use push_result
   so we do not auto-await values that are merely loaded or computed.

   CALL* bytecode uses op_push_or_await instead: same logic plus an optional
   POP,1 peephole so kos_push_or_await and this function stay free of that
   cost on every non-opcode caller (e.g. defer_drive, KosEvalState_Call).

   val is consumed (new reference).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/
static int push_or_await_common(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val,
    int await_defer) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val)) {
    return KosEvalState_StackPUSH(es, val);
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    return KosEvalState_StackPUSH(es, val);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(val, ctx, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    return KosEvalState_StackPUSH(es, result);
  }

  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    SAYF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


static int push_or_await(KosEvalState *es, KosEvent *out_ev, PyObject *val) {
  return push_or_await_common(es, out_ev, val, 0);
}


/**
   Opcode-only variant of push_or_await: same semantics, plus peephole elision
   when the next bytecode is POP with argument 1 (discard TOS). In that case
   the value is released and the frame PC is advanced past that POP without a
   stack push. Used only from CALL* handlers where the frame PC is already the
   next instruction index after fetch. Do not use from defer_drive or other
   paths that pop the stack in C before the next opcode — use push_or_await.

   val is consumed (new reference).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/

#define OP_TRY_FUSE_POP1_AFTER_PUSH(es, val)                                  \
  do {                                                                        \
    KosDynFrame *_opfuse_pf = KosEvalState_FRAME(es);                         \
    if (! _opfuse_pf)                                                         \
      break;                                                                  \
    KosCode *opfuse_code = KosDynFrame_CODE(_opfuse_pf);                      \
    if (! opfuse_code)                                                        \
      opfuse_code = KosEvalState_CODE(es);                                    \
    if (! opfuse_code)                                                        \
      break;                                                                  \
    uint32_t opfuse_pc = (es)->pc;                                            \
    KosOp *opfuse_nx = &KosCode_CODE(opfuse_code)[opfuse_pc];                 \
    if (KosOp_OP(opfuse_nx) == KOS_OP_POP && KosOp_ARG(opfuse_nx) == 1) {     \
      Py_DECREF(val);                                                         \
      if (KosEvalState_SetFramePc(es, opfuse_pc + 1u) < 0)                     \
        return -1;                                                            \
      return 0;                                                               \
    }                                                                         \
  } while (0)


KOS_OP_INLINE int op_push_or_await(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (KosDefer_Check(val)) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return KosEvalState_StackPUSH(es, val);
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] op_push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return KosEvalState_StackPUSH(es, val);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(val, ctx, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, result);
    return KosEvalState_StackPUSH(es, result);
  }

  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    SAYF("[eval] op_push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


#undef OP_TRY_FUSE_POP1_AFTER_PUSH


/* Public wrapper for external callers; delegates to static push_or_await. */
int kos_push_or_await(KosEvalState *es, KosEvent *out_ev, PyObject *val) {
  return push_or_await(es, out_ev, val);
}


int kos_complete_pending_with_exit(KosContext *ctx) {
  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  return kos_dyn_complete_pending_exit(&es, 1);
}


int kos_complete_pending_with_exit_semantic(KosContext *ctx) {
  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  return kos_dyn_complete_pending_exit(&es, 0);
}


void kos_clear_pending_with_exit(KosContext *ctx) {
  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  kos_dyn_clear_pending_exit(&es);
}


/* === Attribute resolution for CALL_ATTR* === */


/**
   True if descr is a plain instance-method descriptor (KosFunction or
   PyFunction) that we can return without calling __get__.
*/
static int is_plain_instance_method(PyObject *descr) {
  return KosFunction_CheckExact(descr) || PyFunction_Check(descr);
}


/**
   True if descr has __set__ or __delete__ (data descriptor).
*/
static int descr_is_data(PyObject *descr) {
  PyTypeObject *tp = Py_TYPE(descr);
  return tp->tp_descr_set != NULL;
}


/**
   Get instance dict from obj. Returns NULL if no instance dict.
   Uses _PyObject_GetDictPtr (Python C API) for correct layout handling
   across versions (e.g. Py_TPFLAGS_MANAGED_DICT, inline values).
*/
static PyObject *instance_dict_get(PyObject *obj) {
  PyObject **dictptr = _PyObject_GetDictPtr(obj);
  if (! dictptr)
    return NULL;
  return *dictptr;
}


/**
   Resolve attribute by name on an object. Uses cache when provided and
   valid. When cache is NULL, performs full resolution only.

   :param obj: Object to resolve attribute on (borrowed).
   :param name: Attribute name (borrowed, PyUnicode).
   :param cache: Optional per-call-site cache (NULL = always full lookup).
   :param out: Output: resolved attribute (new ref on success).
   :return: -1 on error (exception set); 0 = send self (unbound class method);
     1 = omit self (bound, instance dict, etc.).
*/
static int kos_resolve_named_attr(
    PyObject *obj,
    PyObject *name,
    void *cache,
    PyObject **out) {

  (void) cache;

  PyTypeObject *tp = Py_TYPE(obj);

  /*
     Mode: CUSTOM_GETATTRIBUTE (fallback)
     The type overrides __getattribute__ (tp_getattro != PyObject_GenericGetAttr).
     We cannot safely replicate the type's custom lookup; delegate to
     PyObject_GetAttr which invokes the type's tp_getattro slot.
  */
  if (tp->tp_getattro != PyObject_GenericGetAttr) {
    *out = PyObject_GetAttr(obj, name);
    if (! *out)
      return -1;
    // return is_plain_instance_method(*out) ? 0 : 1;
    /*
      PyObject_GetAttr already performed the type's lookup/binding semantics.
      Even if the result is a plain function object, it is now just the resolved
      attribute value and must be called as-is (no implicit self injection).
    */
    return 1;
  }

  /*
     CACHE CHECK AND INVALIDATION (design/interpreter/attr-lookup-cache.md)
     Insert here, before resolution below. When cache != NULL:
     1. Check cache presence — UNCACHED → fall through.
     2. Check cache validity — type match and type_version match; else fall through.
     3. Instance shadow check — if instance __dict__ contains name, fall through
        (instance overrules cached class result). Applies to DIRECT_OBJECT_GUARDED.
     4. Use cached result if guards pass; else fall through.
     On fall-through, resolution below runs (data descriptor → instance dict →
     non-data descriptor → __getattr__). On successful return from resolution,
     populate cache. On eviction: Py_XDECREF(entry->resolved).
  */

  PyObject *descr = PyType_Lookup(tp, name);

  /*
     Mode: DATA_DESCRIPTOR (type lookup)
     Data descriptors (have __set__ or __delete__) override instance dict.
     Must call __get__(obj, type) — the value is computed per access (e.g.
     property). PyType_Lookup already applies MRO precedence.
  */
  if (descr && descr_is_data(descr)) {
    PyObject *res = Py_TYPE(descr)->tp_descr_get(
        descr, obj, (PyObject *) tp);
    if (res) {
      *out = res;
      return 1;
    }
    return -1;
  }

  /*
     Mode: INSTANCE_DICT
     No data descriptor in MRO; check instance __dict__. If the name is present,
     return it. No descriptor protocol — the value is stored directly. Instance
     dict shadows class attributes (non-data descriptors) by Python semantics.
  */
  {
    PyObject *dict = instance_dict_get(obj);
    if (dict) {
      PyObject *val = PyDict_GetItem(dict, name);
      if (val) {
        Py_INCREF(val);
        *out = val;
        return 1;
      }
    }
  }

  /*
     Mode: NON_DATA_DESCRIPTOR / CLASS_ATTRIBUTE (type lookup)
     Not in instance dict; use class-side attribute. Three cases:
     (a) Plain instance method (KosFunction, PyFunction): return descriptor
         without calling __get__. Caller invokes fn(obj, *args) — avoids
         allocating KosBoundMethod or types.MethodType.
     (b) Descriptor with tp_descr_get (slot wrapper, staticmethod, classmethod):
         must call __get__(obj, type); behaviour depends on descriptor.
     (c) Plain attribute (no __get__): return as-is, INCREF.
  */
  if (descr) {
    if (is_plain_instance_method(descr)) {
      Py_INCREF(descr);
      *out = descr;
      return 0;
    }
    if (Py_TYPE(descr)->tp_descr_get != NULL) {
      PyObject *res = Py_TYPE(descr)->tp_descr_get(
          descr, obj, (PyObject *) tp);
      if (res) {
        *out = res;
        return 1;
      }
      return -1;
    }
    Py_INCREF(descr);
    *out = descr;
    return 1;
  }

  /*
     Mode: __getattr__ (fallback)
     Attribute not found by standard lookup. If the type defines __getattr__,
     call it with (name); it is invoked only when normal lookup fails. Raises
     AttributeError if no __getattr__ or if it does not provide the attribute.
  */
  {
    PyObject *getattr_descr = PyType_Lookup(tp, str_getattr);
    if (getattr_descr) {
      PyObject *bound;
      PyObject *res;

      if (Py_TYPE(getattr_descr)->tp_descr_get != NULL) {
        bound = Py_TYPE(getattr_descr)->tp_descr_get(
            getattr_descr, obj, (PyObject *) tp);
      } else {
        bound = getattr_descr;
        Py_INCREF(bound);
      }
      if (! bound)
        return -1;
      res = PyObject_CallFunctionObjArgs(bound, name, NULL);
      Py_DECREF(bound);
      if (res) {
        *out = res;
        // return is_plain_instance_method(res) ? 0 : 1;
        /* __getattr__ returns the final attribute value; call it as-is. */
        return 1;
      }
      return -1;
    }
  }

  /*
     Not found in any mode. Raise AttributeError per Python semantics.
  */
  PyErr_Format(PyExc_AttributeError,
      "'%.100s' object has no attribute '%R'",
      tp->tp_name, name);
  return -1;
}


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Get the name constant for a VAR_* opcode. Borrowed reference.
*/
static PyObject *get_name_from_insn(KosCode *code, KosOp *insn) {
  return KosCode_GetConstant((PyObject *) code, KosOp_ARG(insn));
}


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
  if (KosNamespace_Check(ns))
    return KosNamespace_Deref(ns, name);
  PyErr_Format(PyExc_TypeError, "frame namespace must be Namespace or dict,"
               " got %R", ns);
  return NULL;
}


/**
   Store value into name in the current frame namespace. Caller retains
   ownership of val.
*/
static int frame_ns_store(KosEvalState *es, PyObject *name, PyObject *val) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Store(ns, name, val);

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Bind name to reference in the current frame namespace.
*/
static int frame_ns_bind(KosEvalState *es, PyObject *name, PyObject *ref) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Bind(ns, name, ref);

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Clear name from the current frame namespace.
*/
static int frame_ns_clear(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
  if (KosNamespace_Check(ns))
    return KosNamespace_Clear(ns, name);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Get reference for name in the current frame namespace. Namespace only.
   Borrowed reference; caller must INCREF if keeping.
*/
static PyObject *frame_ns_ref(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace for VAR_REF, got %R", ns);
    return NULL;
  }
  return KosNamespace_Ref(ns, name);
}


/* === Opcode handlers (always inlined; return KosOpOutcome via KOS_OP_* macros) === */


// uncomment the following when we need this stub again.
/*
static int op_unimplemented_stub(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  PyErr_Format(PyExc_RuntimeError, "unimplemented opcode %u",
               (unsigned) KosOp_OP(insn));
  return -1;
}
*/


/*
   Opcode families (for quick navigation):
   NOOP, LABEL, POSITION - no-op / debug
   CONST, DUP, POP - stack
   RETURN, AWAIT - control
   CALL, CALL_*, TAIL_* - calls
   GLOBALS, LOCALS - namespaces
   VAR_*, FAST_* - variable access
   EXCEPTION_* - exceptions
   CHECK_* - comparison / jump
   OBJECT_* - object protocol
   PUSH_*, POP_* - blocks, regions, NS
   SHIFT, CALL on DelimitedContinuation, prompt - continuations
 */


static int kos_await_dispatch(KosEvalState *es, KosEvent *out_ev, PyObject *value) {
  return push_or_await_common(es, out_ev, value, 1);
}


/* === CALL === */


/**
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/


/**
   Reducing operator: pop opa values (or use TailPtr for opa > 0), call fn,
   drop opa, push result. Faithful to ceval _obj_op.
 */
static int obj_op_reducing(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    KosOperatorReducing *fn) {

  int opa = (int) KosOp_ARG(insn);
  PyObject *res;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "Negative count argument to reducing operator: %i", opa);
    return -1;
  }

  if (opa == 0) {
    res = fn(NULL, 0);
  } else {
    PyObject *const *args = KosEvalState_StackTailPtr(es, (size_t) opa);
    if (! args)
      return -1;
    res = fn(args, (size_t) opa);
    KosEvalState_StackDropNoErr(es, (size_t) opa);
  }

  if (! res)
    return -1;
  return push_result(es, res);
}


static int obj_op_binary(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    PyObject *(*fn)(PyObject *left, PyObject *right)) {

  KosEvalStackLR op = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, op))
    return -1;
  PyObject *res = fn(op.left, op.right);
  Py_DECREF(op.left);
  Py_DECREF(op.right);
  if (! res)
    return -1;
  (void) insn;
  return push_result(es, res);
}


static PyObject *binary_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_not_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  PyObject *res = (r ? Py_False : Py_True);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_is_result(PyObject *left, PyObject *right) {
  PyObject *res = (left == right ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_is_not_result(PyObject *left, PyObject *right) {
  PyObject *res = (left != right ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


/* Pop value then container; PySequence_Contains(container, value). Branch to
   KosOp_ARG(insn) when cmp == branch_when (CHECK_IN: 0, CHECK_NOT_IN: 1). */
static int kos_check_contains(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    int branch_when) {

  KosEvalStackContains args = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, args))
    return -1;

  int cmp = PySequence_Contains(args.container, args.value);
  Py_DECREF(args.value);
  Py_DECREF(args.container);
  if (unlikely(cmp < 0))
    return -1;

  if (cmp == branch_when) {
    if (KosEvalState_SetFramePc(es, KosOp_ARG(insn)) < 0)
      return -1;
  }

  (void) out_ev;
  return 0;
}


/* Pop right then left; branch to KosOp_ARG(insn) when (left == right) == branch_when_equal
   (CHECK_IS: 0 so branch when not equal; CHECK_IS_NOT: 1 so branch when equal). */
static int kos_check_identity(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    int branch_when_equal) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int cmp = (lr.left == lr.right);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);
  if (cmp == branch_when_equal) {
    if (KosEvalState_SetFramePc(es, KosOp_ARG(insn)) < 0)
      return -1;
  }

  (void) out_ev;
  return 0;
}


/* Pop right then left; call check(left, right). Branch to KosOp_ARG(insn) when res == 0. */
static int kos_check_two_arg(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    int (*check)(PyObject *, PyObject *)) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int res = check(lr.left, lr.right);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);
  if (unlikely(res < 0))
    return -1;

  if (res == 0) {
    if (KosEvalState_SetFramePc(es, KosOp_ARG(insn)) < 0)
      return -1;
  }

  (void) out_ev;
  return 0;
}


/* Helper for CHECK_EQ/GE/GT/LE/LT/NE: pop right then left, RichCompareBool;
   jump to KosOp_ARG(insn) if comparison fails (res == 0). */
static int kos_check_cmp(
    KosEvalState *es,
    KosEvent *out_ev,
    KosOp *insn,
    int cmp_op) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int res = PyObject_RichCompareBool(lr.left, lr.right, cmp_op);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);

  if (res < 0)
    return -1;

  if (res == 0) {
    if (KosEvalState_SetFramePc(es, KosOp_ARG(insn)) < 0)
      return -1;
  }

  (void) out_ev;
  return 0;
}


/* === THEEND, BREAK, CONTINUE, YIELD, YIELD_FROM === */

/* THEEND: no runtime implementation; hardcoded error (not a "stub" to be implemented). */

/**
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/


typedef enum {
  GET_NEXT_VALUE,
  GET_NEXT_EXHAUSTED,
  GET_NEXT_SUSPEND,
  GET_NEXT_ERROR
} KosGetNextResult;


/**
   Get next value from a Python async iterator using the given __anext__
   awaitable. Caller must have obtained awaitable via get_await_next(iter);
   this helper consumes it (DECREF). *result_out is set with a new
   reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_async_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out,
    PyObject *awaitable) {

  (void) iter;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(awaitable, ctx, &suspended,
                                          &yielded);
  Py_DECREF(awaitable);

  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    return GET_NEXT_SUSPEND;
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return GET_NEXT_EXHAUSTED;
  }

  if (result) {
    *result_out = result;
    return GET_NEXT_VALUE;
  }

  return GET_NEXT_ERROR;
}


/**
   Get next value from a Python sync iterator (PyIter_Next).
   *result_out is set with a new reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_sync_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  (void) ctx;
  (void) out_ev;

  PyObject *n = PyIter_Next(iter);
  if (n) {
    *result_out = n;
    return GET_NEXT_VALUE;
  }

  if (PyErr_Occurred())
    return GET_NEXT_ERROR;

  return GET_NEXT_EXHAUSTED;
}


/**
   Dispatcher: get next from iter (Koskari async gen, Python async iter,
   or Python sync iter). Does not push; does not perform GetIter.
   *result_out set only on GET_NEXT_VALUE (caller pushes and DECREF).
*/
static KosGetNextResult op_get_next_from_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(ctx, out_ev, iter,
                                                      result_out, awaitable);
    return r;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(ctx, out_ev, iter, result_out);

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


enum {
  KOS_ATTACH_AGEN_MODE_NEXT = 0,
  KOS_ATTACH_AGEN_MODE_ITER_NEXT = 1,
  KOS_ATTACH_AGEN_MODE_YIELD_FROM = 2,
};


/**
   Shared install of Koskari async gen frame onto consumer ctx.  mode NEXT
   pops KOS_DYN_NEXT and DECREFs iter/default_val on failure; ITER_NEXT clears
   FOREACH iter_step_phase on failure; YIELD_FROM pops KOS_DYN_YIELD_FROM on
   failure.

   es must be the consumer's live interpreter KosEvalState; KosEvalState_CONTEXT
   (es) is the context onto which the async gen is flattened
*/
static int attach_koskari_gen_core(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    int mode,
    PyObject *iter,
    PyObject *default_val,
    KosDynForeach *iter_next_region,
    KosDynYieldFrom *yf_boundary) {

  KosDynGen *gen = (KosDynGen *) agen->gen;

  if (! gen || ! KosDynGen_ACTIVE(gen)) {
    if (mode == KOS_ATTACH_AGEN_MODE_NEXT) {
      kos_dyn_next_abort_after_failed_attach(es);
      Py_DECREF(iter);
      Py_XDECREF(default_val);
    } else if (mode == KOS_ATTACH_AGEN_MODE_ITER_NEXT && iter_next_region) {
      KosDynForeach_ITER_STEP_PHASE(iter_next_region) =
          KOS_FOREACH_ITER_STEP_IDLE;
    } else if (mode == KOS_ATTACH_AGEN_MODE_YIELD_FROM && yf_boundary) {
      kos_dyn_pop_yield_from(es, yf_boundary);
    }
    agen->in_progress = 0;
    PyErr_SetString(PyExc_RuntimeError, "async generator has no active frame");
    return -1;
  }

  KosContext_DYN_TOP(agen->ctx) = NULL;
  KosContext_FRAME_TOP(agen->ctx) = NULL;

  if (kos_dyn_gen_install(es, gen) < 0) {
    if (mode == KOS_ATTACH_AGEN_MODE_NEXT) {
      kos_dyn_next_abort_after_failed_attach(es);
      Py_DECREF(iter);
      Py_XDECREF(default_val);
    } else if (mode == KOS_ATTACH_AGEN_MODE_ITER_NEXT && iter_next_region) {
      KosDynForeach_ITER_STEP_PHASE(iter_next_region) =
          KOS_FOREACH_ITER_STEP_IDLE;
    } else if (mode == KOS_ATTACH_AGEN_MODE_YIELD_FROM && yf_boundary) {
      kos_dyn_pop_yield_from(es, yf_boundary);
    }
    agen->in_progress = 0;
    return -1;
  }

  return 0;
}


/**
   Install GEN onto consumer ctx (Koskari flatten). Clears agen->ctx chain
   so state lives only in GEN. kos_dyn_gen_install pushes Py_None.
   Caller must have already: checked in_progress, handled closed/ctx.
   Caller must have pushed KOS_DYN_NEXT before calling (NEXT opcode).
   On error: aborts NEXT node, resets in_progress; for NEXT mode also
   DECREFs iter and default_val.
*/
static int op_attach_koskari_agen_frame(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    PyObject *iter,
    PyObject *default_val) {

  return attach_koskari_gen_core(es, agen, KOS_ATTACH_AGEN_MODE_NEXT,
                                 iter, default_val, NULL, NULL);
}


/**
   ITER_NEXT variant: install GEN onto consumer ctx. On error clear
   FOREACH iter_step_phase.
*/
static int op_attach_koskari_agen_frame_for_iter_next(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynNode *region) {

  KosDynForeach *fe = (KosDynNode_IS_FOREACH(region))
                          ? (KosDynForeach *) region
                          : NULL;
  return attach_koskari_gen_core(es, agen, KOS_ATTACH_AGEN_MODE_ITER_NEXT,
                                 NULL, NULL, fe, NULL);
}


/**
   YIELD_FROM: install delegate Koskari async gen onto consumer ctx.  dyn_top
   must be KOS_DYN_YIELD_FROM.  On error pops yield-from node.
*/
static int op_attach_koskari_agen_frame_for_yield_from(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynYieldFrom *yf) {

  return attach_koskari_gen_core(es, agen, KOS_ATTACH_AGEN_MODE_YIELD_FROM,
                                 NULL, NULL, NULL, yf);
}


/* === NEXT, ITER_NEXT === */


/* Yield-from delegate kinds (must match normalizer and handler). */
enum {
  KOS_YF_SYNC_ITER = 0,
  KOS_YF_SYNC_GEN = 1,
  KOS_YF_PY_ASYNC_ITER = 2,
  KOS_YF_PY_ASYNC_GEN = 3,
  KOS_YF_KOS_ASYNC_GEN = 4,
};


/* Yield-from pending action (STEP / SEND / THROW / CLOSE). */
enum {
  KOS_YF_STEP = 0,
  KOS_YF_SEND = 1,
  KOS_YF_THROW = 2,
  KOS_YF_CLOSE = 3,
};


void kos_yieldfrom_close_delegate(KosDynYieldFrom *node) {

  if (! node)
    return;

  PyObject *delegate = KosDynYieldFrom_DELEGATE(node);
  if (! delegate)
    return;

  uint8_t kind = KosDynYieldFrom_KIND(node);
  PyObject *inflight = KosDynYieldFrom_INFLIGHT(node);

  switch (kind) {
  case KOS_YF_SYNC_ITER:
    break;

  case KOS_YF_SYNC_GEN: {
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_close, NULL);
    Py_XDECREF(res);
    PyErr_Clear();
    break;
  }

  case KOS_YF_PY_ASYNC_ITER:
  case KOS_YF_PY_ASYNC_GEN:
    Py_XDECREF(inflight);
    KosDynYieldFrom_INFLIGHT(node) = NULL;
    {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, str_aclose, NULL);
      Py_XDECREF(res);
      PyErr_Clear();
    }
    break;

  case KOS_YF_KOS_ASYNC_GEN:
    if (KosPyAsyncGen_Check(delegate))
      ((KosPyAsyncGen *) delegate)->closed = 1;
    break;

  default:
    break;
  }
}


/*
  Includes the implementation of all opcodes from the opcode/ directory.
*/
#define KOS_OP_HANDLER(name)                                            \
  KOS_OP_INLINE KosOpOutcome op_##name(KosEvalState *es, KosEvent *out_ev, \
                                       KosOp *insn)
#define CURRENT_NS() KosDynFrame_NS(KosEvalState_FRAME(es))
#include "handlers.incl"
#undef CURRENT_NS
#undef KOS_OP_HANDLER


/**
   Try to handle an exception locally by finding a TRY or FINALLY region
   and jumping to its handler.  Call when PyErr_Occurred() is set.

   Delegates to kos_dyn_transition(KOS_TRANS_TRY_LOCAL_RAISE).

   :param ctx: execution context
   :param out_ev: event to fill if no handler is found

   :return: 1 if a handler was applied (caller should continue the loop),
            0 if no handler (out_ev set to KOS_EV_RAISE, caller should return),
            -1 on error (PyErr set)
*/
static int try_local_exception_handler(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosTransitionReq req = { .kind = KOS_TRANS_TRY_LOCAL_RAISE };
  KosTransitionResult res = {0};

  if (kos_dyn_transition(es, &req, &res, out_ev) < 0)
    return -1;
  if (res.status == KOS_TRANS_STATUS_HANDLER_APPLIED)
    return 1;
  if (res.status == KOS_TRANS_STATUS_NO_LOCAL_HANDLER)
    return 0;
  PyErr_SetString(PyExc_RuntimeError,
                  "try_local_exception_handler: unexpected transition status");
  return -1;
}


/**
   Invoke native frame handler when the eval frame has NATIVE_HANDLER.
   Returns handler result, or -1 if no handler (callers usually guard).
*/
static int kos_eval_call_native_handler(
    KosEvalState *es,
    KosEvent *out_ev,
    int phase,
    PyObject *arg) {

  KosDynFrame *frame = KosEvalState_FRAME(es);
  if (! frame || ! KosDynFrame_NATIVE_HANDLER(frame))
    return -1;

  typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
  NativeHandlerFn fn =
      (NativeHandlerFn) KosDynFrame_NATIVE_HANDLER(frame);

  KosContext *ctx = KosEvalState_CONTEXT(es);
  return fn(ctx, out_ev, phase, arg);
}


/**
   Phase 1: entry and revive normalization.

   Determines the initial KosEvalMode by dispatching through the current
   suspension state and send_val/throw_exc arguments.  Handles the
   EXCEPTION and RAISE outcomes in-place (filling out_ev and flagging the
   context dead).  Clears KosContext_SUSPENSION_EVENT after mode selection.

   :param mode_out: Receives the resolved KosEvalMode on KOS_STEP_CONTINUE.
   :return: KOS_STEP_CONTINUE when the loop should proceed;
            KOS_STEP_STOP_EVENT when out_ev is filled and eval should return.
*/
static KosEvalStep eval_phase_entry(
        KosEvalState *es,
        KosEvent *out_ev,
        PyObject *send_val,
        PyObject *throw_typ,
        PyObject *throw_exc,
        KosEvalMode *mode_out) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosEvalMode mode = KOS_MODE_NORMAL;
  if (KosContext_PLAN(ctx)) {
    mode = revive_plan(es, send_val, throw_typ, throw_exc);
    if (mode != KOS_MODE_NORMAL && mode != KOS_MODE_TRANSFER)
      goto entry_mode_dispatch;

  } else if (send_val == NULL && throw_typ == NULL && throw_exc == NULL) {
    SAYF("[eval] phase_entry branch: send/throw all NULL, suspend=%d\n",
         KosContext_SUSPENSION_EVENT(ctx));
    switch (KosContext_SUSPENSION_EVENT(ctx)) {
      case KOS_SUSP_NONE:
        mode = KOS_MODE_NORMAL;
        break;
      case KOS_SUSP_AWAIT:
        if (KosContext_PY_AWAIT_ITER(ctx)) {
          mode = KOS_MODE_NORMAL;
        } else {
          mode = revive_await(es, Py_None, NULL, NULL, out_ev);
        }
        break;
      case KOS_SUSP_YIELD:
        /* anext() passes NULL for send; treat as Py_None per async gen protocol */
        SAY("[eval] phase_entry -> revive_yield(Py_None)");
        mode = revive_yield(es, Py_None, throw_typ, throw_exc);
        break;
      default:
        mode = KOS_MODE_NORMAL;
        break;
    }

  } else {
    SAYF("[eval] phase_entry branch: send_val=%p throw_typ=%p throw_exc=%p "
         "suspend=%d\n",
         (void *) send_val, (void *) throw_typ, (void *) throw_exc,
         KosContext_SUSPENSION_EVENT(ctx));
    switch (KosContext_SUSPENSION_EVENT(ctx)) {
      case KOS_SUSP_AWAIT:
        mode = revive_await(es, send_val, throw_typ, throw_exc, out_ev);
        break;
      case KOS_SUSP_YIELD:
        SAY("[eval] phase_entry -> revive_yield(send_val)");
        mode = revive_yield(es, send_val, throw_typ, throw_exc);
        break;
      default:
        mode = revive_await(es, send_val, throw_typ, throw_exc, out_ev);
        break;
    }
  }
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;

entry_mode_dispatch:
  if (mode == KOS_MODE_AWAIT) {
    KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    return KOS_STEP_STOP_EVENT;
  }

  if (mode == KOS_MODE_EXCEPTION) {
    int tl;
    clear_kos_agen_boundary_on_error(ctx);
    tl = try_local_exception_handler(es, out_ev);
    SAYF("[eval] phase_entry try_local ret=%d out_kind=%d\n",
         tl, (int) KosEvent_KIND(out_ev));

    if (tl > 0) {
      if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
        Py_XDECREF(KosEvent_PAYLOAD(out_ev));
        KosEvent_PAYLOAD(out_ev) = NULL;
        Py_XDECREF(KosEvent_EXC_TYPE(out_ev));
        KosEvent_EXC_TYPE(out_ev) = NULL;
        KosEvent_KIND(out_ev) = KOS_EV_NONE;
      }
      *mode_out = KOS_MODE_NORMAL;
      return KOS_STEP_CONTINUE;
    }

    if (tl < 0)
      return KOS_STEP_FATAL_ERROR;
    /* try_local_exception_handler returning 0 has already filled out_ev with
       KOS_EV_RAISE and the exception payload, and cleared PyErr.  Just mark
       the context dead and stop; do not call PyErr_Fetch again. */
    kos_eval_mark_context_dead(ctx);
    return KOS_STEP_STOP_EVENT;
  }

  if (mode == KOS_MODE_RAISE) {
    kos_eval_emit_raise_from_pyerr(ctx, out_ev);
    return KOS_STEP_STOP_EVENT;
  }

  /* Resume delivery: if we revived with a value and top frame has native_handler, deliver to it */
  if (send_val != NULL
      && mode == KOS_MODE_NORMAL
      && KosEvalState_FRAME(es)
      && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es))) {

    PyObject *val = KosEvalState_StackPopRef(es);
    if (val) {
      int res = kos_eval_call_native_handler(es, out_ev, 1, val);
      if (res < 0)
        return KOS_STEP_FATAL_ERROR;
      if (res == 1)
        return KOS_STEP_STOP_EVENT;
    }
  }

  *mode_out = mode;
  return KOS_STEP_CONTINUE;
}


/**
   Phase 2: transfer-plan synchronization.

   Drains any active transfer plan to completion.  When no frame and no
   hook remain after draining, emits a terminal KOS_EV_RETURN(None) and
   marks the context dead.

   :return: KOS_STEP_CONTINUE when the dispatch loop should start;
            KOS_STEP_STOP_EVENT when out_ev is filled and eval should return.
*/
static KosEvalStep eval_phase_transfer(KosEvalState *es, KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (run_transfer_plan_to_completion(es, out_ev) < 0)
    return KOS_STEP_STOP_EVENT;

  if (! KosEvalState_FRAME(es)) {
    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = Py_None;
    Py_INCREF(Py_None);
    kos_eval_mark_context_dead(ctx);
    return KOS_STEP_STOP_EVENT;
  }

  return KOS_STEP_CONTINUE;
}


/**
   Single PyErr / RAISE ordering contract after process_raise: propagate RAISE,
   resuspend on AWAIT, or REPROLOGUE after local recovery.
*/
static KosEvalStep eval_boundary_raise_dispatch(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosEvalMode raise_mode = process_raise(es, out_ev);
  if (raise_mode == KOS_MODE_RAISE) {
    if (PyErr_Occurred())
      return KOS_STEP_FATAL_ERROR;
    return KOS_STEP_STOP_EVENT;
  }

  if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
    KosContext *ctx = KosEvalState_CONTEXT(es);
    KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
    return KOS_STEP_STOP_EVENT;
  }

  return KOS_STEP_REPROLOGUE;
}


/**
   Post-opcode / entry normalization for active PyErr: delegates to
   eval_boundary_raise_dispatch (process_raise and suspension tagging).
   See design/interpreter/event-driven-eval-event-handlers.md (eval_event_raise).
*/
static KosEvalStep eval_event_raise(KosEvalState *es, KosEvent *out_ev) {
  return eval_boundary_raise_dispatch(es, out_ev);
}


/**
   Phase 3: async exception check at each loop iteration top.

   When PyErr_Occurred() is set on entry to the dispatch loop (e.g. from a
   signal handler or a mid-loop async exception), calls process_raise to
   attempt local handling.

   :return: KOS_STEP_STOP_EVENT when out_ev is filled and eval should return;
            KOS_STEP_REPROLOGUE when handled locally (restart prologue);
            KOS_STEP_CONTINUE when no PyErr.
*/
static KosEvalStep eval_phase_pending_exception(
        KosEvalState *es,
        KosEvent *out_ev) {

  if (! PyErr_Occurred())
    return KOS_STEP_CONTINUE;

  return eval_event_raise(es, out_ev);
}


/**
   YIELD half of dispatch_boundary_event. Caller already ran
   KOS_TRANS_WALK_HANDLE_YIELD.
*/
static KosEvalStep eval_event_yield_deliver_to_boundary(
    KosEvalState *es, KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNext *next = kos_dyn_find_next_above_frame(es);

  if (next) {
    if (kos_dyn_next_deliver_yield(es, out_ev) < 0)
      return KOS_STEP_FATAL_ERROR;
    return KOS_STEP_REPROLOGUE;
  }
  {
    KosDynForeach *fe = kos_dyn_find_awaiting_foreach(es);

    if (fe) {
      if (kos_dyn_foreach_deliver_yield_kos_agen(es, fe, out_ev) < 0)
        return KOS_STEP_FATAL_ERROR;
      return KOS_STEP_REPROLOGUE;
    }
  }
  {
    KosDynYieldFrom *yf = kos_dyn_find_yield_from_iter_boundary(es);

    if (yf) {
      if (kos_dyn_yield_from_deliver_inner_yield(es, yf, out_ev) < 0)
        return KOS_STEP_FATAL_ERROR;
      KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_YIELD;
      return KOS_STEP_STOP_EVENT;
    }
  }
  PyErr_SetString(PyExc_RuntimeError,
                  "eval: boundary YIELD without NEXT/FOREACH/YIELD_FROM "
                  "Koskari agen state");
  return KOS_STEP_FATAL_ERROR;
}


/**
   Shared step mapping for kos_dyn_deliver_iterator_boundary_return_event.
*/
static KosEvalStep eval_deliver_iterator_boundary_return_step(
    KosEvalState *es, KosEvent *out_ev) {

  int dr = kos_dyn_deliver_iterator_boundary_return_event(es, out_ev);

  if (dr < 0) {
    if (PyErr_Occurred())
      return KOS_STEP_FATAL_ERROR;
    return KOS_STEP_REPROLOGUE;
  }
  return KOS_STEP_REPROLOGUE;
}


/**
   RETURN half: deliver KOS_EV_RETURN payload to NEXT or FOREACH consumer.
*/
static KosEvalStep eval_event_return_deliver_to_boundary(
    KosEvalState *es, KosEvent *out_ev) {

  return eval_deliver_iterator_boundary_return_step(es, out_ev);
}


static KosEvalStep eval_parent_return_deliver(KosEvalState *es, KosEvent *out_ev);

static KosEvalStep eval_phase_post_opcode(
        KosEvalState *es,
        KosEvent *out_ev,
        int op_res);


/**
   Stage A of post-opcode YIELD: transition walk only (freeze is walk side).
*/
static KosEvalStep eval_yield_handle_walk_and_freeze(
    KosEvalState *es, KosEvent *out_ev) {

  KosTransitionReq treq = { .kind = KOS_TRANS_WALK_HANDLE_YIELD };
  KosTransitionResult tres = {0};

  if (kos_dyn_transition(es, &treq, &tres, out_ev) < 0)
    return KOS_STEP_FATAL_ERROR;
  return KOS_STEP_CONTINUE;
}


/**
   Post-opcode handling for KOS_EV_YIELD: walk_handle_yield, optional flattened
   agen boundary delivery, else tag suspension for outer koskari_eval.
*/
static KosEvalStep eval_event_yield(KosEvalState *es, KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  SAYF("[eval] post_opcode KOS_EV_YIELD -> walk_handle_yield dyn_top=%p\n",
       (void *) KosContext_DYN_TOP(ctx));
  {
    KosEvalStep ws = eval_yield_handle_walk_and_freeze(es, out_ev);

    if (ws != KOS_STEP_CONTINUE)
      return ws;
  }
  if (kos_dyn_need_kos_agen_boundary_deliver(es))
    return eval_event_yield_deliver_to_boundary(es, out_ev);
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_YIELD;
  return KOS_STEP_STOP_EVENT;
}


/**
   Tail of post-opcode handling after CONTROL_RETURNED hook (or equivalent).
*/
static KosEvalStep eval_phase_post_opcode_tail(
        KosEvalState *es,
        KosEvent *out_ev,
        int op_res) {

  if (PyErr_Occurred()) {
    KosEvalStep rs;

    KOS_TRACE_POST_OP_WRAP(event_raise, rs = eval_event_raise(es, out_ev));
    return rs;
  }

  /* Record why we suspended so next entry can choose revive path.
     post_opcode is only called for non-zero op_res; a handler that stops
     or suspends must set kind. Uninitialized {0} must not masquerade here. */
  if (KosEvent_KIND(out_ev) == KOS_EV_NONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "eval post_opcode: KOS_EV_NONE with non-zero opcode result");
    return KOS_STEP_FATAL_ERROR;
  }

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
    KOS_TRACE_POST_OP_WRAP(event_await,
        KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT);

  } else if (KosEvent_KIND(out_ev) == KOS_EV_YIELD) {
    KosEvalStep ys;

    KOS_TRACE_POST_OP_WRAP(event_yield, ys = eval_event_yield(es, out_ev));

    if (ys != KOS_STEP_STOP_EVENT)
      return ys;

  } else if (KosEvent_KIND(out_ev) == KOS_EV_RETURN
             && kos_dyn_need_kos_agen_boundary_deliver(es)) {
    KosEvalStep bs;

    KOS_TRACE_POST_OP_WRAP(return_boundary,
        bs = eval_event_return_deliver_to_boundary(es, out_ev));
    return bs;
  }

  /* op_res < 0 with no PyErr and no suspension event is a fatal internal error */
  return (op_res < 0) ? KOS_STEP_FATAL_ERROR : KOS_STEP_STOP_EVENT;
}


/**
   Native parent-return path defers a second eval_phase_post_opcode pass by
   filling out_ev with KOS_EV_POST_OPCODE and a PyLong op_res; the dispatch
   prologue drains it before native_iteration.
*/
static KosEvalStep eval_emit_post_opcode_deferred(KosEvent *out_ev, int pr) {

  PyObject *tag = PyLong_FromLong((long) pr);

  if (! tag)
    return KOS_STEP_FATAL_ERROR;
  Py_CLEAR(KosEvent_EXC_TYPE(out_ev));
  Py_XDECREF(KosEvent_PAYLOAD(out_ev));
  KosEvent_PAYLOAD(out_ev) = tag;
  KosEvent_KIND(out_ev) = KOS_EV_POST_OPCODE;
  return KOS_STEP_REPROLOGUE;
}


/**
   After RETURN opcode pop: iterator boundary, native handler or stack push,
   then kos_dynnode_on_control_returned (WIND/WITH).
*/
static KosEvalStep eval_parent_return_deliver_impl(KosEvalState *es, KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  PyObject *val = KosEvent_PAYLOAD(out_ev);

  {
    int bd = kos_dyn_try_deliver_iterator_boundary_after_pop(es, &val, out_ev);

    if (bd == 0)
      return KOS_STEP_REPROLOGUE;
    if (bd < 0)
      return KOS_STEP_FATAL_ERROR;
  }

  KosEvent_KIND(out_ev) = KOS_EV_RETURN;
  KosEvent_PAYLOAD(out_ev) = val;

  if (unlikely(KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL)) {
    typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
    NativeHandlerFn fn =
        (NativeHandlerFn) KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es));
    int res = fn(ctx, out_ev, 1, val);

    Py_DECREF(val);
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_KIND(out_ev) = KOS_EV_NONE;
    if (res < 0)
      return eval_emit_post_opcode_deferred(out_ev, -1);
    if (res == 0)
      return KOS_STEP_REPROLOGUE;
    if (res == 1)
      return eval_emit_post_opcode_deferred(out_ev, 1);
    if (res == KOS_OP_RES_CONTROL_RETURNED)
      return eval_emit_post_opcode_deferred(out_ev, KOS_OP_RES_CONTROL_RETURNED);
    return KOS_STEP_REPROLOGUE;
  }

  if (KosEvalState_StackPushRef(es, val) < 0) {
    Py_DECREF(val);
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_KIND(out_ev) = KOS_EV_NONE;
    return KOS_STEP_FATAL_ERROR;
  }

  Py_DECREF(val);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;
  {
    int r;

    KOS_TRACE_POST_OP_WRAP(control_returned, r = kos_dynnode_on_control_returned(
        es, KosContext_DYN_TOP(ctx), out_ev));

    if (r == 0 || r == KOS_DYN_UNWIND_ASYNC_STARTED)
      return KOS_STEP_REPROLOGUE;
  }

  return eval_phase_post_opcode_tail(es, out_ev, KOS_OP_RES_CONTROL_RETURNED);
}


static KosEvalStep eval_parent_return_deliver(KosEvalState *es, KosEvent *out_ev) {

  KosEvalStep ret;

  KOS_TRACE_POST_OP_WRAP(parent_return,
      ret = eval_parent_return_deliver_impl(es, out_ev));
  return ret;
}


/**
   Phase 6: post-opcode result handling.

   Called when an opcode handler returns a non-zero op_res.  Handles the
   exception path (via try_local_exception_handler) and suspension-event
   tagging for AWAIT and YIELD events.

   :param op_res: The non-zero return value from the opcode handler.

   :return: KOS_STEP_FATAL_ERROR when op_res < 0 and no exception/event
    (caller uses eval_ret -1); KOS_STEP_STOP_EVENT when out_ev is filled and
    eval should return (eval_ret 0); KOS_STEP_REPROLOGUE when the dispatch
    loop should restart the full prologue without surfacing a boundary.
*/
static KosEvalStep eval_phase_post_opcode(
        KosEvalState *es,
        KosEvent *out_ev,
        int op_res) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosEvalStep ret;

  KOS_TRACE_POST_OP_ENTER();

  /* Clear resume kind when handling non-zero result so next iteration (if we
     continue) has it clean. Keeps the hot path (tail-dispatch) from touching it. */
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  if (op_res == KOS_OP_RES_PARENT_RETURN_DELIVER) {
    ret = eval_parent_return_deliver(es, out_ev);
    goto post_opcode_done;
  }

  if (op_res == KOS_OP_RES_CONTROL_RETURNED) {
    int r;

    KOS_TRACE_POST_OP_WRAP(control_returned, r = kos_dynnode_on_control_returned(
        es, KosContext_DYN_TOP(ctx), out_ev));
    if (r == 0 || r == KOS_DYN_UNWIND_ASYNC_STARTED) {
      /* 0: thunk/wind advanced; 1: async extent started (same numeric value as
         a normal opcode "suspend" but only returned from dyn helpers). */
      ret = KOS_STEP_REPROLOGUE;
      goto post_opcode_done;
    }
  }

  ret = eval_phase_post_opcode_tail(es, out_ev, op_res);
post_opcode_done:
  KOS_TRACE_POST_OP_EXIT();
  return ret;
}


/**
   Drain KOS_EV_POST_OPCODE (deferred eval_phase_post_opcode from native
   parent-return).  Runs at dispatch prologue before native_iteration.
*/
static KosEvalStep eval_dispatch_maybe_post_opcode_event(
    KosEvalState *es,
    KosEvent *out_ev,
    int *eval_ret) {

  PyObject *pl;
  long pr_long;
  int pr;
  KosEvalStep ps;

  if (KosEvent_KIND(out_ev) != KOS_EV_POST_OPCODE)
    return KOS_STEP_CONTINUE;

  pl = KosEvent_PAYLOAD(out_ev);
  if (! pl || ! PyLong_Check(pl)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "eval: KOS_EV_POST_OPCODE requires PyLong payload");
    return KOS_STEP_FATAL_ERROR;
  }

  pr_long = PyLong_AsLong(pl);
  if (pr_long == -1 && PyErr_Occurred())
    return KOS_STEP_FATAL_ERROR;

  Py_DECREF(pl);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  pr = (int) pr_long;

  ps = eval_phase_post_opcode(es, out_ev, pr);
  if (ps == KOS_STEP_FATAL_ERROR) {
    if (eval_ret)
      *eval_ret = -1;
    return ps;
  }

  if (ps == KOS_STEP_STOP_EVENT) {
    if (eval_ret)
      *eval_ret = 0;
    return ps;
  }

  return ps;
}


/**
   Top-of-loop transfer drain.  STOP_EVENT means caller should return eval 0.
*/
static KosEvalStep eval_step_drain_transfer_plan(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! KosContext_PLAN(ctx))
    return KOS_STEP_CONTINUE;
  if (run_transfer_plan_to_completion(es, out_ev) < 0)
    return KOS_STEP_STOP_EVENT;
  return KOS_STEP_CONTINUE;
}


/**
   One native-frame iteration: run handler; on non-zero result, post_opcode.
   Caller must only invoke when the eval frame has NATIVE_HANDLER.
   Sets *eval_ret on FATAL (-1) or STOP (0).
*/
static KosEvalStep eval_step_native_iteration(
    KosEvalState *es,
    KosEvent *out_ev,
    int *eval_ret) {

  int op_res;
  KosEvalStep ps;

  op_res = kos_eval_call_native_handler(es, out_ev, 0, NULL);

  if (op_res == 0)
    return KOS_STEP_REPROLOGUE;

  ps = eval_phase_post_opcode(es, out_ev, op_res);
  if (ps == KOS_STEP_FATAL_ERROR)
    *eval_ret = -1;
  else if (ps == KOS_STEP_STOP_EVENT)
    *eval_ret = 0;

  return ps;
}


static void kos_eval_report_pc_bounds_error(KosEvalState *es) {

  KosDynFrame *rep_pf = KosEvalState_FRAME(es);

  if (! rep_pf) {
    PyErr_SetString(PyExc_SystemError,
                    "koskari eval: no interpreter frame during "
                    "instruction fetch");
    return;
  }

  KosCode *rep_code = KosDynFrame_CODE(rep_pf);
  if (! rep_code)
    rep_code = KosEvalState_CODE(es);
  uint32_t rep_pc = es->pc;

  if (! rep_code) {
    PyErr_Format(PyExc_SystemError,
                 "koskari eval: frame has no bytecode (pc=%u)",
                 (unsigned) rep_pc);
    return;
  }

  uint32_t rep_len = KosCode_CODE_LEN(rep_code);

  if (rep_pc >= rep_len) {
    PyErr_Format(PyExc_SystemError,
                  "koskari eval: program counter out of bounds "
                  "(pc=%u, len=%u)",
                  (unsigned) rep_pc,
                  (unsigned) rep_len);
    return;
  }

  PyErr_SetString(PyExc_SystemError,
                  "koskari eval: instruction fetch failed");
}


/* Dispatch prelude: frame and pc from es (dual-written with heap frame).
   Code from frame first, then es->code (tail reset may clear frame code
   before Sync updates es->code).  On OOB, goto on_bad. */
#define KOS_EVAL_PREPARE_NEXT_INSN(es, insn_ptr, on_bad)                   \
  do {                                                                     \
    KosDynFrame *_kos_pf = KosEvalState_FRAME(es);                         \
    if (unlikely(! _kos_pf))                                               \
      goto on_bad;                                                         \
    KosCode *_kos_code = KosDynFrame_CODE(_kos_pf);                         \
    if (unlikely(! _kos_code))                                             \
      _kos_code = KosEvalState_CODE(es);                                   \
    uint32_t _kos_pc = (es)->pc;                                           \
    if (unlikely(! _kos_code || _kos_pc >= KosCode_CODE_LEN(_kos_code))) { \
      goto on_bad;                                                         \
    }                                                                      \
    KosOp *_kos_insn = &KosCode_CODE(_kos_code)[_kos_pc];                   \
    SAYF("[eval] pc=%u op=%s arg=%u\n",                                    \
         (unsigned) _kos_pc, kos_opcode_name(KosOp_OP(_kos_insn)),         \
         (unsigned) KosOp_ARG(_kos_insn));                                 \
    (es)->acting_pc = _kos_pc;                                             \
    (es)->pc = _kos_pc + 1u;                                               \
    KosDynFrame_ACTING_PC(_kos_pf) = _kos_pc;                              \
    KosDynFrame_PC(_kos_pf) = _kos_pc + 1u;                                \
    *(insn_ptr) = _kos_insn;                                               \
  } while (0)


#if KOS_USE_COMPUTED_GOTO
/**
   Opcode loop for GCC/Clang: computed goto, tail-dispatch on RUN, prologue
   via cg_prologue.  Must be a single function: label addresses and optable
   live here only.
*/
static int koskari_eval_dispatch_computed_goto(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  #define OP(name, ...)  &&cg_TARGET_##name,
  static const void *const optable[] = {
    #include "opcodes.incl"
  };
  #undef OP

  KosOp *insn;
  KosOpOutcome op_out;
  int eval_ret = 0;

  goto cg_prologue;

cg_prologue:

  KosEvalState_SyncFromContext(es);

  #if KOS_STACK_TRACE
  {
    KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
    trace_stack("  after ",
                (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
  }
  #endif

  {
    KosEvalStep xfer = eval_step_drain_transfer_plan(es, out_ev);
    if (xfer == KOS_STEP_STOP_EVENT)
      return 0;
  }

  {
    KosEvalStep pend = eval_phase_pending_exception(es, out_ev);
    if (pend == KOS_STEP_STOP_EVENT)
      return 0;
    if (pend == KOS_STEP_REPROLOGUE)
      goto cg_prologue;
  }

  {
    KosEvalStep pp = eval_dispatch_maybe_post_opcode_event(es, out_ev, &eval_ret);
    if (pp == KOS_STEP_FATAL_ERROR)
      return eval_ret;
    if (pp == KOS_STEP_STOP_EVENT)
      return eval_ret;
    if (pp == KOS_STEP_REPROLOGUE)
      goto cg_prologue;
  }

  if (KosEvalState_FRAME(es)
      && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL) {
    KosEvalStep ns = eval_step_native_iteration(es, out_ev, &eval_ret);
    if (ns == KOS_STEP_FATAL_ERROR)
      return eval_ret;
    if (ns == KOS_STEP_STOP_EVENT)
      return eval_ret;
    goto cg_prologue;
  }

  #if KOS_STACK_TRACE
  {
    KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
    trace_stack("  before",
                (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
  }
  #endif

  KOS_EVAL_PREPARE_NEXT_INSN(es, &insn, cg_pc_bounds_error);
  goto *optable[KosOp_OP(insn)];

  #define OP(name, ...)                                                \
    cg_TARGET_##name:                                                  \
      KOS_TRACE_OP_WRAP(name, op_out = op_##name(es, out_ev, insn));    \
      if (likely(op_out.mode == KOS_LOP_RUN)) {                        \
        KOS_EVAL_PREPARE_NEXT_INSN(es, &insn, cg_pc_bounds_error);       \
        goto *optable[KosOp_OP(insn)];                                 \
      }                                                                \
      goto cg_handle_op_outcome;

  #include "opcodes.incl"
  #undef OP

cg_pc_bounds_error:
  kos_eval_report_pc_bounds_error(es);
  return -1;

cg_handle_op_outcome:

  if (op_out.mode == KOS_LOP_FAIL)
    return -1;

  if (op_out.mode == KOS_LOP_PROLOGUE)
    goto cg_prologue;

  if (op_out.mode == KOS_LOP_POST) {
    int op_res = kos_op_post_tag_to_res(op_out.post_tag);
    KosEvalStep ps = eval_phase_post_opcode(es, out_ev, op_res);
    if (ps == KOS_STEP_FATAL_ERROR)
      return -1;
    if (ps == KOS_STEP_STOP_EVENT)
      return 0;
    goto cg_prologue;
  }

  return eval_ret;
}

#else

/**
   Opcode loop without computed goto: for(;;), need_prologue, switch dispatch.
*/
static int
koskari_eval_dispatch_switch(KosEvalState *es, KosEvent *out_ev) {

  int eval_ret = 0;
  int need_prologue = 1;
  KosOp *insn = NULL;
  KosOpOutcome op_out;

  for (;;) {

  sw_loop_head:

    if (need_prologue) {

      KosEvalState_SyncFromContext(es);

      #if KOS_STACK_TRACE
      {
        KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
        trace_stack("  after ",
                    (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
      }
      #endif

      {
        KosEvalStep xfer = eval_step_drain_transfer_plan(es, out_ev);
        if (xfer == KOS_STEP_STOP_EVENT)
          return 0;
      }

      {
        KosEvalStep pend = eval_phase_pending_exception(es, out_ev);
        if (pend == KOS_STEP_STOP_EVENT)
          return 0;
        if (pend == KOS_STEP_REPROLOGUE) {
          need_prologue = 1;
          goto sw_loop_head;
        }
      }

      {
        KosEvalStep pp = eval_dispatch_maybe_post_opcode_event(es, out_ev, &eval_ret);
        if (pp == KOS_STEP_FATAL_ERROR)
          return eval_ret;
        if (pp == KOS_STEP_STOP_EVENT)
          return eval_ret;
        if (pp == KOS_STEP_REPROLOGUE) {
          need_prologue = 1;
          goto sw_loop_head;
        }
      }

      if (KosEvalState_FRAME(es)
          && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL) {
        KosEvalStep ns = eval_step_native_iteration(es, out_ev, &eval_ret);
        if (ns == KOS_STEP_FATAL_ERROR)
          return eval_ret;
        if (ns == KOS_STEP_STOP_EVENT)
          return eval_ret;
        need_prologue = 1;
        goto sw_loop_head;
      }

      #if KOS_STACK_TRACE
      {
        KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
        trace_stack("  before",
                    (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
      }
      #endif

      need_prologue = 0;
    }

  sw_prepare_and_dispatch:

    KOS_EVAL_PREPARE_NEXT_INSN(es, &insn, sw_pc_bounds_error);

    switch (KosOp_OP(insn)) {
    #define OP(name, ...)                                                \
      case KOS_OP_##name:                                                \
        KOS_TRACE_OP_WRAP(name, op_out = op_##name(es, out_ev, insn));    \
        break;
    #include "opcodes.incl"
    #undef OP
    default:
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                   (unsigned) KosOp_OP(insn));
      return -1;
    }

    if (likely(op_out.mode == KOS_LOP_RUN))
      goto sw_prepare_and_dispatch;

    if (op_out.mode == KOS_LOP_FAIL)
      return -1;

    if (op_out.mode == KOS_LOP_PROLOGUE) {
      need_prologue = 1;
      goto sw_loop_head;
    }

    if (op_out.mode == KOS_LOP_POST) {
      int op_res = kos_op_post_tag_to_res(op_out.post_tag);
      KosEvalStep ps = eval_phase_post_opcode(es, out_ev, op_res);
      if (ps == KOS_STEP_FATAL_ERROR)
        return -1;
      if (ps == KOS_STEP_STOP_EVENT)
        return 0;
      need_prologue = 1;
      goto sw_loop_head;
    }
  }

sw_pc_bounds_error:
  kos_eval_report_pc_bounds_error(es);
  return -1;

  /* All iterations return or branch; keep a fallback for static analysis. */
  return -1;
}

#endif

#undef KOS_EVAL_PREPARE_NEXT_INSN


int koskari_eval(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  int eval_ret = 0;

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koskari_eval");
    return -1;
  }

  if (! out_ev) {
    PyErr_SetString(PyExc_RuntimeError, "NULL output event");
    return -1;
  }

  KosEvalState es;
  KosEvalState_Init(&es, ctx);

  /* Mark context as running */
  KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;

  /* Push domain scope (token, context, live_depth) onto per-thread TLS */
  kos_domain_push_scope(ctx);

  /* Phase 1: entry/revive normalization */
  KosEvalMode mode = KOS_MODE_NORMAL;
  {
    KosEvalStep pe = eval_phase_entry(&es, out_ev, send_val, throw_typ,
                                      throw_exc, &mode);

    if (pe == KOS_STEP_FATAL_ERROR) {
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    if (pe != KOS_STEP_CONTINUE)
      goto koskari_eval_cleanup;
  }

  /* Phase 2: transfer-plan synchronization (only when mode warrants it) */
  if (mode == KOS_MODE_NORMAL || mode == KOS_MODE_TRANSFER) {
    if (unlikely(KosContext_PLAN(ctx)))
      if (eval_phase_transfer(&es, out_ev) != KOS_STEP_CONTINUE)
        goto koskari_eval_cleanup;
  }

  /* Clear resume kind once before loop; post_opcode clears it when handling
     non-zero results so the hot path (tail-dispatch) never touches it. */
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

#if KOS_USE_COMPUTED_GOTO
  eval_ret = koskari_eval_dispatch_computed_goto(&es, out_ev);
#else
  eval_ret = koskari_eval_dispatch_switch(&es, out_ev);
#endif

koskari_eval_cleanup:
  kos_domain_pop_scope(ctx);
  return eval_ret;
}


/* === Unit lifecycle === */


/**
   Create a Koskari exception type and add it to the module. On success,
   the module owns the reference (PyModule_AddObject steals).
   base: parent exception type (e.g. PyExc_BaseException or PyExc_RuntimeError)
*/
static int register_koskari_exception(
    PyObject *mod,
    const char *full_name,
    const char *attr_name,
    PyObject *base,
    PyObject **out_exc) {

  PyObject *bases = PyTuple_Pack(1, base);
  if (! bases)
    return -1;
  *out_exc = PyErr_NewException(full_name, bases, NULL);
  Py_DECREF(bases);
  if (! *out_exc)
    return -1;
  return PyModule_AddObject(mod, attr_name, *out_exc);
}


int koskari_eval_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* Initialize the import fromlist sentinel used by IMPORT / IMPORT_REL. */
  if (! _import_fromlist) {
    PyObject *star = PyUnicode_FromString("*");
    if (! star)
      return -1;
    _import_fromlist = PyTuple_Pack(1, star);
    Py_DECREF(star);
    if (! _import_fromlist)
      return -1;
  }

  /* Initialize string constants (cleared in koskari_eval_clear). */
  STR_CONST(str_aclose, "aclose");
  STR_CONST(str_aenter, "__aenter__");
  STR_CONST(str_aexit, "__aexit__");
  STR_CONST(str_asend, "asend");
  STR_CONST(str_athrow, "athrow");
  STR_CONST(str_await, "__await__");
  STR_CONST(str_class, "__class__");
  STR_CONST(str_close, "close");
  STR_CONST(str_enter, "__enter__");
  STR_CONST(str_exit, "__exit__");
  STR_CONST(str_getattr, "__getattr__");
  STR_CONST(str_name, "__name__");
  STR_CONST(str_package, "__package__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  // STR_CONST(str_getattribute, "__getattribute__");

  /* Initialize KosEvaluatorToken type */
  if (PyType_Ready(&KosEvaluatorTokenType) < 0)
    return -1;
  Py_INCREF(&KosEvaluatorTokenType);
  if (PyModule_AddObject(mod, "EvaluatorToken",
                         (PyObject *) &KosEvaluatorTokenType) < 0) {
    Py_DECREF(&KosEvaluatorTokenType);
    return -1;
  }

  /* Initialize exception types */
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationDead",
                                 "KoskariContinuationDead",
                                 (PyObject *) PyExc_RuntimeError,
                                 &KoskariContinuationDead) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationApply",
                                 "KoskariContinuationApply",
                                 (PyObject *) PyExc_BaseException,
                                 &KoskariContinuationApply) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationError",
                                 "KoskariContinuationError",
                                 (PyObject *) PyExc_BaseException,
                                 &KoskariContinuationError) < 0)
    return -1;

  /* Initialize dynchain module */
  if (koskari_dynchain_init(mod))
    return -1;

  return 0;
}


int koskari_eval_clear(PyObject *mod) {

  /* Clear import sentinel and string constants initialized in koskari_eval_init. */
  Py_CLEAR(_import_fromlist);

  Py_CLEAR(str_aclose);
  Py_CLEAR(str_aenter);
  Py_CLEAR(str_aexit);
  Py_CLEAR(str_asend);
  Py_CLEAR(str_athrow);
  Py_CLEAR(str_await);
  Py_CLEAR(str_class);
  Py_CLEAR(str_close);
  Py_CLEAR(str_enter);
  Py_CLEAR(str_exit);
  Py_CLEAR(str_getattr);
  Py_CLEAR(str_name);
  Py_CLEAR(str_package);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);

  /* Null out exception type pointers (module dict owns the refs) */
  KoskariContinuationDead = NULL;
  KoskariContinuationApply = NULL;
  KoskariContinuationError = NULL;

  while (context_freelist) {
    KosContext *next = KosContext_FREELIST_NEXT(context_freelist);
    PyMem_Free(context_freelist);
    context_freelist = next;
  }
  context_freecount = 0;

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
