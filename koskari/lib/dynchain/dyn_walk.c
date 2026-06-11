/* Included by dynchain.c — dyn_walk */
/* === Dynamic chain walk and pop primitives === */

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
   When dyn_top is the current frame, consult the extent table for except_pc
   before walking the chain.

   :return: 1 if a handler was applied, 0 if none, -1 on error
*/
static int try_local_extent_on_current_frame(KosEvalState *es, KosEvent *out_ev) {

  (void) out_ev;

  KosDynFrame *frame = KosEvalState_FRAME(es);
  if (! frame || ! PyErr_Occurred())
    return 0;

  KosTryHandler th;
  if (kos_try_handler_for((PyObject *) KosDynFrame_CODE(frame),
                          KosEvalState_ACTING_PC(es), &th) != 0
      || th.except_pc == KOS_EXTENT_NONE)
    return 0;

  if (KosEvalState_StackClearBlock(es) < 0)
    return -1;

  SAYF("[try_local] extent handler pc=%u on current frame\n",
       (unsigned) th.except_pc);
  KosEvalState_SET_PC(es, th.except_pc);

  {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_SetExcInfo(typ, val, tb);
    PyErr_Clear();
  }

  return 1;
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

  #endif

  PyObject *saved_exc_typ = NULL;
  PyObject *saved_exc_val = NULL;
  PyObject *saved_exc_tb = NULL;

  if (KosEvalState_FRAME(es) && KosEvalState_DYN_TOP(es) != NULL
      && KosEvalState_DYN_TOP(es) == (KosDynNode *) KosEvalState_FRAME(es)
      && PyErr_Occurred()) {
    int extent = try_local_extent_on_current_frame(es, out_ev);
    if (extent != 0)
      return extent;

    {
      KosDynNode *parent = KosDynNode_PARENT(KosEvalState_DYN_TOP(es));
      if (parent && ! KosDynNode_IS_GEN(parent)) {
        if (kos_dyn_pop_frame(es) < 0)
          return -1;
      }
    }
  }

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
