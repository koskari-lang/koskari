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
  koskari/lib/eval/eval_call.c

  The call core: frame prep, the KosEvalState_Call* entries, and
  kos_call_invoke with its per-flag bodies for the CALL* opcode
  prologues. Included into eval.c as a segment (see the eval/ includes
  there).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/**
  Prepare frame stack for a KosCallable install per wants_frame / tail_call.
  Inlines effective wants_frame resolution (hook vs static flag).

  A callable that wants no frame leaves the frame stack untouched, tail
  call or not. It either delivers a value onto the caller's stack, and
  the RETURN that follows every TAIL_CALL then retires the frame, or it
  forwards the call with the tail flag intact to a target whose own
  prep reuses this same frame. Popping the caller here would make that
  forwarded prep act on the caller's parent instead.
*/
static inline int do_frame_prep(
    KosEvalState *es,
    PyObject *callable,
    KosCallSlots *slots,
    int tail_call) {

  int wants = KosCallSlots_WantsFrame(slots, callable);

  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (wants && tail_call) {
    /* Uninstall discardable extents left open above the frame before
       the reuse; their POP_* rows are unreachable on the tail path. */
    if (kos_dyn_teardown_to_frame(es, frame) < 0)
      return -1;
    kos_dynframe_reset_for_tail(frame);
    KosEvalState_CODE(es) = NULL;
    KosEvalState_NAMESPACE(es) = NULL;
    KosEvalState_BLOCK(es) = &KosDynFrame_STACK(frame);
    (es)->pc = 0;
    (es)->acting_pc = 0;
    return 0;
  }

  if (wants && ! tail_call) {
    if (kos_dyn_push_frame_empty(es) == NULL)
      return -1;
    return 0;
  }

  return 0;
}


/**
   The result of an install slot that ran on a prepared frame for a
   native entry (KosEvalState_Call and the CallArgs pair): a frame the
   slot filled is a callee entry, and so a cooperation point; a slot
   that wanted no frame delivered a value and its result stands. POLL
   is a handler-relay answer, so a native caller gets REPROLOGUE for
   it and bounces through the trampoline, which polls ahead of the
   prologue.
*/
static inline KosOpResult kos_call_slot_result(
    KosEvalState *es,
    KosCallSlots *slots,
    PyObject *callable,
    KosOpResult r) {

  if (r.kind == KOS_RES_NORMAL && KosCallSlots_WantsFrame(slots, callable))
    return KOS_EVAL_BREAKER_UP(es) ? EVAL_REPROLOGUE() : EVAL_NORMAL();
  return r;
}


/**
   Tail-call preamble for a plain-Python callable: pop the frame when it
   is dyn_top, or defer delivery to a normal RETURN when extent nodes are
   still open above it.  Sets *tail_defer_return accordingly.

   :return: 0 on success, -1 on error
*/
static int tail_prep_py_call(
    KosEvalState *es,
    int tail_call,
    int *tail_defer_return) {

  *tail_defer_return = 0;
  if (! tail_call)
    return 0;

  {
    KosDynFrame *frame = KosEvalState_FRAME(es);
    KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
    if (! parent)
      return 0;

    if (KosEvalState_DYN_TOP(es) == (KosDynNode *) frame)
      return kos_dyn_pop_frame(es);
  }

  /* Extent nodes are still open above the frame; skip the tail pop and
     deliver the result through a normal RETURN so they unwind properly. */
  *tail_defer_return = 1;
  return 0;
}


/**
   Deliver a plain-Python call result: push-or-await, then map tail-call
   completion to RETURN (deferred) or CONTROL_RETURNED.  result consumed.
*/
static KosOpResult deliver_py_call_result(
    KosEvalState *es,
    PyObject *result,
    int tail_call,
    int tail_defer_return) {

  KosOpResult por = push_or_await(es, result, 0, 0);
  if (por.kind == KOS_RES_NORMAL && tail_call) {
    if (tail_defer_return) {
      PyObject *rv = KosEvalState_StackPopRef(es);
      if (! rv)
        return EVAL_RAISE();
      return EVAL_RETURN(rv);
    }
    return EVAL_CONTROL_RETURNED();
  }
  return por;
}


static inline KosOpResult kos_call_invoke_body(
    KosEvalState *es,
    KosSegment **caller_slot,
    size_t window,
    PyObject *callable,
    PyObject *const *argv,
    Py_ssize_t nargs,
    PyObject *kwnames,
    unsigned flags);


KosOpResult KosEvalState_Call(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosCallSlots *slots;

  /* Exact simple-arity KosFunction: the shared call core, whose
     parked-frame reuse the legacy install path lacks. */
  if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && ! tail_call && ! kwnames
      && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
      && KosFunction_BIND_SELF_STATE(callable) == 2) {
    KosSegment *dummy = NULL;
    KosOpResult r = kos_call_invoke_body(es, &dummy, 0, callable, args,
                                         nargs, kwnames, 0);
    /* POLL is a handler-relay answer; a native caller bounces through
       the trampoline, which polls ahead of the prologue as before. */
    if (r.kind == KOS_RES_POLL)
      return EVAL_REPROLOGUE();
    if (r.kind != KOS_RES_NORMAL)
      return r;
    if (KosEvalState_GOAL(es))
      return EVAL_REPROLOGUE();
    if (KosEvalState_FRAME(es)
        && KosDynFrame_RESUME_KIND(KosEvalState_FRAME(es)))
      return EVAL_REPROLOGUE();
    return EVAL_NORMAL();
  }

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      /* Instance with a KosCallable __call__: dispatch the unbound __call__
         with self injected as the first positional -- no bound-method,
         transient-string, or descriptor-bind allocation. */
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
        Py_ssize_t ntotal = nargs + nkw;
        PyObject *small[8];
        PyObject **inj = small;
        if (ntotal + 1 > (Py_ssize_t) (sizeof(small) / sizeof(small[0]))) {
          inj = PyMem_Malloc((size_t) (ntotal + 1) * sizeof(PyObject *));
          if (! inj)
            return EVAL_RAISE();
        }
        inj[0] = callable;
        if (ntotal)
          memcpy(inj + 1, args, (size_t) ntotal * sizeof(PyObject *));
        KosOpResult r = KosEvalState_Call(es, call_fn, inj, nargs + 1,
                                          kwnames, tail_call);
        if (inj != small)
          PyMem_Free(inj);
        return r;
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();
    /* nargs matches KosCallable_Call: positional count only. PEP 590:
       keyword values follow args[nargs .. nargs + nkw - 1]. */
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
    if (! result)
      return EVAL_RAISE();
    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallSlots(es, slots, callable, args, nargs, kwnames,
                            tail_call));
}


/**
   A CPS entry's value-in-hand outcome: settled, report RETURN with
   val and arm nothing -- the calling resume tail-calls resume with
   it; not yet settled, settle one step through KosEvalState_SettleStage
   and arm resume on caller for the trip that delivers it. val is
   consumed.
*/
static KosOpResult kos_entry_value(
    KosEvalState *es,
    KosDynFrame *caller,
    PyObject *val,
    const KosResumeKind *resume,
    void *state) {

  switch (KosEval_ResultIsValue(val)) {
    case 1:
      return EVAL_RETURN(val);

    case 0: {
      KosOpResult pr = KosEvalState_SettleStage(es, val);
      if (pr.kind == KOS_RES_REPROLOGUE || pr.kind == KOS_RES_AWAIT) {
        KosDynFrame_RESUME_KIND(caller) = resume;
        KosDynFrame_RESUME_STATE(caller) = state;
      }
      return pr;
    }

    default:
      Py_DECREF(val);
      return EVAL_RAISE();
  }
}


KosOpResult KosEvalState_CallResult(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state) {

  KosDynFrame *caller = KosEvalState_FRAME(es);
  KosOpResult r;

#if KOS_EVAL_SAFETYNET
  if (unlikely(tail_call && resume)) {
    PyErr_SetString(PyExc_SystemError,
                    "tail call with a resume: no caller frame to resume");
    return EVAL_RAISE();
  }
#endif

  r = KosEvalState_Call(es, callable, args, nargs, kwnames, tail_call);

  if (! resume)
    return r;

  switch (r.kind) {
    case KOS_RES_NORMAL:
      if (KosEvalState_FRAME(es) == caller) {
        /* Nothing was installed: the callable ran to a value that
           Call landed on the caller's own stack (a plain callable's
           result, a generator function's generator). Take it back
           and report it in hand. */
        PyObject *val = KosEvalState_StackPopRef(es);
        if (unlikely(! val))
          return EVAL_RAISE();
        return kos_entry_value(es, caller, val, resume, state);
      }
      /* fall through: callee installed and current */
    case KOS_RES_REPROLOGUE:
    case KOS_RES_POLL:
      /* callee installed above caller, or a plan armed by the install;
         caller's own frame is unchanged and still reachable -- its
         eventual return delivers here. Call never answers POLL; the
         arm keeps an installed callee from losing its resume should a
         path ever relay one. */
      KosDynFrame_RESUME_KIND(caller) = resume;
      KosDynFrame_RESUME_STATE(caller) = state;
      return r;

    case KOS_RES_AWAIT:
      /* the call's own result needed awaiting; the AWAITED revive
         delivers into resume once it settles. */
      KosDynFrame_RESUME_KIND(caller) = resume;
      KosDynFrame_RESUME_STATE(caller) = state;
      return r;

    case KOS_RES_RETURN:
      return kos_entry_value(es, caller, r.val, resume, state);

    default:
      return r;  /* RAISE / FATAL: relay untouched */
  }
}


KosOpResult KosEvalState_CallInto(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags) {

  /* The frame starts with no resume armed: an armed resume would make
     a value pushed onto the frame read as an installed callee, and
     skip its settling. CallResult arms it on every path but a value
     back in hand. */
  KosDynFrame *frame = KosResumeFrame_Install(es, NULL, NULL, NULL);
  KosOpResult r;

  if (! frame)
    return EVAL_RAISE();
  KosDynFrame_FLAGS(frame) |= frame_flags;

  r = KosEvalState_CallResult(es, callable, args, nargs, kwnames, 0,
                              resume, state);
  if (r.kind != KOS_RES_RETURN)
    return r;

  KosDynFrame_RESUME_KIND(frame) = resume;
  KosDynFrame_RESUME_STATE(frame) = state;
  KosEvalState_StageDelivery(es, r.val);
  return EVAL_REPROLOGUE();
}


/* === kos_call_invoke: the shared call core (call-unification Phase 2) ===

   The entry for the CALL_* opcode prologues: kos_call_invoke inlines
   into each handler (window math and self-arg prepend), delegating to
   kos_call_invoke_body, which picks the out-of-line class dispatch
   specialised for the handler's flags word.  argv is
   always a BORROWED read vector (vectorcall convention).  With
   KOS_CALL_ARGV_WINDOW, argv additionally names the caller's stack tail
   (nargs + kwname values, plus the callable itself under
   KOS_CALL_WINDOW_CALLABLE); the core owns dropping that window from
   the caller's block, sequenced per install class:

   - classes that deliver a result onto the CALLER's block (generator
     agen creation, plain-Python push-or-await, and every legacy install
     that may) drop the window BEFORE the delivery;
   - the frame-install class delivers nothing onto the caller's block,
     so it drops the window LAST, keeping the callable and args alive
     through the whole install with no extra refcounting.

   Legacy classes (KosCallables other than exact KosFunction, and
   instance-call injection) preserve today's steal-first ordering by
   spilling the window into an owned vector before delegating to
   KosEvalState_Call.  Tail calls ride the same entry under
   KOS_CALL_TAIL (see the flag notes below). */

#define KOS_CALL_PLAIN            0x0u
#define KOS_CALL_ARGV_WINDOW      0x1u
#define KOS_CALL_WINDOW_CALLABLE  0x2u
/* One extra window slot beneath argv (an on-stack receiver): dropped
   with the window, never the call target.  The receiver reaches the
   callee only via self_arg injection. */
#define KOS_CALL_WINDOW_UNDER     0x4u
/* Tail call: the caller's frame is reused.  Exact non-yielding
   KosFunctions take the bind-before-reset subpath (the frame reset's
   block-clear IS the window drop); everything else spills and keeps
   KosEvalState_Call's tail semantics. */
#define KOS_CALL_TAIL             0x8u
/* Internal, set by kos_call_invoke when self_arg was injected: argv is
   the prepend buffer, not the block tail, so the steal-bind fast path
   (which moves references directly off the block) must not run. */
#define KOS_CALL_ARGV_PREPENDED   0x10u
/* Caller transfers its callable reference to the core, which consumes
   it on every path: an install that stamps the frame's FN tag takes the
   reference (no fresh incref), and every other exit decrefs.  Handlers
   pass their fast-slot / resolver reference this way instead of
   dropping it after the invoke, saving a refcount round trip per call
   on the install paths.  The flag is a per-site literal, so the checks
   fold away in the inlined prologue. */
#define KOS_CALL_CALLABLE_OWNED   0x20u

#define KOS_CALL_SPILL_FAST 16


/**
   True when the live frame's namespace is provably private, so a
   self-tail call may rebind it in place.  Mirrors the privacy test in
   KosFunction_TryParkFrame: a captured, revealed, or dynamically
   extended namespace stays live-visible and takes the ordinary
   namespace rebuild instead.
*/
static inline int kos_tail_ns_reusable(PyObject *ns) {
  return ns
      && KosNamespace_CheckExact(ns)
      && Py_REFCNT(ns) == 1
      && ! PyObject_GC_IsTracked(ns)
      && KosNamespace_BINDS(ns) == NULL;
}


/* The class dispatch proper.  Always inlined into the per-flag bodies
   below, each of which passes a literal flags word, so every flag test
   in here folds away and each body carries only its own arms. */
KOS_OP_INLINE KosOpResult kos_call_invoke_impl(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;

  /* Class: exact KosFunction, tail position.  Bind-before-reset: the
     namespace is prepared FROM the still-intact window, then the frame
     reset's block-clear performs the window drop.  Requires the window
     to live in the reused frame's own block and non-yielding code;
     anything else falls to the legacy spill below (which preserves
     KosEvalState_Call's tail semantics exactly). */
  if (unlikely(flags & KOS_CALL_TAIL)) {
    KosDynFrame *frame = KosEvalState_FRAME(es);

    if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
        && frame
        && caller_slot == &KosDynFrame_STACK(frame)) {
      KosCode *code = (KosCode *) KosFunction_CODE(callable);

      if (! (code && KosCode_YIELDS(code))) {
        PyObject *frame_ns;

        /* Discardable extents open above the frame (a tail call from
           inside let / loop / foreach) uninstall now: the reused frame
           never reaches their POP_* rows.  The window is unaffected --
           the guard above pins it to the frame's own block. */
        if (unlikely(kos_dyn_teardown_to_frame(es, frame) < 0))
          goto fail;

        /* Self-tail at simple arity with a provably private namespace:
           rebind the live frame's own namespace in place -- release the
           raw locals, steal the arg span back into slots 0..nargs-1, and
           clear the block residue.  No namespace build, no frame reset;
           callable == FN means the frame's own ref keeps callable (and
           code) alive across the block clear. */
        if (callable == (PyObject *) KosDynFrame_FN(frame)
            && ! kwnames
            && ! (flags & KOS_CALL_ARGV_PREPENDED)
            && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
            && KosFunction_BIND_SELF_STATE(callable) == 2
            && ! KosDynFrame_RESUME_KIND(frame)
            && kos_tail_ns_reusable(KosDynFrame_NS(frame))) {
          PyObject *ns = KosDynFrame_NS(frame);

          /* Verify the arg span BEFORE releasing the raw locals: a
             steal underrun after the clear would leave the still
             installed frame with a wiped namespace.  Impossible by
             classification; if it ever fires, the raise (delegated to
             the formals checker so the report has one home) happens
             with the namespace untouched. */
          if (unlikely(nargs
                       && (! *caller_slot
                           || (size_t) nargs
                               > KosSegment_LEN(*caller_slot)))) {
            (void) KosFormals_SimpleStealApplyBlock(ns, *caller_slot,
                                                    nargs);
            goto fail;
          }

          KosNamespace_ResetToTemplate(ns);
          (void) KosFormals_SimpleStealApplyBlock(ns, *caller_slot, nargs);
          kos_stack_clear_fast(&KosDynFrame_STACK(frame));
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_TAIL_INPLACE);
          es->pc = 0;
          es->acting_pc = 0;
          /* the frame's FN reference keeps callable alive */
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return kos_call_installed(es);
        }

        if (likely(! kwnames
                   && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable))) {
          frame_ns = KosFunction_SimpleCallNamespaceFromVector(
              callable, argv, nargs);
        } else {
          frame_ns = KosFunction_PrepareCallNamespaceFromVector(
              callable, argv, nargs, kwnames);
        }
        if (unlikely(! frame_ns))
          goto fail;

        /* The callable is window-resident: the reset's block-clear
           releases it.  Hold it (and thereby code) across the reset,
           then transfer the reference into the FN tag -- the reset
           cleared the previous one -- so a following self-tail can
           rebind in place and release can attempt the park.  An owned
           caller reference serves as the hold and transfers directly. */
        if (! (flags & KOS_CALL_CALLABLE_OWNED))
          Py_INCREF(callable);
        kos_dynframe_reset_for_tail(frame);
        (void) KosEvalState_SetFrameCodeAndNamespace(es, frame, code,
                                                     frame_ns);
        KosDynFrame_FN(frame) = callable;
        es->pc = 0;
        es->acting_pc = 0;
        return kos_call_installed(es);
      }
    }
    goto legacy;
  }

  /* Class: exact KosFunction -- the dominant call target. */
  if (likely(Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType))) {
    KosCode *code = (KosCode *) KosFunction_CODE(callable);
    PyObject *frame_ns;

    /* A generator function call NEVER begins evaluating its code: it
       binds the arguments and completes immediately with the new
       AsyncGenerator as the call's value.  This gate stands ahead of
       every install arm below, so none of them can see yielding code
       no matter how the call classifies.  The agen lands on the
       caller's block, so the window drops first; NewFromCodeAndNs
       steals frame_ns. */
    if (unlikely(code && KosCode_YIELDS(code))) {
      if (! kwnames
          && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
          && KosFunction_BIND_SELF_STATE(callable) == 2) {
        frame_ns = KosFunction_SimpleCallNamespaceFromVector(
            callable, argv, nargs);
      } else {
        frame_ns = KosFunction_PrepareCallNamespaceFromVector(
            callable, argv, nargs, kwnames);
      }
      if (unlikely(! frame_ns))
        goto fail;
      {
        PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, frame_ns);
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        if (unlikely(! agen))
          return EVAL_RAISE();
        return KosEvalState_StackPUSH(es, agen) < 0
            ? EVAL_RAISE() : EVAL_NORMAL();
      }
    }

    if (likely(! kwnames
               && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable))) {
      /* Simple call: exact arity against plain positional formals on
         non-yielding code (classified at construction, and the
         generator gate above stands regardless).  Binds by slot
         index; the general formals machine is bypassed.  Any
         mismatch falls through to the general path, which owns the
         arity error messages.

         When argv is the block tail (window, no self-arg prepend) and
         the function has no self-bind, the args steal-move off the
         block into the namespace -- the stolen span leaves the window,
         so only the residue (a WINDOW_CALLABLE slot) drops after the
         install. */
      if (likely((flags & KOS_CALL_ARGV_WINDOW)
                 && ! (flags & KOS_CALL_ARGV_PREPENDED)
                 && KosFunction_BIND_SELF_STATE(callable) == 2)) {
        KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);

        if (unlikely(kos_eval_check_frame_room(
                es, KosEvalState_FRAME(es), 1) < 0)) {
          goto fail;
        }

        if (likely(cached)) {
          /* Cache hit: steal-bind into the parked namespace first -- on
             failure nothing is stolen and the frame stays parked. */
          if (unlikely(KosFormals_SimpleStealApplyBlock(
                  KosDynFrame_NS(cached), *caller_slot, nargs) < 0)) {
            goto fail;
          }
          KosFunction_CACHED_FRAME(callable) = NULL;
          window -= (size_t) nargs;
          kos_dyn_push_ready_frame(es, cached);
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(cached) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          return kos_call_installed(es);
        }

        frame_ns = KosFunction_SimpleCallNamespaceStealBlock(
            callable, *caller_slot, nargs);
        if (unlikely(! frame_ns)) {
          /* nothing stolen; the whole window is still on the block */
          goto fail;
        }
        window -= (size_t) nargs;

        /* Cache miss: install as usual, then tag the frame for the
           park attempt at release. */
        {
          KosDynFrame *frame = kos_dyn_push_frame_unchecked(
              es, code, frame_ns);
          if (unlikely(! frame))
            goto fail;
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(frame) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_STEAL);
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          return kos_call_installed(es);
        }
      }

      /* Plain vector (no stack window), no self-bind: the parked
         frame binds directly from argv -- references taken, slots
         filled, ready-push. A miss installs and tags the frame so its
         release parks it. */
      if (likely(! (flags & KOS_CALL_ARGV_WINDOW)
                 && KosFunction_BIND_SELF_STATE(callable) == 2)) {
        KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);

        if (unlikely(kos_eval_check_frame_room(
                es, KosEvalState_FRAME(es), 1) < 0)) {
          goto fail;
        }

        if (likely(cached)) {
          for (Py_ssize_t i = 0; i < nargs; i++)
            Py_INCREF(argv[i]);
          (void) KosFormals_SimpleStealApply(
              KosDynFrame_NS(cached), argv, nargs);
          KosFunction_CACHED_FRAME(callable) = NULL;
          kos_dyn_push_ready_frame(es, cached);
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(cached) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
          return kos_call_installed(es);
        }

        frame_ns = KosFunction_SimpleCallNamespaceFromVector(
            callable, argv, nargs);
        if (unlikely(! frame_ns))
          goto fail;
        KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_VECTOR);
        {
          KosDynFrame *frame = kos_dyn_push_frame_unchecked(
              es, code, frame_ns);
          if (unlikely(! frame))
            goto fail;
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(frame) = callable;
          return kos_call_installed(es);
        }
      }

      frame_ns = KosFunction_SimpleCallNamespaceFromVector(
          callable, argv, nargs);
      if (unlikely(! frame_ns))
        goto fail;
      KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_VECTOR);
      goto fn_frame_install;
    }

    /* General path: the generator gate above already peeled yielding
       code off, so this always installs a frame. */
    frame_ns = KosFunction_PrepareCallNamespaceFromVector(
        callable, argv, nargs, kwnames);

    if (unlikely(! frame_ns))
      goto fail;

fn_frame_install:

    /* Frame install: nothing lands on the caller's block, so the drop
       runs last and the window keeps callable and args alive for the
       whole install.  kos_dyn_push_frame installs code and frame_ns in
       one pass (consuming frame_ns even on failure), so the live
       registers are loaded once, with their final values. */
    {
      KosDynFrame *frame = kos_dyn_push_frame(es, code, frame_ns);
      if (unlikely(! frame))
        goto fail;
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return kos_call_installed(es);
    }
  }

  /* Class: plain Python callable -- vectorcall reading argv in place;
     the result lands on the caller's block, so the window drops first. */
  if (! KosCallable_Check(callable) && ! KosInstanceCall_Check(callable)) {
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, argv, (size_t) nargs, kwnames);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return kos_op_result_PUSH_OR_AWAIT(es, result);
  }

  /* Legacy classes: preserve steal-first ordering.  Spill the window
     into an owned vector (refs transfer out of the block), delegate,
     release.  Tail flows land here too (py tails need
     tail_prep_py_call; other-callable and deferred tails keep
     KosEvalState_Call's semantics). */
legacy:
  if (window) {
    PyObject *small[KOS_CALL_SPILL_FAST];
    PyObject **vals = small;
    KosOpResult r;

    if (window > KOS_CALL_SPILL_FAST) {
      vals = PyMem_Malloc(window * sizeof(PyObject *));
      if (unlikely(! vals)) {
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
    }
    if (unlikely(KosEvalState_StackPopTailPtr(es, window, vals) < 0)) {
      if (vals != small)
        PyMem_Free(vals);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return EVAL_RAISE();
    }

    /* vals is the ownership ledger for the spilled window entries and
       the vector the call reads: a tail call pops the caller's frame
       before the callee runs, and the block storage argv points into
       goes with it. A prepend buffer is the handler's own local, so it
       stays valid, and the objects it names are held by vals. */
    {
      PyObject *target;
      PyObject *const *call_argv;
      if (flags & KOS_CALL_WINDOW_CALLABLE) {
        target = vals[0];
        call_argv = vals + 1;
      } else {
        target = callable;
        /* whatever sits below the arguments in the window (a receiver
           slot) is skipped by size, so the body need not read the
           flag that put it there */
        call_argv = (flags & KOS_CALL_ARGV_PREPENDED)
            ? argv
            : vals + (window - (size_t) (nargs + nkw));
      }
      r = KosEvalState_Call(es, target,
                            (nargs + nkw) ? call_argv : NULL,
                            nargs, kwnames,
                            (flags & KOS_CALL_TAIL) ? 1 : 0);
    }

    for (size_t i = 0; i < window; i++)
      Py_DECREF(vals[i]);
    if (vals != small)
      PyMem_Free(vals);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return r;
  }

  {
    KosOpResult r = KosEvalState_Call(es, callable, argv, nargs, kwnames,
                                      (flags & KOS_CALL_TAIL) ? 1 : 0);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return r;
  }

  /* Every error exit of the class dispatch: drop whatever of the
     window is still on the block (a steal already took its span off
     the count), release an owned callable, and raise. */
fail:
  if (window)
    kos_stack_drop_noerr_fast(caller_slot, window);
  if (flags & KOS_CALL_CALLABLE_OWNED)
    Py_DECREF(callable);
  return EVAL_RAISE();
}


/* One out-of-line body per flags word the call sites use.  A handler
   passes its flags as a literal, so the dispatcher in
   kos_call_invoke_body folds to a direct call of the matching body,
   and the body itself is the class dispatch specialised for that word.
   KOS_CALL_WINDOW_UNDER shapes only the window arithmetic in
   kos_call_invoke and is never read by the body, so a word carrying
   it shares the body of the word without it.  Any other word takes
   the generic body, which keeps flags live. */
#define KOS_CALL_BODY_VARIANT(name, FLAGS)                              \
  static KOS_NOINLINE KosOpResult name(                                 \
      KosEvalState *es,                                                 \
      KosSegment **caller_slot,                                         \
      size_t window,                                                    \
      PyObject *callable,                                               \
      PyObject *const *argv,                                            \
      Py_ssize_t nargs,                                                 \
      PyObject *kwnames) {                                              \
    return kos_call_invoke_impl(es, caller_slot, window, callable,      \
                                argv, nargs, kwnames, (FLAGS));         \
  }

KOS_CALL_BODY_VARIANT(kos_call_body_plain, KOS_CALL_PLAIN)
KOS_CALL_BODY_VARIANT(kos_call_body_window_callable,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE)
KOS_CALL_BODY_VARIANT(kos_call_body_window_callable_tail,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE
                      | KOS_CALL_TAIL)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_prepended,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED
                      | KOS_CALL_ARGV_PREPENDED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_tail,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                      | KOS_CALL_CALLABLE_OWNED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_tail_prepended,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                      | KOS_CALL_CALLABLE_OWNED | KOS_CALL_ARGV_PREPENDED)

#undef KOS_CALL_BODY_VARIANT


static KOS_NOINLINE KosOpResult kos_call_body_any(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  return kos_call_invoke_impl(es, caller_slot, window, callable,
                              argv, nargs, kwnames, flags);
}


static inline KosOpResult kos_call_invoke_body(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  switch (flags) {
    case KOS_CALL_PLAIN:
      return kos_call_body_plain(es, caller_slot, window, callable,
                                 argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE:
      return kos_call_body_window_callable(es, caller_slot, window,
                                           callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE | KOS_CALL_TAIL:
      return kos_call_body_window_callable_tail(es, caller_slot, window,
                                                callable, argv, nargs,
                                                kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(es, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(es, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_prepended(es, caller_slot, window,
                                                  callable, argv, nargs,
                                                  kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(es, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER | KOS_CALL_TAIL
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(es, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_tail_prepended(es, caller_slot,
                                                       window, callable,
                                                       argv, nargs,
                                                       kwnames);
    default:
      return kos_call_body_any(es, caller_slot, window, callable,
                               argv, nargs, kwnames, flags);
  }
}


/**
   The frame-cache hit of a window call, taken in the handler before the
   call core: an exact KosFunction at simple arity with a frame parked
   on it steal-binds the block tail into that frame's namespace (slot 0
   from self_arg when one is given), pushes the frame ready, and tags
   it with the owned callable reference. Any other shape returns 0 with
   nothing consumed and the handler goes on to kos_call_invoke. The
   window is nargs wide and the callable is owned by the caller, so a
   taken arm leaves no residue to drop.

   :param self_arg: borrowed receiver for an unbound method call, or
     NULL; the arity classified is nargs + 2 with it, nargs + 1 without
   :param out: the installed result when 1 is returned
   :return: 1 when the call was installed here, 0 when the arm does not
     apply, -1 with an error set and nothing consumed when it applied
     and failed (no frame room); the handler releases the window and
     the callable
*/
KOS_OP_INLINE int kos_call_cached_frame_install(
    KosEvalState *es,
    PyObject *callable,
    PyObject *self_arg,
    Py_ssize_t nargs,
    KosOpResult *out) {

  KosDynFrame *cached;
  Py_ssize_t arity1 = nargs + (self_arg ? 2 : 1);

  if (Py_TYPE(callable) != KOS_EVALUATED_PYTYPE(KosFunctionType)
      || arity1 != KosFunction_SIMPLE_ARITY1(callable)
      || KosFunction_BIND_SELF_STATE(callable) != 2
      || KosCode_YIELDS((KosCode *) KosFunction_CODE(callable)))
    return 0;

  cached = KosFunction_CACHED_FRAME(callable);
  if (! cached)
    return 0;

  if (unlikely(kos_eval_check_frame_room(es, KosEvalState_FRAME(es), 1) < 0))
    return -1;

  /* Steal-bind first: on failure nothing is stolen and the frame stays
     parked. */
  if (self_arg
      ? unlikely(KosFormals_SimpleStealApplySelfBlock(
            KosDynFrame_NS(cached), self_arg, *KosEvalState_BLOCK(es),
            nargs) < 0)
      : unlikely(KosFormals_SimpleStealApplyBlock(
            KosDynFrame_NS(cached), *KosEvalState_BLOCK(es), nargs) < 0))
    return -1;

  KosFunction_CACHED_FRAME(callable) = NULL;
  kos_dyn_push_ready_frame(es, cached);
  KosDynFrame_FN(cached) = callable;  /* the owned reference transfers */
  KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
  *out = kos_call_installed(es);
  return 1;
}


/**
   Install a simple-arity KosFunction call whose receiver arrives out of
   band (unbound method resolution): fast slot 0 binds from self_arg and
   the window's arg span steal-binds into slots 1..nargs, through the
   parked frame when one is available.  The caller has already
   classified the callable -- exact KosFunction, simple arity nargs + 2,
   no self-bind, no kwnames -- and the window holds the args at the
   block tail.  Mirrors the two steal-bind arms of the simple-call
   branch in kos_call_invoke_body.
*/
static KOS_NOINLINE KosOpResult kos_call_invoke_self_steal(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *self_arg,
        Py_ssize_t nargs,
        unsigned flags) {

  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);
  PyObject *frame_ns;

  if (unlikely(kos_eval_check_frame_room(es, KosEvalState_FRAME(es), 1) < 0))
    goto fail;

  if (likely(cached)) {
    /* Cache hit: steal-bind into the parked namespace first -- on
       failure nothing is stolen and the frame stays parked. */
    if (unlikely(KosFormals_SimpleStealApplySelfBlock(
            KosDynFrame_NS(cached), self_arg, *caller_slot, nargs) < 0)) {
      goto fail;
    }
    KosFunction_CACHED_FRAME(callable) = NULL;
    window -= (size_t) nargs;
    kos_dyn_push_ready_frame(es, cached);
    if (! (flags & KOS_CALL_CALLABLE_OWNED))
      Py_INCREF(callable);
    KosDynFrame_FN(cached) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    return kos_call_installed(es);
  }

  frame_ns = KosFunction_SimpleCallNamespaceStealSelfBlock(
      callable, self_arg, *caller_slot, nargs);
  if (unlikely(! frame_ns)) {
    /* nothing stolen; the whole window is still on the block */
    goto fail;
  }
  window -= (size_t) nargs;

  /* Cache miss: install as usual, then tag the frame for the park
     attempt at release. */
  {
    KosDynFrame *frame = kos_dyn_push_frame_unchecked(es, code, frame_ns);
    if (unlikely(! frame))
      goto fail;
    if (! (flags & KOS_CALL_CALLABLE_OWNED))
      Py_INCREF(callable);
    KosDynFrame_FN(frame) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_STEAL);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    return kos_call_installed(es);
  }

fail:
  if (window)
    kos_stack_drop_noerr_fast(caller_slot, window);
  if (flags & KOS_CALL_CALLABLE_OWNED)
    Py_DECREF(callable);
  return EVAL_RAISE();
}


/**
   Shared call core entry.  self_arg (borrowed, may be NULL) is injected
   as the first positional via a local borrowed-pointer prepend buffer;
   the window accounting is untouched by the injection (self never lives
   on the caller's stack).  An unbound-method call that fits the simple
   steal-bind classification skips the buffer entirely and routes
   through kos_call_invoke_self_steal, keeping the parked-frame path
   reachable.  See the block comment above for the window sequencing
   contract.
*/
static inline KosOpResult kos_call_invoke(
        KosEvalState *es,
        PyObject *callable,
        PyObject *self_arg,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  KosSegment **caller_slot = KosEvalState_BLOCK(es);
  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  size_t window = 0;

  if (flags & KOS_CALL_ARGV_WINDOW)
    window = (size_t) (nargs + nkw)
        + ((flags & KOS_CALL_WINDOW_CALLABLE) ? 1u : 0u)
        + ((flags & KOS_CALL_WINDOW_UNDER) ? 1u : 0u);

  if (! self_arg)
    return kos_call_invoke_body(es, caller_slot, window, callable,
                                argv, nargs, kwnames, flags);

  if (likely((flags & KOS_CALL_ARGV_WINDOW))
      && ! (flags & KOS_CALL_TAIL)
      && ! kwnames
      && Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && nargs + 2 == KosFunction_SIMPLE_ARITY1(callable)
      /* a generator function never takes a frame-install arm; its
         call constructs the AsyncGenerator through the generator gate
         in kos_call_invoke_body instead */
      && ! KosCode_YIELDS((KosCode *) KosFunction_CODE(callable))
      && KosFunction_BIND_SELF_STATE(callable) == 2)
    return kos_call_invoke_self_steal(es, caller_slot, window, callable,
                                      self_arg, nargs, flags);

  {
    PyObject *pre_small[KOS_CALL_SPILL_FAST];
    PyObject **pre = pre_small;
    Py_ssize_t total = nargs + nkw + 1;
    KosOpResult r;

    if (total > KOS_CALL_SPILL_FAST) {
      pre = PyMem_Malloc((size_t) total * sizeof(PyObject *));
      if (unlikely(! pre)) {
        PyErr_NoMemory();
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
    }
    pre[0] = self_arg;
    if (total > 1)
      memcpy(pre + 1, argv, (size_t) (total - 1) * sizeof(PyObject *));

    r = kos_call_invoke_body(es, caller_slot, window, callable,
                             pre, nargs + 1, kwnames,
                             flags | KOS_CALL_ARGV_PREPENDED);
    if (pre != pre_small)
      PyMem_Free(pre);
    return r;
  }
}


KosOpResult KosEvalState_CallArgs(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosCallSlots *slots;

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        /* Prepend self to the positional tuple (the tuple form allocates
           regardless). */
        Py_ssize_t na = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
        PyObject *inj = PyTuple_New(na + 1);
        if (! inj)
          return EVAL_RAISE();
        Py_INCREF(callable);
        PyTuple_SET_ITEM(inj, 0, callable);
        for (Py_ssize_t i = 0; i < na; i++) {
          PyObject *it = PyTuple_GET_ITEM(args_tuple, i);
          Py_INCREF(it);
          PyTuple_SET_ITEM(inj, i + 1, it);
        }
        KosOpResult r = KosEvalState_CallArgs(es, call_fn, inj, kwds_dict,
                                              tail_call);
        Py_DECREF(inj);
        return r;
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();

    PyObject *result = NULL;

    if (args_tuple) {
      result = PyObject_Call(callable, args_tuple, kwds_dict);

    } else if (kwds_dict) {
      PyObject *empty_args = PyTuple_New(0);
      if (! empty_args)
        return EVAL_RAISE();
      result = PyObject_Call(callable, empty_args, kwds_dict);
      Py_DECREF(empty_args);

    } else {
      result = PyObject_CallNoArgs(callable);
    }

    if (! result)
      return EVAL_RAISE();

    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallArgsSlots(es, slots, callable, args_tuple,
                                kwds_dict, tail_call));
}


KosOpResult KosEvalState_CallNoArgs(
    KosEvalState *es,
    PyObject *callable,
    int tail_call) {

  KosCallSlots *slots;

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        return KosEvalState_Call(es, call_fn, &callable, 1, NULL, tail_call);
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();

    PyObject *result = PyObject_CallNoArgs(callable);
    if (! result)
      return EVAL_RAISE();
    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallArgsSlots(es, slots, callable, NULL, NULL,
                                tail_call));
}


// The end.
