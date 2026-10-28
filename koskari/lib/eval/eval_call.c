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

  The call core: the CPS entries (KosContext_Call, CallArgs,
  CallMethod, CallInto), the one dispatch they share, and
  kos_call_invoke with its per-flag bodies for the CALL* opcode
  prologues. Included into eval.c as a segment (see the eval/ includes
  there).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/**
   Install a code frame for a call. A tail call reuses the live frame:
   discardable extents left open above it are uninstalled (their POP_*
   rows are unreachable on the tail path), the frame is reset, and code
   and ns fill it. Otherwise a new frame is pushed above the live one.
   ns is stolen on every path.
*/
KosOpResult KosContext_InstallCallFrame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns,
    int tail_call) {

  if (tail_call) {
    KosDynFrame *frame = KosContext_FRAME(ctx);

    if (kos_dyn_teardown_to_frame(ctx, frame) < 0
        || kos_code_fits_namespace(code, ns) < 0) {
      Py_DECREF(ns);
      return EVAL_RAISE();
    }
    kos_dynframe_reset_for_tail(frame);
    (void) KosContext_SetFrameCodeAndNamespace(ctx, frame, code, ns);
    KosContext_SetEntryPC(ctx, code);
    return kos_call_installed(ctx);
  }

  if (! kos_dyn_push_frame(ctx, code, ns))
    return EVAL_RAISE();
  return kos_call_installed(ctx);
}


/**
   Tail-call preamble for a plain-Python callable: pop the frame when it
   is dyn_top and its parent is a plain frame, the one parent that
   takes a return on its stack; otherwise defer delivery to a normal
   RETURN. Sets *tail_defer_return accordingly.

   :return: 0 on success, -1 on error
*/
static int tail_prep_py_call(
    KosContext *ctx,
    int tail_call,
    int *tail_defer_return) {

  *tail_defer_return = 0;
  if (! tail_call)
    return 0;

  {
    KosDynFrame *frame = KosContext_FRAME(ctx);
    KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
    if (! parent)
      return 0;

    if (KosContext_DYN_TOP(ctx) == (KosDynNode *) frame
        && kos_return_parent_is_plain(parent))
      return kos_dyn_pop_frame(ctx);
  }

  /* Extent nodes are still open above the frame, or the parent is a
     region, a generator or a frame waiting on a resume, whose delivery
     is the return event's to route: skip the tail pop and deliver the
     result through a normal RETURN. */
  *tail_defer_return = 1;
  return 0;
}


/**
   Deliver a value in hand onto the current frame's stack: push-or-await,
   then, for a plain-Python callee in tail position (py_tail), map the
   completion to CONTROL_RETURNED when tail_prep_py_call popped the
   caller's frame or left a root frame in place, or to a deferred RETURN
   when extents open above the frame kept it.  result consumed.
*/
static KosOpResult deliver_call_value(
    KosContext *ctx,
    PyObject *result,
    int py_tail,
    int tail_defer_return) {

  KosOpResult por = push_or_await(ctx, result, 0, 0);
  if (por.kind == KOS_RES_NORMAL && py_tail) {
    if (tail_defer_return) {
      PyObject *rv = KosContext_StackPopRef(ctx);
      if (! rv)
        return EVAL_RAISE();
      return EVAL_RETURN(rv);
    }
    return EVAL_CONTROL_RETURNED();
  }
  return por;
}


static KosOpResult kos_call_dispatch(
    KosContext *ctx,
    KosCallSlots *slots,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


static KosOpResult kos_call_result_deliver(
    KosContext *ctx,
    KosDynFrame *caller,
    KosOpResult r,
    const KosResumeKind *resume,
    void *state);


/**
   Call callable with self as its receiver: self is injected as the
   first positional through a small buffer (no bound method, no
   descriptor bind) and the call dispatched, its outcome handed to
   resume as the vector entry hands it. Out of line so the buffer is
   not on the stack of every call.
*/
KOS_NOINLINE KosOpResult KosContext_CallMethod(
    KosContext *ctx,
    PyObject *callable,
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state) {

  KosDynFrame *caller = KosContext_FRAME(ctx);

  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  Py_ssize_t ntotal = nargs + nkw;
  PyObject *small[8];
  PyObject **inj = small;
  KosCallSlots *slots = NULL;
  KosOpResult r;

  if (ntotal + 1 > (Py_ssize_t) (sizeof(small) / sizeof(small[0]))) {
    inj = PyMem_Malloc((size_t) (ntotal + 1) * sizeof(PyObject *));
    if (! inj) {
      PyErr_NoMemory();
      return EVAL_RAISE();
    }
  }
  inj[0] = self;
  if (ntotal)
    memcpy(inj + 1, args, (size_t) ntotal * sizeof(PyObject *));
  (void) KosCallable_CheckSlots(callable, &slots);
  r = kos_call_dispatch(ctx, slots, callable, inj, nargs + 1, kwnames,
                        tail_call);
  if (inj != small)
    PyMem_Free(inj);
  return kos_call_result_deliver(ctx, caller, r, resume, state);
}


/**
   The one call dispatch: run a KosCallable's install through its
   resolved suite (slots), or vectorcall anything else, and report the
   outcome in hand. slots is NULL for a callable without a suite. An
   install's NORMAL is promoted to REPROLOGUE when a goal is set or the
   installed frame cannot be threaded into (it carries a resume rather
   than bytecode), so the trampoline's prologue picks it up. An
   install's POLL is answered as REPROLOGUE as well. The prologue polls
   when the breaker is up before it delivers, and the native callers
   of the CPS entries take REPROLOGUE as the installed outcome. A plain
   callable's result, and an instance's KosCallable __call__ run with
   self injected, report as the install contract does: RETURN with the
   value in hand, nothing pushed. args and kwnames are borrowed.
*/
static KosOpResult kos_call_dispatch(
    KosContext *ctx,
    KosCallSlots *slots,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  PyObject *result;

  if (slots) {
    KosOpResult r = slots->kc_vinstall(ctx, callable, args, nargs, kwnames,
                                       tail_call);
    if (r.kind == KOS_RES_NORMAL
        && (KosContext_GOAL(ctx)
            || ! kos_frame_threads(KosContext_FRAME(ctx))))
      return EVAL_REPROLOGUE();
    if (r.kind == KOS_RES_POLL)
      return EVAL_REPROLOGUE();
    return r;
  }

  if (KosInstanceCall_Check(callable)) {
    int binds;
    PyObject *target = kos_instance_call_target(callable, &binds);

    if (target && binds)
      return KosContext_CallMethod(
          ctx, target, callable, args, nargs, kwnames, tail_call, NULL,
          NULL);
    if (target) {
      KosCallSlots *target_slots = NULL;

      (void) KosCallable_CheckSlots(target, &target_slots);
      return kos_call_dispatch(ctx, target_slots, target, args, nargs,
                               kwnames, tail_call);
    }
  }

  /* PEP 590: keyword values follow args[nargs .. nargs + nkw - 1]. */
  result = PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
  return result ? EVAL_RETURN(result) : EVAL_RAISE();
}


/**
   The CALL* opcodes' entry, whose result lands on the current frame's
   stack: a value the dispatch reports in hand is pushed or awaited
   there and an installed callee is left current. A plain Python
   callable in tail position gives up the caller's frame before the
   call when the frame's parent is a plain frame. Reached from the
   class dispatch's legacy arm.
*/
static KosOpResult kos_call_push(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosCallSlots *slots = NULL;
  int py_tail = 0;
  int tail_defer_return = 0;
  KosOpResult r;

  /* A plain Python callable in tail position gives up the caller's
     frame before the call when the parent is a plain frame; a
     KosCallable that completes in hand keeps it, and the RETURN
     following the TAIL_CALL retires it. */
  if (! KosCallable_CheckSlots(callable, &slots)
      && ! KosInstanceCall_Check(callable)) {
    if (tail_prep_py_call(ctx, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();
    py_tail = tail_call;
  }

  r = kos_call_dispatch(ctx, slots, callable, args, nargs, kwnames,
                        tail_call);
  if (r.kind != KOS_RES_RETURN)
    return r;
  return deliver_call_value(ctx, r.val, py_tail, tail_defer_return);
}


/**
   A CPS entry's value-in-hand outcome: settled, report RETURN with
   val and set no resume -- the calling resume tail-calls resume with
   it; not yet settled, settle one step through KosContext_SettleStage
   and set resume on caller for the trip that delivers it. val is
   consumed.
*/
static KosOpResult kos_entry_value(
    KosContext *ctx,
    KosDynFrame *caller,
    PyObject *val,
    const KosResumeKind *resume,
    void *state) {

  switch (KosEval_ResultIsValue(val)) {
    case 1:
      return EVAL_RETURN(val);

    case 0: {
      KosOpResult pr = KosContext_SettleStage(ctx, val);
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


/**
   The resume arm shared by the CPS entries: hand a dispatch outcome to
   resume. A value in hand goes to kos_entry_value; an installed
   callee, a resume set on a code-less frame the install pushed, or
   an await from such a frame leaves caller's own frame unchanged and
   still reachable, so resume is set on it for the eventual
   delivery; RAISE and FATAL relay untouched. With resume NULL the
   outcome relays as is.
*/
static KosOpResult kos_call_result_deliver(
    KosContext *ctx,
    KosDynFrame *caller,
    KosOpResult r,
    const KosResumeKind *resume,
    void *state) {

  if (! resume)
    return r;

  switch (r.kind) {
    case KOS_RES_RETURN:
      return kos_entry_value(ctx, caller, r.val, resume, state);

    case KOS_RES_NORMAL:
    case KOS_RES_POLL:
    case KOS_RES_REPROLOGUE:
    case KOS_RES_AWAIT:
      KosDynFrame_RESUME_KIND(caller) = resume;
      KosDynFrame_RESUME_STATE(caller) = state;
      return r;

    default:
      return r;
  }
}


KosOpResult KosContext_Call(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state) {

  KosDynFrame *caller = KosContext_FRAME(ctx);
  KosOpResult r;

#if KOS_EVAL_SAFETYNET
  if (unlikely(tail_call && resume)) {
    PyErr_SetString(PyExc_SystemError,
                    "tail call with a resume: no caller frame to resume");
    return EVAL_RAISE();
  }
#endif

  {
    KosCallSlots *slots = NULL;

    (void) KosCallable_CheckSlots(callable, &slots);
    r = kos_call_dispatch(ctx, slots, callable, args, nargs, kwnames,
                          tail_call);
  }

  return kos_call_result_deliver(ctx, caller, r, resume, state);
}


/**
   Install a frame for a native program, the way InstallCallFrame
   installs one for bytecode: a tail call tears down to the live frame,
   resets it, and refills it as a native frame; otherwise a native
   frame is pushed, taken from frame_cache when the slot holds one.
   Either way the frame carries the empty namespace, the resume set,
   and frame_flags. Returns the frame, or NULL with PyErr set.
*/
KosDynFrame *KosContext_InstallNativeFrame(
    KosContext *ctx,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags,
    KosDynFrame **frame_cache,
    int tail_call) {

  KosDynFrame *frame;

  if (tail_call) {
    PyObject *empty = KosNamespace_Empty();

    if (! empty)
      return NULL;
    frame = KosContext_FRAME(ctx);
    if (kos_dyn_teardown_to_frame(ctx, frame) < 0)
      return NULL;
    kos_dynframe_reset_for_tail(frame);
    Py_INCREF(empty);
    (void) KosContext_SetFrameCodeAndNamespace(ctx, frame, NULL, empty);
    KosContext_SetEntryPC(ctx, NULL);
  } else {
    frame = KosResumeFrame_Install(ctx, NULL, NULL, frame_cache);
    if (! frame)
      return NULL;
  }

  KosDynFrame_FLAGS(frame) |= frame_flags;
  KosDynFrame_RESUME_KIND(frame) = resume;
  KosDynFrame_RESUME_STATE(frame) = state;
  return frame;
}


KosOpResult KosContext_CallInto(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags,
    KosDynFrame **frame_cache,
    int tail_call) {

  KosDynFrame *frame = KosContext_InstallNativeFrame(
      ctx, resume, state, frame_flags, frame_cache, tail_call);
  KosOpResult r;

  if (! frame)
    return EVAL_RAISE();

  /* The inner call is never a tail call: its return must reach this
     frame's resume. */
  r = KosContext_Call(
      ctx, callable, args, nargs, kwnames, 0, resume, state);
  if (r.kind != KOS_RES_RETURN)
    return r;

  KosContext_StageDelivery(ctx, r.val);
  return EVAL_REPROLOGUE();
}


KosOpResult KosContext_SettleInto(
    KosContext *ctx,
    PyObject *val,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags) {

  KosDynFrame *frame = KosContext_InstallNativeFrame(
      ctx, resume, state, frame_flags, NULL, 0);
  KosOpResult r;

  if (! frame) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }

  r = kos_entry_value(ctx, frame, val, resume, state);
  if (r.kind != KOS_RES_RETURN)
    return r;

  KosContext_StageDelivery(ctx, r.val);
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

   Legacy classes (KosCallables other than KosFunction, and every
   tail call the bind-before-reset arm declines) preserve the
   steal-first ordering by spilling the window into an owned vector
   before delegating to kos_call_push.  An instance whose __call__ is a
   KosCallable re-enters the core on the target without a spill
   (kos_call_invoke_instance).  Tail calls ride the same entry under
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
   kos_call_push's tail semantics. */
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
   KosFunction_CacheFrame: a captured, revealed, or dynamically
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


/**
   The legacy classes' steal-first spill: move the window into an owned
   vector (refs transfer out of the block), delegate to
   kos_call_push, release. Out of line so the spill buffer sits in
   this frame alone, on the paths that take it.
*/
static KOS_NOINLINE KosOpResult kos_call_legacy_spill(
    KosContext *ctx,
    size_t window,
    PyObject *callable,
    PyObject *const *argv,
    Py_ssize_t nargs,
    PyObject *kwnames,
    unsigned flags) {

  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
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
  if (unlikely(KosContext_StackPopTailPtr(ctx, window, vals) < 0)) {
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
         slot) is skipped by size, so the body need not read the flag
         that put it there */
      call_argv = (flags & KOS_CALL_ARGV_PREPENDED)
          ? argv
          : vals + (window - (size_t) (nargs + nkw));
    }
    r = kos_call_push(
        ctx, target, (nargs + nkw) ? call_argv : NULL, nargs, kwnames,
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


KOS_OP_INLINE KosOpResult kos_call_invoke(
        KosContext *ctx,
        PyObject *callable,
        PyObject *self_arg,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags);


/**
   An instance call from the class dispatch: re-enter the call core
   with the __call__ target as the callable, and the instance as the
   receiver when the target binds one. The instance stays where it is:
   a window slot under the arguments (WINDOW_CALLABLE becomes
   WINDOW_UNDER, same width) that drops with the window, or a
   reference the handler owns, released once the invoke returns. The
   target is borrowed from the type, so the owned-callable flag is
   cleared for the re-entry. window is unused: kos_call_invoke
   recomputes it from the rewritten flags.
*/
static KOS_NOINLINE KosOpResult kos_call_invoke_instance(
        KosContext *ctx,
        size_t window,
        PyObject *instance,
        PyObject *target,
        int binds,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  unsigned reentry = flags & ~KOS_CALL_CALLABLE_OWNED;
  KosOpResult r;

  (void) window;
  if (reentry & KOS_CALL_WINDOW_CALLABLE)
    reentry = (reentry & ~KOS_CALL_WINDOW_CALLABLE) | KOS_CALL_WINDOW_UNDER;

  r = kos_call_invoke(ctx, target, binds ? instance : NULL, argv, nargs,
                      kwnames, reentry);
  if (flags & KOS_CALL_CALLABLE_OWNED)
    Py_DECREF(instance);
  return r;
}


/* The class dispatch proper.  Always inlined into the per-flag bodies
   below, each of which passes a literal flags word, so every flag test
   in here folds away and each body carries only its own arms. */
KOS_OP_INLINE KosOpResult kos_call_invoke_impl(
        KosContext *ctx,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  /* Class: exact KosFunction, tail position.  Bind-before-reset: the
     namespace is prepared FROM the still-intact window, then the frame
     reset's block-clear performs the window drop.  Requires the window
     to live in the reused frame's own block and non-yielding code;
     anything else falls to the legacy spill below (which preserves
     kos_call_push's tail semantics exactly). */
  if (unlikely(flags & KOS_CALL_TAIL)) {
    KosDynFrame *frame = KosContext_FRAME(ctx);

    if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
        && frame
        && caller_slot == &KosDynFrame_STACK(frame)) {
      KosCode *code = (KosCode *) KosFunction_CODE(callable);

      if (! (code && KosCode_YIELDS(code))) {
        /* Discardable extents open above the frame (a tail call from
           inside let / loop / foreach) uninstall now: the reused frame
           never reaches their POP_* rows.  The window is unaffected --
           the guard above keeps it in the frame's own block. */
        if (unlikely(kos_dyn_teardown_to_frame(ctx, frame) < 0))
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
            && KosFunction_BIND_SELF_STATE(callable) != 0
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
          KosFunction_StoreSelf(callable, ns);
          kos_stack_clear_fast(&KosDynFrame_STACK(frame));
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_TAIL_INPLACE);
          KosContext_SetEntryPC(ctx, code);
          /* the frame's FN reference keeps callable alive */
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return kos_call_installed(ctx);
        }

        /* Any other tail call: the callee's ready frame, bound from the
           window while it still sits on the live frame's block, takes
           the live frame's place on the chain. The swap frees the live
           frame, whose block clear performs the window drop and whose
           release offers it to its own function's cache; the ready
           frame's tag holds the callable across that clear. */
        {
          KosDynFrame *ready = KosFunction_ReadyFrameVector(
              callable, argv, nargs, kwnames);

          if (unlikely(! ready))
            goto fail;
          kos_dyn_swap_frame_for_tail(ctx, ready);
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return kos_call_installed(ctx);
        }
      }
    }
    goto legacy;
  }

  /* Class: exact KosFunction -- the dominant call target. */
  if (likely(Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType))) {
    KosCode *code = (KosCode *) KosFunction_CODE(callable);

    /* A generator function call never begins evaluating its code: it
       binds the arguments and completes with the new AsyncGenerator as
       the call's value, which the function's install reports in hand.
       The steal-bind arm below never sees yielding code. */
    if (likely(! kwnames
               && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
               && ! (code && KosCode_YIELDS(code)))) {
      /* Simple call: exact arity against plain positional formals on
         non-yielding code (classified at construction, and the
         generator gate above stands regardless).  Binds by slot
         index; the general formals machine is bypassed.

         When argv is the block tail (window, no self-arg prepend) and
         the function's self-bind is resolved, the args steal-move off
         the block into the namespace -- the stolen span leaves the
         window, so only the residue (a WINDOW_CALLABLE slot) drops
         after the install.  Any other shape takes the function's own
         install. */
      if (likely((flags & KOS_CALL_ARGV_WINDOW)
                 && ! (flags & KOS_CALL_ARGV_PREPENDED)
                 && KosFunction_BIND_SELF_STATE(callable) != 0)) {
        KosDynFrame *ready;

        if (unlikely(kos_eval_check_frame_room(
                ctx, KosContext_FRAME(ctx), 1) < 0)) {
          goto fail;
        }

        /* The ready frame steal-binds the block tail; on failure
           nothing is stolen and the whole window is still on the
           block. An owned callable reference moves into the frame's
           tag. */
        ready = KosFunction_ReadyFrameBlock(
            callable, flags & KOS_CALL_CALLABLE_OWNED, *caller_slot, NULL, 0,
            nargs);
        if (unlikely(! ready))
          goto fail;
        window -= (size_t) nargs;
        kos_dyn_push_ready_frame(ctx, ready);
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        return kos_call_installed(ctx);
      }
    }

    /* Every other function call (a generator function, a plain vector,
       keywords, a non-simple arity, an unresolved self-bind, a
       prepended receiver): the function's own install, reading argv
       in place.  An installed frame lands nothing on the caller's
       block, so the window drops after it; a generator function
       completes in hand and its AsyncGenerator lands on the block, so
       the window drops first. */
    {
      KosOpResult r = KosFunction_Install(
          ctx, callable, argv, nargs, kwnames, 0);

      if (unlikely(r.kind == KOS_RES_RAISE))
        goto fail;
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      if (r.kind == KOS_RES_RETURN)
        return kos_op_result_PUSH_OR_AWAIT(ctx, r.val);
      return r;
    }
  }

  /* Class: an instance whose __call__ is a KosCallable -- the call
     core re-entered on the target, with the instance as the window's
     under-slot and as the receiver when the target binds one. */
  if (KosInstanceCall_Check(callable)) {
    int binds;
    PyObject *target = kos_instance_call_target(callable, &binds);
    if (target)
      return kos_call_invoke_instance(ctx, window, callable, target, binds,
                                      argv, nargs, kwnames, flags);
  }

  /* Class: plain Python callable -- vectorcall reading argv in place;
     the result lands on the caller's block, so the window drops first. */
  if (! KosCallable_Check(callable)) {
    PyObject *result = PyObject_Vectorcall(callable, argv, (size_t) nargs,
                                           kwnames);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return kos_op_result_PUSH_OR_AWAIT(ctx, result);
  }

  /* Legacy classes: preserve steal-first ordering.  Spill the window
     into an owned vector (refs transfer out of the block), delegate,
     release.  Tail flows land here too (py tails need
     tail_prep_py_call; other-callable and deferred tails keep
     kos_call_push's semantics). */
legacy:
  if (window)
    return kos_call_legacy_spill(
        ctx, window, callable, argv, nargs, kwnames, flags);

  {
    KosOpResult r = kos_call_push(
        ctx, callable, argv, nargs, kwnames,
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
      KosContext *ctx,                                                  \
      KosSegment **caller_slot,                                         \
      size_t window,                                                    \
      PyObject *callable,                                               \
      PyObject *const *argv,                                            \
      Py_ssize_t nargs,                                                 \
      PyObject *kwnames) {                                              \
    return kos_call_invoke_impl(ctx, caller_slot, window, callable,     \
                                argv, nargs, kwnames, (FLAGS));         \
  }

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
        KosContext *ctx,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  return kos_call_invoke_impl(ctx, caller_slot, window, callable, argv,
                              nargs, kwnames, flags);
}


KOS_OP_INLINE KosOpResult kos_call_invoke_body(
        KosContext *ctx,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  switch (flags) {
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE:
      return kos_call_body_window_callable(ctx, caller_slot, window,
                                           callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE | KOS_CALL_TAIL:
      return kos_call_body_window_callable_tail(ctx, caller_slot, window,
                                                callable, argv, nargs,
                                                kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(ctx, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(ctx, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_prepended(ctx, caller_slot, window,
                                                  callable, argv, nargs,
                                                  kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(ctx, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER | KOS_CALL_TAIL
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(ctx, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_tail_prepended(ctx, caller_slot,
                                                       window, callable,
                                                       argv, nargs,
                                                       kwnames);
    default:
      return kos_call_body_any(ctx, caller_slot, window, callable,
                               argv, nargs, kwnames, flags);
  }
}


/**
   The simple call of a window call, installed in the handler before
   the call core: an exact KosFunction at simple arity on non-yielding
   code takes its ready frame from the block tail
   (KosFunction_ReadyFrameBlock: slot 0 from self_arg when one is
   given, the cache head or a fresh frame) and pushes it. Any other
   shape returns 0 with nothing consumed and the handler goes on to
   kos_call_invoke. The window is nargs wide and the callable is owned
   by the caller, so a taken arm leaves no residue to drop, and the
   caller's reference moves into the frame's tag.

   :param self_arg: receiver for an unbound method call, or NULL; the
     arity classified is nargs + 2 with it, nargs + 1 without
   :param self_owned: 1 when the caller's reference to self_arg moves
     into slot 0 on a taken arm (the caller then does not release it),
     0 when self_arg is borrowed and the slot takes its own reference
   :param out: the installed result when 1 is returned
   :return: 1 when the call was installed here, 0 when the arm does not
     apply, -1 with an error set and nothing consumed when it applied
     and failed (no frame room, a refused bind); the handler releases
     the window and the callable
*/
KOS_OP_INLINE int kos_call_simple_install(
    KosContext *ctx,
    PyObject *callable,
    PyObject *self_arg,
    int self_owned,
    Py_ssize_t nargs,
    KosOpResult *out) {

  KosDynFrame *ready;
  Py_ssize_t arity1 = nargs + (self_arg ? 2 : 1);

  if (Py_TYPE(callable) != KOS_EVALUATED_PYTYPE(KosFunctionType)
      || arity1 != KosFunction_SIMPLE_ARITY1(callable)
      || KosFunction_BIND_SELF_STATE(callable) == 0
      || ! KosFunction_CODE(callable)
      || KosCode_YIELDS((KosCode *) KosFunction_CODE(callable)))
    return 0;

  if (unlikely(kos_eval_check_frame_room(ctx, KosContext_FRAME(ctx), 1) < 0))
    return -1;

  ready = KosFunction_ReadyFrameBlock(callable, 1, *KosContext_BLOCK(ctx),
                                      self_arg, self_owned, nargs);
  if (unlikely(! ready))
    return -1;
  kos_dyn_push_ready_frame(ctx, ready);
  *out = kos_call_installed(ctx);
  return 1;
}


/**
   Install a simple-arity KosFunction call whose receiver arrives out of
   band (unbound method resolution): fast slot 0 binds from self_arg and
   the window's arg span steal-binds into slots 1..nargs, through the
   cached frame when one is available.  The caller has already
   classified the callable -- exact KosFunction, simple arity nargs + 2,
   no self-bind, no kwnames -- and the window holds the args at the
   block tail.  Mirrors the two steal-bind arms of the simple-call
   branch in kos_call_invoke_impl.
*/
static KOS_NOINLINE KosOpResult kos_call_invoke_self_steal(
        KosContext *ctx,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *self_arg,
        Py_ssize_t nargs,
        unsigned flags) {

  KosDynFrame *ready;

  if (unlikely(kos_eval_check_frame_room(ctx, KosContext_FRAME(ctx), 1) < 0))
    goto fail;

  /* on failure nothing is stolen; the whole window is still on the
     block. An owned callable reference moves into the frame's tag. */
  ready = KosFunction_ReadyFrameBlock(
      callable, flags & KOS_CALL_CALLABLE_OWNED, *caller_slot, self_arg, 0,
      nargs);
  if (unlikely(! ready))
    goto fail;
  window -= (size_t) nargs;
  kos_dyn_push_ready_frame(ctx, ready);
  if (window)
    kos_stack_drop_noerr_fast(caller_slot, window);
  return kos_call_installed(ctx);

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
   through kos_call_invoke_self_steal, keeping the cached-frame path
   reachable.  See the block comment above for the window sequencing
   contract.
*/
KOS_OP_INLINE KosOpResult kos_call_invoke(
        KosContext *ctx,
        PyObject *callable,
        PyObject *self_arg,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  KosSegment **caller_slot = KosContext_BLOCK(ctx);
  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  size_t window = 0;

  if (flags & KOS_CALL_ARGV_WINDOW)
    window = (size_t) (nargs + nkw)
        + ((flags & KOS_CALL_WINDOW_CALLABLE) ? 1u : 0u)
        + ((flags & KOS_CALL_WINDOW_UNDER) ? 1u : 0u);

  if (! self_arg)
    return kos_call_invoke_body(ctx, caller_slot, window, callable,
                                argv, nargs, kwnames, flags);

  if (likely((flags & KOS_CALL_ARGV_WINDOW))
      && ! (flags & KOS_CALL_TAIL)
      && ! kwnames
      && Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && nargs + 2 == KosFunction_SIMPLE_ARITY1(callable)
      /* a generator function never takes a frame-install arm; its
         call constructs the AsyncGenerator through the generator gate
         in kos_call_invoke_impl instead */
      && KosFunction_CODE(callable)
      && ! KosCode_YIELDS((KosCode *) KosFunction_CODE(callable))
      && KosFunction_BIND_SELF_STATE(callable) != 0)
    return kos_call_invoke_self_steal(ctx, caller_slot, window, callable,
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

    r = kos_call_invoke_body(ctx, caller_slot, window, callable,
                             pre, nargs + 1, kwnames,
                             flags | KOS_CALL_ARGV_PREPENDED);
    if (pre != pre_small)
      PyMem_Free(pre);
    return r;
  }
}


/**
   The handler entry.  A CALL handler learns whether it is running as
   the TAIL_CALL twin from the opcode, so its tail word is a runtime
   value; splitting on it here hands kos_call_invoke a literal flags
   word on each arm, and the body dispatch folds to a direct call in
   both.
*/
KOS_OP_INLINE KosOpResult kos_call_invoke_from_handler(
        KosContext *ctx,
        unsigned tail,
        PyObject *callable,
        PyObject *self_arg,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  if (unlikely(tail))
    return kos_call_invoke(ctx, callable, self_arg, argv, nargs, kwnames,
                           flags | KOS_CALL_TAIL);
  return kos_call_invoke(ctx, callable, self_arg, argv, nargs, kwnames,
                         flags);
}


/**
   The one place a tuple and a dict meet a call: a plain Python
   callable takes them as they are through PyObject_Call and its result
   is reported in hand; a KosCallable, or an instance whose __call__ is
   one, is spread once to the vector shape and dispatched. Keywords
   take the heap, as PyObject_Call's own dict unpacking does. The
   outcome is the install contract's.
*/
static KosOpResult kos_call_dispatch_args(
    KosContext *ctx,
    KosCallSlots *slots,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosArgSpread spread;
  KosOpResult r;

  if (! slots && ! KosInstanceCall_Check(callable)) {
    PyObject *empty = NULL;
    PyObject *result;

    if (! args_tuple) {
      empty = PyTuple_New(0);
      if (! empty)
        return EVAL_RAISE();
      args_tuple = empty;
    }
    result = PyObject_Call(callable, args_tuple, kwds_dict);
    Py_XDECREF(empty);
    return result ? EVAL_RETURN(result) : EVAL_RAISE();
  }

  if (KosArgSpread_Init(&spread, args_tuple, kwds_dict, NULL, 0) < 0)
    return EVAL_RAISE();
  r = kos_call_dispatch(ctx, slots, callable, spread.argv, spread.nargs,
                        spread.kwnames, tail_call);
  KosArgSpread_Clear(&spread);
  return r;
}


/**
   kos_call_push over a tuple and a dict, for CALL_STAR and
   TAIL_CALL_STAR.
*/
static KosOpResult kos_call_push_args(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosCallSlots *slots = NULL;
  int py_tail = 0;
  int tail_defer_return = 0;
  KosOpResult r;

  if (! KosCallable_CheckSlots(callable, &slots)
      && ! KosInstanceCall_Check(callable)) {
    if (tail_prep_py_call(ctx, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();
    py_tail = tail_call;
  }

  r = kos_call_dispatch_args(ctx, slots, callable, args_tuple, kwds_dict,
                             tail_call);
  if (r.kind != KOS_RES_RETURN)
    return r;
  return deliver_call_value(ctx, r.val, py_tail, tail_defer_return);
}


KosOpResult KosContext_CallArgs(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call,
    const KosResumeKind *resume,
    void *state) {

  KosDynFrame *caller = KosContext_FRAME(ctx);
  KosCallSlots *slots = NULL;
  KosOpResult r;

#if KOS_EVAL_SAFETYNET
  if (unlikely(tail_call && resume)) {
    PyErr_SetString(PyExc_SystemError,
                    "tail call with a resume: no caller frame to resume");
    return EVAL_RAISE();
  }
#endif

  (void) KosCallable_CheckSlots(callable, &slots);
  r = kos_call_dispatch_args(ctx, slots, callable, args_tuple, kwds_dict,
                             tail_call);
  return kos_call_result_deliver(ctx, caller, r, resume, state);
}


// The end.
