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

  /* GEN is frozen back into the KPAG: drop the keepalive.  Done last, as it
     may run the owner's deallocation if no one else holds it (an abandoned
     generator cleans up here).  No reads of gen/owner follow. */
  kos_dyngen_keepalive_release(gen);
  return 0;
}


int kos_dyn_walk_handle_yield(KosEvalState *es) {
  KosDynNode *node = KosEvalState_DYN_TOP(es);

  SAYF("[gen] walk_handle_yield ENTER dyn_top=%p\n", (void *) node);
  while (node) {
    KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_YIELD);
    SAYF("[gen] walk_handle_yield node=%p kind=%s hr=%d\n",
         (void *) node, kos_dynnode_kind_name(KosDynNode_KIND(node)),
         (int) hr);

    if (hr == KOS_HANDLE_CONSUMED) {
      if (KosDynNode_IS_GEN(node)) {
        SAYF("[gen] walk_handle_yield -> freeze at gen %p\n", (void *) node);
        int fr = kos_dyn_gen_freeze_and_release(es, (KosDynGen *) node);
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
      /* Exception propagates past the prompt boundary; continue unwinding */
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
   Pop until dyn_top == target or empty. PROMPT uses kos_dyn_pop_prompt so
   walks can cross delimiters. Barriers are popped here because these walks
   deliberately unwind through delimited prompt boundaries. Callers:
   transfer helpers, targeted unwind.
*/
int kos_dyn_unwind_to_node(KosEvalState *es, const KosDynNode *target,
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
      return m;
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
                                  PyObject *val) {

  if (KOS_XFER_TRACE)
    SAYF("[xfer] walk_return_to_frame: target frame=%p", (void *) frame);

  while (KosEvalState_DYN_TOP(es) != NULL
         && KosEvalState_DYN_TOP(es) != (KosDynNode *) frame) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);

    /* Stopping node: a live delimited-invoke bookmark.  The return has
       reached the reset boundary; leave the DELIM at dyn_top and let the
       caller route val to the k-invocation site.  val is NOT consumed. */
    if (KosDynNode_IS_DELIM(top)
        && KosDynDelim_RETURN_FRAME((KosDynDelim *) top) != NULL
        && KosDynDelim_RETURN_PC((KosDynDelim *) top) != (uint32_t) -1) {
      if (KOS_XFER_TRACE)
        SAYF("[xfer] walk_return: stopped at DELIM bookmark node=%p",
             (void *) top);
      return KOS_DYN_UNWIND_DELIM_TAKING_RETURN;
    }

    /* Handle phase: offer RETURN before unwind */
    {
      KosHandleResult hr = kos_dyn_handle(es, top, KOS_XFER_EVENT_RETURN);
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
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    int m = kos_map_pop_result(
        kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_RETURN));
    if (m) {
      Py_DECREF(val);
      return m;
    }
  }
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
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    int m = kos_map_pop_result(kos_dyn_pop_top_allow_barrier(es, event));
    if (m)
      return m;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_frame: done, dyn_top=%p", (void *) KosEvalState_DYN_TOP(es));
  return 0;
}


/**
   Nearest LOOP or FOREACH from dyn_top. Callers: ITER_NEXT opcode and the
   transfer-to-loop walk serving BREAK/CONTINUE transitions.
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
                             PyObject *result) {

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
    int m = kos_map_pop_result(kos_dyn_pop_nonbarrier_top(es, event));
    if (m)
      return m;
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
   FINALLY consumes (jump to handler) or no handler remains.
   Returns EVAL_REPROLOGUE if a handler was applied, EVAL_RAISE if no handler
   (PyErr set and boundary raise prepared), EVAL_AWAIT if async unwind started.
   Callers: process_raise (the unified RAISE ingress in eval.c).
*/
KosOpResult kos_dyn_try_local_raise_body(KosEvalState *es) {
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

    /* Frame boundary: cross into the caller section and keep walking.
       Handler discovery is node-authoritative - a live TRY/FINALLY node
       consumes via handle(); the extent table only supplies jump
       positions at node push time. The raise leaves the loop only at
       the chain root (NULL parent) or a generator boundary. */
    if (KosEvalState_DYN_TOP(es) == (KosDynNode *) KosEvalState_FRAME(es)) {
      if (! PyErr_Occurred())
        break;

      {
        KosDynNode *parent = KosDynNode_PARENT(KosEvalState_DYN_TOP(es));
        if (! parent)
          break;
        if (KosDynNode_IS_GEN(parent)) {
          if (! KosDynNode_PARENT(parent))
            /* Boundary-driven generator: the GEN is the context root and
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
            struct KosPyAsyncGen *agen =
                (struct KosPyAsyncGen *) KosDynGen_OWNER(parent);
            if (agen) {
              agen->closed = 1;
              agen->in_progress = 0;
            }
          }
          {
            PyObject *typ = NULL, *val = NULL, *tb = NULL;
            PyErr_Fetch(&typ, &val, &tb);
            if (kos_dyn_gen_unwind_exhausted(es) < 0) {
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
        || KosDynNode_HAS_CAP(top, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)) {

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
            /* Boundary-driven generator: the GEN is the context root and
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
            struct KosPyAsyncGen *agen =
                (struct KosPyAsyncGen *) KosDynGen_OWNER(top);
            if (agen) {
              agen->closed = 1;
              agen->in_progress = 0;
            }
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

        if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)) {
          /* The boundary consumed the step-ending exception from its
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
          if (kos_dyn_iter_boundary_deliver_exhausted(es, top, stop_val) < 0) {
            if (PyErr_Occurred())
              /* the boundary re-emitted (e.g. 1-arg NEXT re-raising
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
          if (KosEvalState_StackClearBlock(es) < 0) {
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
/**
   Walk from DYN_TOP toward the given prompt, offering handle(SHIFT) to
   each node. GEN denies (returns -1); PROMPT consumes (success).
   Call before building a PUSH_SHIFT unwind plan.

   :return: 0 when we reach prompt and it consumes, -1 on error (GEN denies)
*/
int kos_dyn_walk_handle_shift_to_prompt(KosEvalState *es,
                                               KosDynPrompt *prompt) {

  KosDynNode *node = KosEvalState_DYN_TOP(es);
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  while (node && node != prompt_node) {
    KosHandleResult hr = kos_dyn_handle(es, node, KOS_XFER_EVENT_SHIFT);
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
                                        KOS_XFER_EVENT_SHIFT);
    if (hr != KOS_HANDLE_CONSUMED) {
      PyErr_SetString(PyExc_RuntimeError,
                      "prompt did not consume handle(SHIFT)");
      return -1;
    }
  }
  return 0;
}
