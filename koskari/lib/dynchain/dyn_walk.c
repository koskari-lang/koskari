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
  Included by dynchain.c. The chain walks: raise, return, loop and
  targeted unwinds, and the pop primitives they share.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Dynamic chain walk and pop primitives === */

int kos_dyn_gen_freeze(KosContext *ctx, KosDynGen *gen) {
  KosDynNode *saved_parent;
  KosDynNode *ctx_active;
  PyObject *owner;

  SAYF("[gen] freeze ENTER ctx=%p gen=%p dyn_top=%p (%s) gen_parent=%p\n",
       (void *) ctx, (void *) gen,
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosDynNode_PARENT(gen));

#if VERBOSE_DEBUG
  if (KosContext_DYN_TOP(ctx)) {
    KosDynNode *n;
    SAY("[gen] freeze chain from dyn_top: ");
    for (n = KosContext_DYN_TOP(ctx); n; n = KosDynNode_PARENT(n))
      SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }
#endif

  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_freeze: no dyn_top");
    return -1;
  }

#if KOS_EVAL_SAFETYNET
  {
    /* gen came from kos_dyn_find_gen on the running frame, which sits
       on dyn_top's path; the span from dyn_top must reach it. */
    KosDynNode *node;
    for (node = KosContext_DYN_TOP(ctx); node; node = KosDynNode_PARENT(node)) {
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
  ctx_active = KosContext_DYN_TOP(ctx);
  KosDynGen_ACTIVE(gen) = ctx_active;
  kos_live_regs_capture(ctx, KosDynGen_FROZEN(gen));

  /* An effect a node holds outside itself leaves with it, innermost
     first; the install puts it back, and only when one left. The
     span's interior unwind caches stay: they name nodes of this same
     span. The frame count serves the install's room check. */
  {
    unsigned nframes = 0;
    int installs = 0;

    for (KosDynNode *n = ctx_active; n; n = KosDynNode_PARENT(n)) {
      nframes += KosDynNode_IS_FRAME(n) ? 1u : 0u;
      installs |= kos_dyn_extent_freeze(ctx, n);
    }
    KosDynGen_NFRAMES(gen) = (uint16_t) nframes;
    if (installs)
      KosDynNode_FLAGS(gen) |= KOS_DYNF_GEN_INSTALLS;
    else
      KosDynNode_FLAGS(gen) &= ~KOS_DYNF_GEN_INSTALLS;
  }
  /* The GEN's namespace fill is the consumer's live namespace stamped
     at the install; the next install stamps it again from that
     consumer's registers. Holding it across the freeze would keep the
     consumer's frame namespace referenced from a suspended generator
     the consumer itself may hold. */
  Py_CLEAR(KosDynNode_NS(gen));

#if VERBOSE_DEBUG
  {
    KosDynNode *n;
    SAY("[gen] freeze storing in gen->active: chain ");
    for (n = ctx_active; n; n = KosDynNode_PARENT(n))
      SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }
#endif

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
    kos_live_regs_apply(ctx, KosDynGen_HOST(gen));
  } else {
    KosContext_DYN_TOP(ctx) = saved_parent;
    kos_dyn_resync_chain(ctx);
  }
  KosDynGen_HOST(gen)->top = NULL;

  owner = KosDynGen_OWNER(gen);

  /* Release only when owner holds the gen; standalone gen is kept for resume */
  if (owner)
    kos_dyngen_release(gen);

  SAYF("[gen] freeze DONE ctx->dyn_top=%p (%s) frame_top=%p owner=%p gen=%p\n",
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosContext_FRAME(ctx),
       (void *) owner,
       (void *) gen);

  /* GEN is frozen back into the KPAG: drop the keepalive.  Done last, as it
     may run the owner's deallocation if no one else holds it (an abandoned
     generator cleans up here).  No reads of gen/owner follow. */
  kos_dyngen_keepalive_release(gen);
  return 0;
}


KosOpResult kos_dyn_exception_unwind(
    KosContext *ctx,
    KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_PROMPT:
      if (KOS_XFER_TRACE)
        SAYF("[xfer] prompt_unwind_exception node=%p: unlinking prompt", (void *) node);
      {
        KosOpResult kr = kos_dyn_pop_top_allow_barrier(ctx, KOS_XFER_EVENT_NONE);
        if (kr.kind != KOS_RES_NORMAL && kr.kind != KOS_RES_REPROLOGUE
            && kr.kind != KOS_RES_AWAIT)
          return kr;
      }
      /* Exception propagates past the prompt; continue unwinding */
      return EVAL_NORMAL();
    case KOS_DYN_DELIM: {
      int r = kos_dyn_pop_delim(ctx, (KosDynDelim *) node);
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
    KosContext *ctx,
    const KosDynNode *target,
    KosXferEvent event,
    KosDynNode **out_pending) {

  if (out_pending)
    *out_pending = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: target=%p kind=%s",
         (void *) target, target ? kos_dynnode_kind_name(KosDynNode_KIND(target)) : "NULL");

  while (KosContext_DYN_TOP(ctx) != NULL
         && KosContext_DYN_TOP(ctx) != target) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    KosOpResult r;

    if (KOS_XFER_TRACE)
      SAYF("[xfer] unwind_to_node: popping node=%p kind=%s",
           (void *) top, kos_dynnode_kind_name(KosDynNode_KIND(top)));
    KOS_TRACE_EVENTS_INC(KOS_TE_DYN_UNWIND_STEP);
    r = kos_dyn_unlink_top(ctx, event, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    /* the node's leave is in flight: the caller stores what it carries
       on it, and that resumes once the node has left the chain */
    if (out_pending
        && (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT))
      *out_pending = top;
    return r;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_to_node: done, dyn_top=%p", (void *) KosContext_DYN_TOP(ctx));
  return EVAL_NORMAL();
}


/**
   A FINALLY that consumed a control transfer takes it over: the operand
   stack is cleared and the transfer's value pushed for the handler,
   the pc moves to the handler, the node is disabled, and the pending
   kind is recorded so POP_FINALLY re-issues the transfer once the body
   is done. val is consumed. walk names the caller for the trace.
*/
static KosOpResult finally_take_control(
    KosContext *ctx,
    KosDynFinally *fin,
    PyObject *val,
    KosPendingReturnKind kind,
    const char *walk) {

  if (KosContext_StackClear(ctx) < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  if (KosContext_StackPushRef(ctx, val) < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  Py_DECREF(val);
  if (KosContext_FRAME(ctx))
    KosContext_SET_PC(ctx, KosDynFinally_HANDLER_PC(fin));
  KosDynFinally_ENABLED(fin) = 0;
  KosDynFinally_HANDLING_CONTROL(fin) = kind;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] %s: delivered to FINALLY, pc=%u", walk,
         (unsigned) KosDynFinally_HANDLER_PC(fin));
  return EVAL_REPROLOGUE();
}


KosOpResult kos_dyn_finally_take_transfer(
    KosContext *ctx,
    KosDynFinally *fin) {

  Py_INCREF(Py_None);
  return finally_take_control(ctx, fin, Py_None,
                              KOS_PENDING_RETURN_TRANSFER, "goal_leave");
}


/**
   RETURN unwind walk to frame. See dynchain.h for the contract. Caller:
   eval.c eval_event_return.
*/
KosOpResult kos_dyn_unwind_return(
    KosContext *ctx,
    KosDynFrame *frame,
    PyObject *val,
    KosPendingReturnKind kind,
    KosDynDelim **took) {

  *took = NULL;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] unwind_return: target frame=%p", (void *) frame);

  while (KosContext_DYN_TOP(ctx) != NULL
         && KosContext_DYN_TOP(ctx) != (KosDynNode *) frame) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
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
      KosHandleResult hr = kos_dyn_handle(ctx, top, KOS_XFER_EVENT_RETURN);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top))
          return finally_take_control(ctx, (KosDynFinally *) top, val,
                                      KOS_PENDING_RETURN_FRAME,
                                      "unwind_return");
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
    r = kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_RETURN, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      /* The node's leave started and will suspend; val is stored on the node
         and the return resumes once the node has left the chain. */
      if (kos_dyn_ob_store_return(top, val, (uint8_t) kind, NULL) < 0) {
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
         (void *) KosContext_DYN_TOP(ctx));
  return EVAL_NORMAL();
}


/**
   Find the nearest enabled LOOP or FOREACH from dyn_top. A loop whose
   else clause is running has been disabled by CLEAR_LOOP or
   CLEAR_FOREACH and is finished: a break or continue issued there
   belongs to the loop enclosing it. A live delimited-invoke bookmark
   ends the search: the resumed slice above it is the continuation's
   body, and a loop below it (the one around the reset, or one in the
   shift handler) is outside that body, as a return or a raise in the
   slice is delivered to the (k ...) call site and not past it.
   Read-only; never sets PyErr. Callers: opcode/ITER_NEXT.c and
   kos_dyn_unwind_to_loop serving BREAK/CONTINUE.
*/
KosDynNode *kos_dyn_find_loop(KosContext *ctx) {
  KosDynNode *node = KosContext_DYN_TOP(ctx);
  while (node) {
    if (kos_dyn_loop_is_enabled(node))
      return node;
    if (KosDynNode_IS_DELIM(node)
        && KosDynDelim_RETURN_FRAME((KosDynDelim *) node) != NULL
        && KosDynDelim_RETURN_PC((KosDynDelim *) node) != (uint32_t) -1)
      return NULL;
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
    KosContext *ctx,
    KosXferEvent event,
    PyObject *result) {

  KosDynNode *region = kos_dyn_find_loop(ctx);
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
    KosDynFrame *frame = KosContext_FRAME(ctx);
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
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    KosOpResult r;

    /* Handle phase: offer BREAK/CONTINUE before unwind, as the return
       walk offers RETURN. An enabled FINALLY consumes it and the value
       rides its stack through the finally body; POP_FINALLY re-issues
       the transfer once the region is gone. */
    {
      KosHandleResult hr = kos_dyn_handle(ctx, top, event);
      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_FINALLY(top))
          return finally_take_control(
              ctx, (KosDynFinally *) top, result,
              (event == KOS_XFER_EVENT_BREAK)
                  ? KOS_PENDING_RETURN_BREAK : KOS_PENDING_RETURN_CONTINUE,
              "unwind_to_loop");
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

    /* The loop lies under the current frame, so every node between
       is the frame's own: a reset's PROMPT and a SHIFT leave like any
       other node, as they do for the return walk. */
    r = kos_dyn_unlink_top(ctx, event, 1);

    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      /* The node's leave started and will suspend: result is stored on
         the node with the event as its path, and the walk resumes
         from here once the node has left the chain, as a return does. */
      uint8_t path = (event == KOS_XFER_EVENT_BREAK)
          ? KOS_PENDING_RETURN_BREAK : KOS_PENDING_RETURN_CONTINUE;

      if (kos_dyn_ob_store_return(top, result, path, NULL) < 0) {
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
    Py_XSETREF(KosDynRegion_RESULT(region), result);

    if (KosContext_StackClear(ctx) < 0)
      return EVAL_RAISE();

    KosContext_SET_PC(ctx, KosDynRegion_CONTINUE_PC(region));
    return EVAL_NORMAL();
  }

  /* KOS_XFER_EVENT_BREAK: read break_pc before popping, then pop, drop block,
     push result */
  uint32_t break_pc = KosDynRegion_BREAK_PC(region);

  if (KosDynNode_IS_LOOP(region)) {
    if (kos_dyn_pop_loop(ctx, (KosDynLoop *) region) < 0) {
      Py_DECREF(result);
      return EVAL_RAISE();
    }
  } else if (kos_dyn_pop_foreach(ctx, (KosDynForeach *) region) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }

  if (KosContext_StackPushRef(ctx, result) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }
  Py_DECREF(result);

  KosContext_SET_PC(ctx, break_pc);
  return EVAL_NORMAL();
}



/**
   PEP 479 and PEP 525 at a generator's boundary: a StopIteration or
   StopAsyncIteration escaping the body is not the protocol's own end
   signal, so it becomes a RuntimeError with the original as its
   cause before any consumer -- the flattened consumer's step resume,
   the Python iterator adapter, the async-generator stepper -- can read it
   as exhaustion. Any other pending exception passes untouched.
*/
static void kos_dyn_gen_convert_escaping_stop(void) {
  if (PyErr_ExceptionMatches(PyExc_StopIteration))
    _PyErr_FormatFromCause(PyExc_RuntimeError,
                           "generator raised StopIteration");
  else if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
    _PyErr_FormatFromCause(PyExc_RuntimeError,
                           "generator raised StopAsyncIteration");
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
KosOpResult kos_dyn_unwind_raise(KosContext *ctx) {
  #if VERBOSE_DEBUG
  if (! ctx) {
    SAY("[try_local] ctx=NULL\n");
  }
  else if (KosContext_FRAME(ctx)) {
    size_t _try_local_stack_len = 0;
    _try_local_stack_len = KosContext_StackLen(ctx);
    SAYF("[try_local] enter dyn_top=%p frame_top=%p acting_pc=%u stack_len=%zu\n",
         (void *) KosContext_DYN_TOP(ctx),
         (void *) KosContext_FRAME(ctx),
         (unsigned) KosContext_ACTING_PC(ctx),
         (size_t) _try_local_stack_len);
  }
  else {
    SAYF("[try_local] enter dyn_top=%p frame_top=NULL\n",
         (void *) KosContext_DYN_TOP(ctx));
  }

  #endif

  PyObject *saved_exc_typ = NULL;
  PyObject *saved_exc_val = NULL;
  PyObject *saved_exc_tb = NULL;

  while (KosContext_FRAME(ctx) && KosContext_DYN_TOP(ctx) != NULL) {

    /* The live frame's resume is first at every level: a frame pop or
       a generator teardown that reveals a frame with a resume hands
       the walk back to process_raise, which bounces to the prologue's
       delivery step, where the resume may thread on. A decline clears
       the resume and re-enters the walk from this same dyn_top. */
    if (KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx)))
      return EVAL_REPROLOGUE();

    /* Frame crossing: cross into the caller section and keep walking.
       Handler discovery is node-authoritative - a live TRY/FINALLY node
       consumes via handle(); the extent table only supplies jump
       positions at node push time. The raise leaves the loop only at
       the chain root (NULL parent) or a generator node. */
    if (KosContext_DYN_TOP(ctx) == (KosDynNode *) KosContext_FRAME(ctx)) {
      if (! PyErr_Occurred())
        break;

      /* A code-less step frame's resume has already seen this
         exception: process_raise hands it to the prologue, which
         delivers it as a NULL against a settled chain, and the walk
         only runs once that resume declined and cleared itself. */

      {
        KosDynNode *parent = KosDynNode_PARENT(KosContext_DYN_TOP(ctx));
        if (! parent)
          break;
        if (KosDynNode_IS_GEN(parent)) {
          kos_dyn_gen_convert_escaping_stop();
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
          kos_eval_note_crossed_frame(ctx);
          {
            PyObject *owner = KosDynGen_OWNER(parent);
            KosIterSlots *islots;
            if (owner && KosIter_CheckSlots(owner, &islots))
              islots->ki_finish(owner, 1);
          }
          {
            PyObject *typ = NULL, *val = NULL, *tb = NULL;
            PyErr_Fetch(&typ, &val, &tb);
            if (kos_dyn_teardown_gen_exhausted(ctx) < 0) {
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
        kos_eval_note_crossed_frame(ctx);
        if (kos_dyn_pop_frame(ctx) < 0)
          return EVAL_FATAL();
      }
      continue;
    }

    KosDynNode *top = KosContext_DYN_TOP(ctx);

    KOS_TRACE_EVENTS_INC(KOS_TE_RAISE_LOCAL_WALK_ITER);

    SAYF("[try_local] walk top=%p kind=%s\n",
         (void *) top,
         kos_dynnode_kind_name(KosDynNode_KIND(top)));

    /* Cap fast check: nodes that intercept RAISE are dispatched to the
       handle phase; everything else goes straight to unwind/pop. */
    if (KosDynNode_HAS_CAP(top, KOS_DYN_CAP_INTERCEPTS_RAISE)) {

    /* In-place delimited return bookmark: the slice raised. Restitch the
       invoke span and continue the raise from the invoke site. */
    if (KosDynNode_IS_DELIM(top)) {
      PyObject *typ = NULL, *val = NULL, *tb = NULL;
      PyErr_Fetch(&typ, &val, &tb);
      if (kos_dyn_inplace_restitch(ctx, (KosDynDelim *) top) < 0) {
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
      KosHandleResult hr = kos_dyn_handle(ctx, top, KOS_XFER_EVENT_RAISE);
      SAYF("[try_local] kos_dyn_handle RAISE hr=%d (%s)\n",
           (int) hr,
           hr == KOS_HANDLE_CONSUMED ? "CONSUMED"
             : hr == KOS_HANDLE_PASS ? "PASS"
             : hr == KOS_HANDLE_ERROR ? "ERROR" : "?");

      if (hr == KOS_HANDLE_CONSUMED) {
        if (KosDynNode_IS_GEN(top)) {
          kos_dyn_gen_convert_escaping_stop();
          if (! KosDynNode_PARENT(top)) {
            /* Adapter-driven generator: the GEN is the context root and
               the raise exits to the Python driver, which translates it. */
            kos_eval_prepare_boundary_raise(ctx);
            return EVAL_RAISE();
          }

          /* Flattened generator whose frame the earlier raise phases
             already crossed (dyn_top is the GEN itself). Finish it and
             continue the walk so the raise propagates into the
             consumer's regions, per Python generator semantics. */
          kos_dyn_finish_flattened_gen(ctx, (KosDynGen *) top);
          continue;
        }

        KosDynNode *region = top;
        uint32_t handler_pc =
            kos_dynnode_raise_handler_pc_for_try_finally(ctx, region);

        if (KosDynNode_IS_TRY(region) || KosDynNode_IS_FINALLY(region)) {
          if (KosContext_StackClear(ctx) < 0) {
            return EVAL_FATAL();
          }
        }

        SAYF("[try_local] CONSUMED region=%s handler_pc=%u\n",
             KosDynNode_IS_TRY(region) ? "TRY" : "FINALLY",
             (unsigned) handler_pc);
        KosContext_SET_PC(ctx, handler_pc);

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
          /* The handler reads e.__traceback__: give the exception a
             traceback entry for every Koskari frame the raise crossed
             and for this frame at the raising pc, as the boundary does
             when an exception leaves to Python. */
          kos_eval_note_crossed_frame(ctx);
          KosExc_TakeRaised(&raised);
          kos_eval_inject_recorded_frames(&raised.type, &raised.value,
                                          &raised.tb);
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
         exception in flight is held here and stored on the node once
         the thunk has started. A wind (or with) whose thunk is dead is
         aborted by the unlink and never fires, so the exception stays
         pending for the unlink to chain what the node was holding. A
         with fires __exit__ with the exception as its argument and
         stores it itself. */
      int needs_preserve = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP))
          || top_is_barrier
          || (top_is_wind && ! kos_dyn_extent_in_flight(top));
      if (needs_preserve && PyErr_Occurred())
        PyErr_Fetch(&saved_exc_typ, &saved_exc_val, &saved_exc_tb);

      KosOpResult pop_res = kos_dyn_pop_nonbarrier_top(ctx, KOS_XFER_EVENT_RAISE);
      if (saved_exc_typ != NULL) {
        if (top_is_wind
            && (pop_res.kind == KOS_RES_REPROLOGUE
                || pop_res.kind == KOS_RES_AWAIT)) {
          /* After-thunk started: store the exception on the wind so the
             thunk runs with no pending error; the unlink re-raises it
             once the thunk completes. */
          if (kos_dyn_ob_store_exception(top, saved_exc_typ, saved_exc_val,
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
          KosOpResult exc_res = kos_dyn_exception_unwind(ctx, top);
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
          kos_eval_prepare_boundary_raise(ctx);
          return EVAL_RAISE();
        }
        /* A leave raised: the superseding exception keeps walking. The
           failing node is still in place, its call dead at dyn_top, so
           the unlink aborts it without re-running anything and chains
           what it was holding under the new exception. */
        if (KosContext_DYN_TOP(ctx) == top) {
          KosOpResult again = kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_RAISE, 1);
          if (again.kind != KOS_RES_NORMAL && again.kind != KOS_RES_RAISE) {
            Py_XDECREF(again.val);
            return EVAL_FATAL();
          }
          if (KosContext_DYN_TOP(ctx) == top) {
            /* the node refused to leave twice: no progress possible */
            PyErr_SetString(PyExc_SystemError,
                            "raise walk: a node's leave keeps failing");
            return EVAL_FATAL();
          }
        }
        continue;
      }
      if (top_may_swallow && ! PyErr_Occurred()) {
        if (catch_resume_pc != (uint32_t) -1 && KosContext_FRAME(ctx)) {
          KosContext_SET_PC(ctx, catch_resume_pc);
        }
        return EVAL_REPROLOGUE();
      }
    }
  }

  /* No handler found */
  {
    SAY("[try_local] no handler found -> RAISE\n");
    kos_eval_prepare_boundary_raise(ctx);
    return EVAL_RAISE();
  }
}


// The end.
