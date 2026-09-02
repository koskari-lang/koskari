/* Included by dynchain.c — dyn_walk */
/* === Dynamic chain walk and pop primitives === */

static int kos_dyn_gen_freeze_and_release(KosEvalState *es, KosDynGen *gen) {
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

  /* 1. Record GEN's parent, then remove the GEN->parent link */
  saved_parent = KosDynNode_PARENT(gen);
  KosDynNode_PARENT(gen) = NULL;

  /* 2. Remove ctx's active node and store it in GEN */
  ctx_active = KosEvalState_DYN_TOP(es);
  KosDynGen_ACTIVE(gen) = ctx_active;

  /* The frozen span leaves the live chain; its segments' unwind-cache
     back links cannot survive the detach. */
  kos_dyn_clear_unwind_caches(ctx_active, NULL);

  SAY("[gen] freeze storing in gen->active: chain ");
  for (n = ctx_active; n; n = KosDynNode_PARENT(n))
    SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
  SAY("(end)");

  /* 3. Put GEN's old parent onto ctx's active */
  KosEvalState_DYN_TOP(es) = saved_parent;
  kos_dyn_resync_chain(es);

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


/**
   YIELD unwind walk. See dynchain.h for the contract. Offers handle(YIELD)
   up from dyn_top; freezes and detaches the GEN node that consumes.
   Callers: eval.c eval_event_yield.
*/
KosWalkStatus kos_dyn_unwind_yield(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  SAYF("[gen] unwind_yield ENTER dyn_top=%p\n", (void *) node);
  while (node) {
    KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_YIELD);
    SAYF("[gen] unwind_yield node=%p kind=%s hr=%d\n",
         (void *) node, kos_dynnode_kind_name(KosDynNode_KIND(node)),
         (int) hr);

    if (hr == KOS_HANDLE_CONSUMED) {
      if (KosDynNode_IS_GEN(node)) {
        SAYF("[gen] unwind_yield -> freeze at gen %p\n", (void *) node);
        int fr = kos_dyn_gen_freeze_and_release(es, (KosDynGen *) node);
        return (fr < 0) ? KOS_WALK_ERROR : KOS_WALK_OK;
      }
      SAY("[gen] unwind_yield -> consumed (non-GEN)");
      return KOS_WALK_OK;
    }

    if (hr == KOS_HANDLE_ERROR) {
      return KOS_WALK_ERROR;
    }

    node = KosDynNode_PARENT(node);
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "yield outside generator: no generator node on chain");
  return KOS_WALK_ERROR;
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


KosOpResult kos_dyn_pop_nonbarrier_top(
    KosEvalState *es,
    KosXferEvent event) {

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return EVAL_RAISE();
  }

  if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
    PyErr_SetString(PyExc_RuntimeError, "cannot unwind past barrier node");
    return EVAL_RAISE();
  }

  return kos_dyn_uninstall_core(es, top, event);
}


KosOpResult kos_dyn_pop_top_allow_barrier(
    KosEvalState *es,
    KosXferEvent event) {

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop from empty dynamic chain");
    return EVAL_RAISE();
  }

  return kos_dyn_uninstall_core(es, top, event);
}


KosOpResult kos_dyn_pop_top(KosEvalState *es, KosXferEvent event) {
  KosOpResult kr = kos_dyn_pop_nonbarrier_top(es, event);
  if (kr.kind == KOS_RES_AWAIT) {
    Py_XDECREF(kr.val);
    return EVAL_REPROLOGUE();
  }
  return kr;
}


/**
   Generic unwind walk toward a target node. See dynchain.h for the
   contract. Pops until dyn_top == target or empty, crossing barriers and
   delimiters (PROMPT pops via kos_dyn_pop_prompt) since these walks
   deliberately unwind through delimited prompt boundaries. Callers: eval.c
   route_delim_return, continuation transfer helpers.
*/
KosWalkStatus kos_dyn_unwind_to_node(KosEvalState *es,
                                     const KosDynNode *target,
                                     KosXferEvent event) {

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
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    int m = kos_map_pop_result(kos_dyn_pop_top_allow_barrier(es, event));
    if (m)
      return (m < 0) ? KOS_WALK_ERROR : KOS_WALK_ASYNC_UNWIND;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return KOS_WALK_OK;
}


/**
   RETURN unwind walk to frame. See dynchain.h for the contract. Offers
   handle(RETURN) to each top; a FINALLY consumes and branches to its
   handler, a live DELIM invoke bookmark stops the walk for the caller to
   route the value. Callers: eval.c eval_event_return, utils.c native apply
   return.
*/
KosWalkStatus kos_dyn_unwind_return(KosEvalState *es, KosDynFrame *frame,
                                    PyObject *val) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_return: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    /* Stopping node: a live delimited-invoke bookmark.  The return has
       reached the delimiting reset; leave the DELIM at dyn_top and let the
       caller route val to the k-invocation site.  val is NOT consumed. */
    if (KosDynNode_IS_DELIM(top)
        && KosDynDelim_RETURN_FRAME((KosDynDelim *) top) != NULL
        && KosDynDelim_RETURN_PC((KosDynDelim *) top) != (uint32_t) -1) {
      if (KOS_XFER_TRACE)
        SAYF("[xfer] unwind_return: stopped at DELIM bookmark node=%p",
             (void *) top);
      return KOS_WALK_DELIM_TAKING_RETURN;
    }

    /* Handle phase: offer RETURN before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RETURN);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top)) {
          /* Deliver to finally block: clear stack, push value, set PC */
          KosDynFinally *fin = (KosDynFinally *) top;
          {
            if (KosEvalState_StackClear(es) < 0) {
              Py_DECREF(val);
              return KOS_WALK_ERROR;
            }
            if (KosEvalState_StackPushRef(es, val) < 0) {
              Py_DECREF(val);
              return KOS_WALK_ERROR;
            }
          }
          Py_DECREF(val);
          if (KosEvalState_FRAME(es)) {
            KosEvalState_SET_PC(es, KosDynFinally_HANDLER_PC(fin));
          }
          KosDynFinally_ENABLED(fin) = 0;
          KosDynFinally_HANDLING_RETURN(fin) = 1;
          if (KOS_XFER_TRACE)
            SAYF("[xfer] unwind_return: delivered to FINALLY, pc=%u",
                 (unsigned) KosDynFinally_HANDLER_PC(fin));
          return KOS_WALK_FINALLY_TAKING_RETURN;
        }
        /* Other CONSUMED (e.g. GEN) not expected for RETURN walk */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during return walk: unexpected node");
        Py_DECREF(val);
        return KOS_WALK_ERROR;
      }
      if (hr == KOS_HANDLE_ERROR) {
        Py_DECREF(val);
        return KOS_WALK_ERROR;
      }
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_return: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    int m = kos_map_pop_result(
        kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_RETURN));
    if (m) {
      Py_DECREF(val);
      return (m < 0) ? KOS_WALK_ERROR : KOS_WALK_ASYNC_UNWIND;
    }
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_return: done, dyn_top=%p",
         (void *) KosEvalState_DYN_TOP(es));
  return KOS_WALK_OK;
}


/**
   Generic unwind walk toward a frame. See dynchain.h for the contract.
   Pops toward frame with handle(event) before each pop; a CONSUME (other
   than the FINALLY case, handled by kos_dyn_unwind_return) is an error.
   Callers: eval.c route_delim_return.
*/
KosWalkStatus kos_dyn_unwind_to_frame(KosEvalState *es, KosDynFrame *frame,
                                      KosXferEvent event) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    /* Handle phase: offer event before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, event);
      if (hr == KOS_HANDLE_CONSUMED) {
        /* RETURN: use kos_dyn_unwind_return. RAISE: use kos_dyn_unwind_raise. */
        PyErr_SetString(PyExc_RuntimeError,
                        "handle(CONSUMED) during unwind_to_frame not implemented");
        return KOS_WALK_ERROR;
      }
      if (hr == KOS_HANDLE_ERROR) {
        return KOS_WALK_ERROR;
      }
    }

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_frame: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    int m = kos_map_pop_result(kos_dyn_pop_top_allow_barrier(es, event));
    if (m)
      return (m < 0) ? KOS_WALK_ERROR : KOS_WALK_ASYNC_UNWIND;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return KOS_WALK_OK;
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
KosWalkStatus kos_dyn_unwind_to_loop(KosEvalState *es, KosXferEvent event,
                                     PyObject *result) {

  KosDynNode *region = kos_dyn_find_loop(es);
  if (! region) {
    PyErr_SetString(PyExc_RuntimeError,
                    (event == KOS_XFER_EVENT_BREAK)
                        ? "break outside loop"
                        : "continue outside loop");
    return KOS_WALK_ERROR;
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
      return KOS_WALK_ERROR;
    }
  }

  /* Walk from DYN_TOP toward region; unwind each node with event */
  while (KosEvalState_DYN_TOP(es) != (KosDynNode *) region) {
    int m = kos_map_pop_result(kos_dyn_pop_nonbarrier_top(es, event));
    if (m)
      return (m < 0) ? KOS_WALK_ERROR : KOS_WALK_ASYNC_UNWIND;
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

    if (KosEvalState_StackClear(es) < 0) {
      return KOS_WALK_ERROR;
    }

    KosEvalState_SET_PC(es, continue_pc);
    return KOS_WALK_OK;
  }

  /* KOS_XFER_EVENT_BREAK: read break_pc before popping, then pop, drop block,
     push result */
  uint32_t break_pc = (KosDynNode_IS_LOOP(region))
      ? KosDynLoop_BREAK_PC((KosDynLoop *) region)
      : KosDynForeach_BREAK_PC((KosDynForeach *) region);

  if (KosDynNode_IS_LOOP(region)) {
    if (kos_dyn_pop_loop(es, (KosDynLoop *) region) < 0)
      return KOS_WALK_ERROR;
  } else if (kos_dyn_pop_foreach(es, (KosDynForeach *) region) < 0) {
    return KOS_WALK_ERROR;
  }

  if (KosEvalState_StackPushRef(es, result) < 0) {
    Py_DECREF(result);
    return KOS_WALK_ERROR;
  }
  Py_DECREF(result);

  KosEvalState_SET_PC(es, break_pc);
  return KOS_WALK_OK;
}


/**
   RAISE unwind walk. See dynchain.h for the contract. Walks dyn_top down
   toward the innermost frame, offering handle(RAISE) then popping until a
   TRY or FINALLY consumes (jump to handler) or no handler remains, crossing
   frame and generator boundaries as the exception propagates. Returns
   KosOpResult (the eval-loop boundary type): EVAL_REPROLOGUE when a handler
   applied, EVAL_RAISE when none remains, EVAL_AWAIT when an async unwind
   started. Callers: process_raise (the unified RAISE ingress in eval.c).
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
             already crossed (dyn_top is the GEN itself). Finish it --
             mark the owner closed, unlink the GEN, drop its chain refs
             -- and continue the walk so the raise propagates into the
             consumer's regions, per Python generator semantics. */
          {
            PyObject *owner = KosDynGen_OWNER(top);
            KosIterSlots *islots;
            if (owner && KosIter_CheckSlots(owner, &islots))
              islots->ki_finish(owner, 1);
          }
          {
            PyObject *typ = NULL, *val = NULL, *tb = NULL;
            KosDynNode *consumer = KosDynNode_PARENT(top);

            PyErr_Fetch(&typ, &val, &tb);
            kos_dyn_commit_dyn_top(es, consumer);
            KosDynNode_PARENT(top) = NULL;
            kos_dyngen_keepalive_release((KosDynGen *) top);
            kos_dyngen_release((KosDynGen *) top);
            PyErr_Restore(typ, val, tb);
          }
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
          PyObject *typ = NULL, *val = NULL, *tb = NULL;
          PyErr_Fetch(&typ, &val, &tb);
#if PY_MINOR_VERSION < 12
          PyErr_NormalizeException(&typ, &val, &tb);
#endif
          PyErr_SetExcInfo(typ, val, tb);
          PyErr_Clear();
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

    /* A frame carrying a CPS resume is offered the pending exception
       before it is torn through: the resume may consume (REPROLOGUE)
       or decline (EVAL_RAISE), in which case the pop below proceeds.
       This is what makes CallResult's "NULL with exception set"
       delivery real -- without this offer a resume set on a frame that
       fails mid-call is never invoked at all. */
    if (KosDynNode_IS_FRAME(top)
        && KosDynFrame_RESUME_FN((KosDynFrame *) top)) {
      KosDynFrame *frame = (KosDynFrame *) top;
      KosResumeFn resume = (KosResumeFn) KosDynFrame_RESUME_FN(frame);
      void *state = KosDynFrame_RESUME_STATE(frame);
      KosOpResult hr;

      KosDynFrame_RESUME_FN(frame) = NULL;
      KosDynFrame_RESUME_STATE(frame) = NULL;

      hr = resume(es, state, NULL);
      if (hr.kind == KOS_RES_NORMAL || hr.kind == KOS_RES_REPROLOGUE)
        return EVAL_REPROLOGUE();
      if (hr.kind == KOS_RES_FATAL)
        return EVAL_FATAL();
      /* declined: fall through to the pop */
    }

    /* PASS: unwind and pop */
    {
      int top_may_swallow = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_CATCH));
      uint32_t catch_resume_pc = kos_dynnode_catch_resume_pc(top);
      int top_is_wind = (top && KosDynNode_IS_WIND(top));
      int needs_preserve = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP))
          || (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER))
          || top_is_wind;
      if (needs_preserve && PyErr_Occurred())
        PyErr_Fetch(&saved_exc_typ, &saved_exc_val, &saved_exc_tb);

      /* A wind whose parked exception is being superseded by the
         in-flight one (its thunk raised, or it never resumed): chain the
         parked original as __context__ before the pop discards the node. */
      if (top_is_wind && saved_exc_typ != NULL
          && kos_ctx_exc_stash_top_is(es, KosDynNode_NODE_ID(top))) {
        kos_ctx_exc_stash_chain_top(es, KosDynNode_NODE_ID(top),
                                    &saved_exc_typ, &saved_exc_val,
                                    &saved_exc_tb);
      }

      KosOpResult pop_res = kos_dyn_pop_nonbarrier_top(es, KOS_XFER_EVENT_RAISE);
      if (saved_exc_typ != NULL) {
        if (top_is_wind
            && (pop_res.kind == KOS_RES_REPROLOGUE
                || pop_res.kind == KOS_RES_AWAIT)) {
          /* After-thunk started: park the exception on the context stash
             so the thunk runs with no pending error; on_control_returned
             re-raises it once the thunk completes. */
          if (kos_ctx_exc_stash_push(es, KosDynNode_NODE_ID(top),
                                     saved_exc_typ, saved_exc_val,
                                     saved_exc_tb) < 0) {
            return EVAL_FATAL();
          }
        } else if (PyErr_Occurred()
                   && ! KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
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
        if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_HANDLES_EXCEPTIONS)) {
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
        if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER)) {
          /* Barrier with no working recovery: a legitimate walk exit. */
          SAY("[try_local] barrier without recovery -> RAISE\n");
          kos_eval_prepare_boundary_raise(ctx, es);
          return EVAL_RAISE();
        }
        /* User teardown raised: the superseding exception (original
           chained above) keeps walking. If the failing pop left the node
           in place, discard it without re-running its teardown - it had
           its chance - so the walk always makes progress. */
        if (KosEvalState_DYN_TOP(es) == top) {
          KosDynNode *parent = KosDynNode_PARENT(top);
          kos_dyn_commit_dyn_top(es, parent);
          KosDynNode_PARENT(top) = NULL;
          kos_dynnode_free(top);
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
