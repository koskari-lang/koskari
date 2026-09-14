/* Included by dynchain.c — dyn_walk */
/* === Dynamic chain walk and pop primitives === */

int kos_dyn_gen_freeze(KosEvalState *es, KosDynGen *gen) {
  KosDynNode *saved_parent;
  KosDynNode *ctx_active;
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

#if KOS_EVAL_SAFETYNET
  {
    /* gen came from kos_dyn_find_gen on the running frame, which sits
       on dyn_top's path; the span from dyn_top must reach it. */
    KosDynNode *node;
    for (node = KosEvalState_DYN_TOP(es); node; node = KosDynNode_PARENT(node)) {
      if (node == (KosDynNode *) gen)
        break;
    }
    if (! node) {
      PyErr_SetString(PyExc_SystemError,
                      "kos_dyn_gen_freeze: gen not in chain from dyn_top");
      return -1;
    }
  }
#endif

  /* 1. Record GEN's parent, then remove the GEN->parent link */
  saved_parent = KosDynNode_PARENT(gen);
  KosDynNode_PARENT(gen) = NULL;

  /* 2. Remove ctx's active node and store it in GEN, with the live
     registers the next install of this head applies directly. */
  ctx_active = KosEvalState_DYN_TOP(es);
  KosDynGen_ACTIVE(gen) = ctx_active;
  kos_live_regs_capture(es, KosDynGen_FROZEN(gen));

  /* The frozen span leaves the live chain; its segments' unwind-cache
     back links cannot survive the detach. */
  kos_dyn_clear_unwind_caches(ctx_active, NULL);

  SAY("[gen] freeze storing in gen->active: chain ");
  for (n = ctx_active; n; n = KosDynNode_PARENT(n))
    SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
  SAY("(end)");

  /* 3. Put GEN's old parent onto ctx's active. The consumer below the
     GEN has not run since the install recorded its registers, so they
     are restored as recorded when the GEN still sits where it was
     installed; anything else rebuilds from the chain. */
  /* host.top always equals saved_parent here: both installs record the
     host and set the GEN's parent from the same dyn_top, and nothing
     re-parents a running GEN. The compare is kept for the same reason
     as the frozen.top compare in kos_dyn_gen_install. */
  if (saved_parent && saved_parent == KosDynGen_HOST(gen)->top
      && KosDynGen_HOST(gen)->frame) {
    kos_live_regs_apply(es, KosDynGen_HOST(gen));
  } else {
    KosEvalState_DYN_TOP(es) = saved_parent;
    kos_dyn_resync_chain(es);
  }
  KosDynGen_HOST(gen)->top = NULL;

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

  /* GEN is frozen back into the KPAG: drop the keepalive.  Done last, as it
     may run the owner's deallocation if no one else holds it (an abandoned
     generator cleans up here).  No reads of gen/owner follow. */
  kos_dyngen_keepalive_release(gen);
  return 0;
}


KosOpResult kos_dyn_exception_unwind(
    KosEvalState *es,
    KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT:
      if (KOS_XFER_TRACE)
        SAYF("[xfer] prompt_unwind_exception node=%p: unlinking prompt", (void *) node);
      {
        KosOpResult kr = kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_NONE);
        if (kr.kind != KOS_RES_NORMAL && kr.kind != KOS_RES_REPROLOGUE
            && kr.kind != KOS_RES_AWAIT)
          return kr;
      }
      /* Exception propagates past the prompt; continue unwinding */
      return EVAL_NORMAL();
    case KOS_DYN_DELIM: {
      int r = kos_dyn_pop_delim(es, (KosDynDelim *) node);
      return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
    }
    default:
      return EVAL_NORMAL();
  }
}


/**
   Generic unwind walk toward a target node. See dynchain.h for the
   contract. Pops until dyn_top == target or empty, crossing barriers and
   delimiters since these walks deliberately unwind through delimited
   prompt boundaries. Callers: eval.c route_delim_return.
*/
KosOpResult kos_dyn_unwind_to_node(
    KosEvalState *es,
    const KosDynNode *target,
    KosXferEvent event,
    KosDynNode **out_pending) {

  if (out_pending)
    *out_pending = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: target=%p kind=%s",
         (void *) target, target ? kos_dynnode_kind_name(KosDynNode_KIND(target)) : "NULL");

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != target) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);
    KosOpResult r;

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_node: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    r = kos_dyn_unlink_top(es, event, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    /* the node's leave is in flight: the caller parks what it carries
       on it, and that resumes once the node has left the chain */
    if (out_pending
        && (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT))
      *out_pending = top;
    return r;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return EVAL_NORMAL();
}


/**
   RETURN unwind walk to frame. See dynchain.h for the contract. Caller:
   eval.c eval_event_return.
*/
KosOpResult kos_dyn_unwind_return(
    KosEvalState *es,
    KosDynFrame *frame,
    PyObject *val,
    KosPendingReturnKind kind,
    KosDynDelim **took) {

  *took = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_return: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);
    KosOpResult r;

    /* Stopping node: a live delimited-invoke bookmark.  The return has
       reached the delimiting reset; leave the DELIM at dyn_top and let the
       caller route val to the k-invocation site.  val is NOT consumed. */
    if (KosDynNode_IS_DELIM(top)
        && KosDynDelim_RETURN_FRAME((KosDynDelim *) top) != NULL
        && KosDynDelim_RETURN_PC((KosDynDelim *) top) != (uint32_t) -1) {
      if (KOS_XFER_TRACE)
        SAYF("[xfer] unwind_return: stopped at DELIM bookmark node=%p",
             (void *) top);
      *took = (KosDynDelim *) top;
      return EVAL_NORMAL();
    }

    /* Handle phase: offer RETURN before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RETURN);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top)) {
          /* Deliver to finally block: clear stack, push value, set PC */
          KosDynFinally *fin = (KosDynFinally *) top;
          if (KosEvalState_StackClear(es) < 0) {
            Py_DECREF(val);
            return EVAL_RAISE();
          }
          if (KosEvalState_StackPushRef(es, val) < 0) {
            Py_DECREF(val);
            return EVAL_RAISE();
          }
          Py_DECREF(val);
          if (KosEvalState_FRAME(es))
            KosEvalState_SET_PC(es, KosDynFinally_HANDLER_PC(fin));
          KosDynFinally_ENABLED(fin) = 0;
          KosDynFinally_HANDLING_CONTROL(fin) = KOS_PENDING_RETURN_FRAME;
          if (KOS_XFER_TRACE)
            SAYF("[xfer] unwind_return: delivered to FINALLY, pc=%u",
                 (unsigned) KosDynFinally_HANDLER_PC(fin));
          return EVAL_REPROLOGUE();
        }
        /* Other CONSUMED (e.g. GEN) not expected for RETURN walk */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during return walk: unexpected node");
        Py_DECREF(val);
        return EVAL_RAISE();
      }
      if (hr == KOS_HANDLE_ERROR) {
        Py_DECREF(val);
        return EVAL_RAISE();
      }
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_return: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    r = kos_dyn_unlink_top(es, KOS_XFER_EVENT_RETURN, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      /* The node's leave started and will suspend; val parks on the node
         and the return resumes once the node has left the chain. */
      if (kos_dyn_ob_park_return(top, val, (uint8_t) kind, NULL) < 0) {
        Py_XDECREF(r.val);
        return EVAL_RAISE();
      }
      return r;
    }
    Py_DECREF(val);
    return r;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_return: done, dyn_top=%p",
         (void *) KosEvalState_DYN_TOP(es));
  return EVAL_NORMAL();
}


/**
   Generic unwind walk toward a frame. See dynchain.h for the contract.
   Pops toward frame with handle(event) before each pop; a CONSUME (other
   than the FINALLY case, handled by kos_dyn_unwind_return) is an error.
   Callers: eval.c route_delim_return.
*/
KosOpResult kos_dyn_unwind_to_frame(
    KosEvalState *es,
    KosDynFrame *frame,
    KosXferEvent event,
    KosDynNode **out_pending) {

  if (out_pending)
    *out_pending = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);
    KosOpResult r;

    /* Handle phase: offer event before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, event);
      if (hr == KOS_HANDLE_CONSUMED) {
        /* RETURN: use kos_dyn_unwind_return. RAISE: use kos_dyn_unwind_raise. */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during unwind_to_frame not implemented");
        return EVAL_RAISE();
      }
      if (hr == KOS_HANDLE_ERROR)
        return EVAL_RAISE();
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_frame: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    r = kos_dyn_unlink_top(es, event, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (out_pending
        && (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT))
      *out_pending = top;
    return r;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return EVAL_NORMAL();
}


/**
   Find the nearest LOOP or FOREACH from dyn_top. Read-only; never sets
   PyErr. Callers: opcode/ITER_NEXT.c and kos_dyn_unwind_to_loop serving
   BREAK/CONTINUE.
*/
KosDynNode *kos_dyn_find_loop(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);
  while (node) {
    if (KosDynNode_IS_LOOP(node) || KosDynNode_IS_FOREACH(node))
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   BREAK/CONTINUE unwind walk to the innermost loop. See dynchain.h for the
   contract. Finds the loop region in the current frame, unwinds to it, then
   sets PC and stack per event. Callers: opcode/BREAK.c, opcode/CONTINUE.c.
*/
KosOpResult kos_dyn_unwind_to_loop(
    KosEvalState *es,
    KosXferEvent event,
    PyObject *result) {

  KosDynNode *region = kos_dyn_find_loop(es);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    (event == KOS_XFER_EVENT_BREAK)
                        ? "break outside loop"
                        : "continue outside loop");
    return EVAL_RAISE();
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
      Py_DECREF(result);
      PyErr_SetString(PyExc_RuntimeError,
                      (event == KOS_XFER_EVENT_BREAK)
                          ? "break outside loop"
                          : "continue outside loop");
      return EVAL_RAISE();
    }
  }

  /* Walk from DYN_TOP toward region; unwind each node with event */
  while (KosEvalState_DYN_TOP(es) != (KosDynNode *) region) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);
    KosOpResult r;

    /* Handle phase: offer BREAK/CONTINUE before unwind, as the return
       walk offers RETURN. An enabled FINALLY consumes it and the value
       rides its stack through the finally body; POP_FINALLY re-issues
       the transfer once the region is gone. */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, event);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top)) {
          KosDynFinally *fin = (KosDynFinally *) top;
          if (KosEvalState_StackClear(es) < 0) {
            Py_DECREF(result);
            return EVAL_RAISE();
          }
          if (KosEvalState_StackPushRef(es, result) < 0) {
            Py_DECREF(result);
            return EVAL_RAISE();
          }
          Py_DECREF(result);
          if (KosEvalState_FRAME(es))
            KosEvalState_SET_PC(es, KosDynFinally_HANDLER_PC(fin));
          KosDynFinally_ENABLED(fin) = 0;
          KosDynFinally_HANDLING_CONTROL(fin) =
              (event == KOS_XFER_EVENT_BREAK)
                  ? KOS_PENDING_RETURN_BREAK : KOS_PENDING_RETURN_CONTINUE;
          if (KOS_XFER_TRACE)
            SAYF("[xfer] unwind_to_loop: delivered to FINALLY, pc=%u",
                 (unsigned) KosDynFinally_HANDLER_PC(fin));
          return EVAL_REPROLOGUE();
        }
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during loop walk: unexpected node");
        Py_DECREF(result);
        return EVAL_RAISE();
      }
      if (hr == KOS_HANDLE_ERROR) {
        Py_DECREF(result);
        return EVAL_RAISE();
      }
    }

    r = kos_dyn_unlink_top(es, event, 0);

    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      /* The node's leave started and will suspend: result parks on
         the node with the event as its path, and the walk resumes
         from here once the node has left the chain, as a return does. */
      uint8_t path = (event == KOS_XFER_EVENT_BREAK)
          ? KOS_PENDING_RETURN_BREAK : KOS_PENDING_RETURN_CONTINUE;

      if (kos_dyn_ob_park_return(top, result, path, NULL) < 0) {
        Py_XDECREF(r.val);
        return EVAL_RAISE();
      }
      return r;
    }
    Py_DECREF(result);
    return r;
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

    if (KosEvalState_StackClear(es) < 0)
      return EVAL_RAISE();

    KosEvalState_SET_PC(es, continue_pc);
    return EVAL_NORMAL();
  }

  /* KOS_XFER_EVENT_BREAK: read break_pc before popping, then pop, drop block,
     push result */
  uint32_t break_pc = (KosDynNode_IS_LOOP(region))
      ? KosDynLoop_BREAK_PC((KosDynLoop *) region)
      : KosDynForeach_BREAK_PC((KosDynForeach *) region);

  if (KosDynNode_IS_LOOP(region)) {
    if (kos_dyn_pop_loop(es, (KosDynLoop *) region) < 0) {
      Py_DECREF(result);
      return EVAL_RAISE();
    }
  } else if (kos_dyn_pop_foreach(es, (KosDynForeach *) region) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }

  if (KosEvalState_StackPushRef(es, result) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }
  Py_DECREF(result);

  KosEvalState_SET_PC(es, break_pc);
  return EVAL_NORMAL();
}


/**
   Hand the pending exception to the CPS resume armed on a frame the
   walk reached at dyn_top above the live frame -- a flattened
   delegation whose resume the prologue's own delivery cannot reach,
   since that tests the live frame. A plain call with NULL, the resume
   disarmed first as the delivery step disarms it. The resume handles
   the exception (REPROLOGUE, once it has arranged the frame's
   continuation) or propagates it with EVAL_RAISE, in which case the
   walk goes on to pop the frame.

   A code-less step frame that is itself the live frame does not come
   through here: process_raise hands that case to the prologue, which
   delivers against a settled chain where the resume may thread on.

   :return: EVAL_REPROLOGUE, EVAL_RAISE, or EVAL_FATAL
*/
static KosOpResult raise_walk_hand_to_resume(
    KosEvalState *es,
    KosDynFrame *frame) {

  KosResumeFn resume = (KosResumeFn) KosDynFrame_RESUME_FN(frame);
  void *state = KosDynFrame_RESUME_STATE(frame);
  KosOpResult hr;

  KosDynFrame_RESUME_FN(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;

  KOS_TRACE_RESUME_STEP();
  hr = resume(es, state, NULL);
  KOS_TRACE_TRAMPOLINE_STEP();

  switch (hr.kind) {
    case KOS_RES_NORMAL:
    case KOS_RES_REPROLOGUE:
      return EVAL_REPROLOGUE();

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      return hr;

    default:
      /* The resume ran as a plain call: it may handle the exception
         or propagate it, never thread on or produce a signal this
         walk cannot apply. */
      Py_XDECREF(hr.val);
#if KOS_EVAL_SAFETYNET
      PyErr_SetString(PyExc_SystemError,
                      "resume answered a propagating exception with"
                      " a signal other than REPROLOGUE or RAISE");
      return EVAL_FATAL();
#else
      return EVAL_RAISE();
#endif
  }
}


/**
   RAISE unwind walk. See dynchain.h for the contract. Walks dyn_top down
   toward the innermost frame, offering handle(RAISE) then popping until a
   TRY or FINALLY consumes (jump to handler) or no handler remains, crossing
   frame and generator boundaries as the exception propagates. Returns
   KosOpResult (the eval-loop boundary type): EVAL_REPROLOGUE when a handler
   applied, EVAL_RAISE when none remains, EVAL_AWAIT when an async unwind
   started. Callers: process_raise (the unified RAISE ingress in
   eval.c).
*/
KosOpResult kos_dyn_unwind_raise(KosEvalState *es) {
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

  #endif

  PyObject *saved_exc_typ = NULL;
  PyObject *saved_exc_val = NULL;
  PyObject *saved_exc_tb = NULL;

  while (KosEvalState_FRAME(es) && KosEvalState_DYN_TOP(es) != NULL) {

    /* Frame crossing: cross into the caller section and keep walking.
       Handler discovery is node-authoritative - a live TRY/FINALLY node
       consumes via handle(); the extent table only supplies jump
       positions at node push time. The raise leaves the loop only at
       the chain root (NULL parent) or a generator node. */
    if (KosEvalState_DYN_TOP(es) == (KosDynNode *) KosEvalState_FRAME(es)) {
      if (! PyErr_Occurred())
        break;

      /* A code-less step frame's resume has already seen this
         exception: process_raise hands it to the prologue, which
         delivers it as a NULL against a settled chain, and the walk
         only runs once that resume declined and disarmed itself. */

      {
        KosDynNode *parent = KosDynNode_PARENT(KosEvalState_DYN_TOP(es));
        if (! parent)
          break;
        if (KosDynNode_IS_GEN(parent)) {
          if (! KosDynNode_PARENT(parent))
            /* Adapter-driven generator: the GEN is the context root and
               the raise exits to the Python driver, which translates it
               (anext / demand step). */
            break;

          /* Flattened generator: the raise propagates INTO the consumer
             at the iteration point, per Python generator semantics. The
             generator is finished by the exception: record its frame in
             the trace, mark the owner closed, unlink frame+GEN (the
             chain state here -- frame on top, GEN parent, consumer
             below -- is exactly the exhaustion shape), and continue the
             walk so the consumer's regions can catch. */
          kos_eval_note_crossed_frame(es);
          {
            PyObject *owner = KosDynGen_OWNER(parent);
            KosIterSlots *islots;
            if (owner && KosIter_CheckSlots(owner, &islots))
              islots->ki_finish(owner, 1);
          }
          {
            PyObject *typ = NULL, *val = NULL, *tb = NULL;
            PyErr_Fetch(&typ, &val, &tb);
            if (kos_dyn_teardown_gen_exhausted(es) < 0) {
              Py_XDECREF(typ);
              Py_XDECREF(val);
              Py_XDECREF(tb);
              return EVAL_FATAL();
            }
            PyErr_Restore(typ, val, tb);
          }
          continue;
        }
        /* Record the frame into the exception's trace before it goes. */
        kos_eval_note_crossed_frame(es);
        if (kos_dyn_pop_frame(es) < 0)
          return EVAL_FATAL();
      }
      continue;
    }

    KosDynNode *top = KosEvalState_DYN_TOP(es);

    KOS_TRACE_EVENTS_INC(KOS_TE_RAISE_LOCAL_WALK_ITER);

    SAYF("[try_local] walk top=%p kind=%s\n",
         (void *) top,
         kos_dynnode_kind_name(KosDynNode_KIND(top)));

    /* Cap fast check: nodes that intercept RAISE, and receiving iterator
       boundaries (which may consume a StopIteration from their in-flight
       flattened step), are dispatched to the handle phase; everything
       else goes straight to unwind/pop. */
    if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_INTERCEPTS_RAISE)
        || KosDynNode_HAS_CAP(top, KOS_DYN_CAP_ITER_RECEIVER)) {

    /* In-place delimited return bookmark: the slice raised. Restitch the
       invoke span and continue the raise from the invoke site. */
    if (KosDynNode_IS_DELIM(top)) {
      PyObject *typ = NULL, *val = NULL, *tb = NULL;
      PyErr_Fetch(&typ, &val, &tb);
      if (kos_dyn_inplace_restitch(es, (KosDynDelim *) top) < 0) {
        Py_XDECREF(typ);
        Py_XDECREF(val);
        Py_XDECREF(tb);
        return EVAL_FATAL();
      }
      PyErr_Restore(typ, val, tb);
      continue;
    }

    /* Handle phase: offer RAISE to node before unwind.  If consumed,
       deliver to handler and return.  Otherwise pop and continue. */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RAISE);
      SAYF("[try_local] kos_dyn_handle RAISE hr=%d (%s)\n",
           (int) hr,
           hr == KOS_HANDLE_CONSUMED ? "CONSUMED"
             : hr == KOS_HANDLE_PASS ? "PASS"
             : hr == KOS_HANDLE_DEFERRED ? "DEFERRED"
             : hr == KOS_HANDLE_ERROR ? "ERROR" : "?");

      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_GEN(top)) {
          if (! KosDynNode_PARENT(top)) {
            /* Adapter-driven generator: the GEN is the context root and
               the raise exits to the Python driver, which translates it. */
            kos_eval_prepare_boundary_raise(ctx, es);
            return EVAL_RAISE();
          }

          /* Flattened generator whose frame the earlier raise phases
             already crossed (dyn_top is the GEN itself). Finish it and
             continue the walk so the raise propagates into the
             consumer's regions, per Python generator semantics. */
          kos_dyn_finish_flattened_gen(es, (KosDynGen *) top);
          continue;
        }

        if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_ITER_RECEIVER)) {
          /* The iteration node consumed the step-ending exception from its
             in-flight step as end-of-iteration: convert to exhaustion
             delivery. StopIteration carries the flattened generator's
             return value (coroutine protocol equation);
             StopAsyncIteration has no return-value channel. */
          PyObject *stop_val = NULL;

          if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
            if (PyGen_FetchStopIterationValue(&stop_val) < 0)
              return EVAL_FATAL();
          } else {
            PyErr_Clear();
          }
          if (! stop_val) {
            stop_val = Py_None;
            Py_INCREF(stop_val);
          }
          if (kos_dyn_iter_node_deliver_exhausted(es, top, stop_val) < 0) {
            if (PyErr_Occurred())
              /* the iteration node re-emitted (e.g. 1-arg NEXT re-raising
                 StopIteration): keep propagating */
              continue;
            return EVAL_FATAL();
          }
          return EVAL_REPROLOGUE();
        }

        KosDynNode *region = top;
        uint32_t handler_pc =
            kos_dynnode_raise_handler_pc_for_try_finally(es, region);

        if (KosDynNode_IS_TRY(region) || KosDynNode_IS_FINALLY(region)) {
          if (KosEvalState_StackClear(es) < 0) {
            return EVAL_FATAL();
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
          /* The region now owns the handled exception. Save what the
             thread state showed, to give back when the region leaves,
             then make the raise handler-visible. A FINALLY also keeps
             its own reference for the re-raise at POP_FINALLY, which a
             region nested in the finally body must not disturb. */
          KosExcState raised;
          KosExc_Init(&raised);
          KosExc_TakeRaised(&raised);
          if (KosDynNode_IS_TRY(region)) {
            KosExc_Fetch(KosDynTry_SAVED((KosDynTry *) region));
            KosDynTry_EXC_SAVED((KosDynTry *) region) = 1;
          } else {
            KosExc_Fetch(KosDynFinally_SAVED((KosDynFinally *) region));
            KosDynFinally_EXC_SAVED((KosDynFinally *) region) = 1;
            KosExc_Clear(KosDynFinally_HANDLING((KosDynFinally *) region));
            KosExc_Copy(KosDynFinally_HANDLING((KosDynFinally *) region),
                        &raised);
          }
          KosExc_Install(&raised);
        }

        if (KosDynNode_IS_FINALLY(region))
          KosDynFinally_HANDLING_RAISE((KosDynFinally *) region) = 1;
        return EVAL_REPROLOGUE();
      }

      if (hr == KOS_HANDLE_ERROR) {
        return EVAL_FATAL();
      }
    }

    }  /* KOS_DYN_CAP_INTERCEPTS_RAISE */

    /* A resume-carrying frame at dyn_top above the live frame: its
       resume sees the pending exception the same way the
       frame-crossing branch's does, and a propagated exception goes
       on to the pop below. */
    if (KosDynNode_IS_FRAME(top)
        && KosDynFrame_RESUME_FN((KosDynFrame *) top)) {
      KosOpResult hr = raise_walk_hand_to_resume(es, (KosDynFrame *) top);

      if (hr.kind != KOS_RES_RAISE)
        return hr;
      /* propagating: fall through to the pop */
    }

    /* PASS: unwind and pop */
    {
      /* Everything the code after the pop needs from the node is read
         here: a pop that completes unlinks and frees it. */
      int top_may_swallow = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_CATCH));
      uint32_t catch_resume_pc = kos_dynnode_catch_resume_pc(top);
      int top_is_wind = (top && KosDynNode_IS_WIND(top));
      int top_is_barrier =
          (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER));
      int top_handles_exceptions =
          (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_HANDLES_EXCEPTIONS));
      /* A wind about to fire its after-thunk runs it clean: the
         exception in flight is held here and parked on the node once
         the thunk has started. A wind (or with) whose thunk is dead is
         aborted by the unlink and never fires, so the exception stays
         pending for the unlink to chain what the node was holding. A
         with fires __exit__ with the exception as its argument and
         parks it itself. */
      int needs_preserve = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP))
          || top_is_barrier
          || (top_is_wind && ! kos_dyn_extent_in_flight(top));
      if (needs_preserve && PyErr_Occurred())
        PyErr_Fetch(&saved_exc_typ, &saved_exc_val, &saved_exc_tb);

      KosOpResult pop_res = kos_dyn_pop_nonbarrier_top(es, KOS_XFER_EVENT_RAISE);
      if (saved_exc_typ != NULL) {
        if (top_is_wind
            && (pop_res.kind == KOS_RES_REPROLOGUE
                || pop_res.kind == KOS_RES_AWAIT)) {
          /* After-thunk started: park the exception on the wind so the
             thunk runs with no pending error; the unlink re-raises it
             once the thunk completes. */
          if (kos_dyn_ob_park_exception(top, saved_exc_typ, saved_exc_val,
                                        saved_exc_tb) < 0) {
            Py_XDECREF(pop_res.val);
            return EVAL_FATAL();
          }
        } else if (PyErr_Occurred() && ! top_is_barrier) {
          /* The pop ran user teardown and it raised; the new exception
             supersedes the one in flight, which chains in as __context__
             (never clobbered). Barrier refusals are machinery noise, not
             user failures: for those the original is restored below and
             the recovery branch crosses the barrier properly. */
          PyObject *ntyp = NULL, *nval = NULL, *ntb = NULL;
          PyErr_Fetch(&ntyp, &nval, &ntb);
          kos_exc_supersede_chain(&ntyp, &nval, &ntb,
                                  saved_exc_typ, saved_exc_val,
                                  saved_exc_tb);
          PyErr_Restore(ntyp, nval, ntb);
        } else {
          PyErr_Restore(saved_exc_typ, saved_exc_val, saved_exc_tb);
        }
        saved_exc_typ = saved_exc_val = saved_exc_tb = NULL;
      }
      if (pop_res.kind == KOS_RES_AWAIT) {
        return pop_res;
      }
      if (pop_res.kind == KOS_RES_REPROLOGUE) {
        return EVAL_REPROLOGUE();
      }
      if (pop_res.kind != KOS_RES_NORMAL) {
        if (pop_res.kind == KOS_RES_FATAL) {
          /* Evaluator-internal fault: allowed to abend the loop. */
          return EVAL_FATAL();
        }
        if (top_handles_exceptions) {
          KosOpResult exc_res = kos_dyn_exception_unwind(es, top);
          if (exc_res.kind == KOS_RES_AWAIT) {
            return exc_res;
          }
          if (exc_res.kind == KOS_RES_REPROLOGUE) {
            return EVAL_REPROLOGUE();
          }
          if (exc_res.kind == KOS_RES_NORMAL)
            continue;
        }
        if (top_is_barrier) {
          /* Barrier with no working recovery: a legitimate walk exit. */
          SAY("[try_local] barrier without recovery -> RAISE\n");
          kos_eval_prepare_boundary_raise(ctx, es);
          return EVAL_RAISE();
        }
        /* A leave raised: the superseding exception keeps walking. The
           failing node is still in place, its call dead at dyn_top, so
           the unlink aborts it without re-running anything and chains
           what it was holding under the new exception. */
        if (KosEvalState_DYN_TOP(es) == top) {
          KosOpResult again = kos_dyn_unlink_top(es, KOS_XFER_EVENT_RAISE, 1);
          if (again.kind != KOS_RES_NORMAL && again.kind != KOS_RES_RAISE) {
            Py_XDECREF(again.val);
            return EVAL_FATAL();
          }
          if (KosEvalState_DYN_TOP(es) == top) {
            /* the node refused to leave twice: no progress possible */
            PyErr_SetString(PyExc_SystemError,
                            "raise walk: a node's leave keeps failing");
            return EVAL_FATAL();
          }
        }
        continue;
      }
      if (top_may_swallow && ! PyErr_Occurred()) {
        if (catch_resume_pc != (uint32_t) -1 && KosEvalState_FRAME(es)) {
          KosEvalState_SET_PC(es, catch_resume_pc);
        }
        return EVAL_REPROLOGUE();
      }
    }
  }

  /* No handler found */
  {
    SAY("[try_local] no handler found -> RAISE\n");
    kos_eval_prepare_boundary_raise(ctx, es);
    return EVAL_RAISE();
  }
}
