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
  Included by dynchain.c. The KOS_DYN_FRAME node: function activation
  records, their push, pop, tail reset and caching.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynFrame implementation === */


int kos_code_fits_namespace(KosCode *code, PyObject *ns) {

  if (! KosCode_USES_FAST(code))
    return 0;

  if (! ns || ! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError,
                 "code %R uses fast slots but namespace %R does not"
                 " support them", KosCode_NAME(code), ns ? ns : Py_None);
    return -1;
  }

  if (KosNamespace_FAST_COUNT(ns) < KosCode_FAST_NEED(code)) {
    PyErr_Format(PyExc_RuntimeError,
                 "code %R needs %u fast slots; namespace %R has %zu",
                 KosCode_NAME(code), (unsigned) KosCode_FAST_NEED(code),
                 ns, (size_t) KosNamespace_FAST_COUNT(ns));
    return -1;
  }

  return 0;
}


/**
  Allocate a FRAME node: code and namespace attached, operand segment
  aimed at the embedded inline storage. Frame takes a strong reference
  to code.
   Callers: internal frame construction, kos_dyn_push_frame.
*/
KosDynFrame *kos_dynframe_new(
    KosDynNode *parent,
    KosCode *code,
    PyObject *ns) {

  KosDynFrame *frame = kos_dynnode_alloc();
  if (! frame) {
    Py_XDECREF(ns);
    return NULL;
  }

  /* A frame with code introduces its namespace; a native or
     placeholder frame carries the live namespace as an owned fill, as
     any non-introducer node does, and must not report it as its own. */
  kos_dynnode_init(frame, KOS_DYN_FRAME, parent,
                   code ? KOS_DYN_CAP_OWNS_NS : 0);
  if (code) {
    if (kos_code_fits_namespace(code, ns) < 0) {
      Py_XDECREF(ns);
      kos_dynnode_return_to_freelist(frame);
      return NULL;
    }
    Py_INCREF(code);
  }
  KosDynFrame_CODE(frame) = code;  /* owned */
  KosDynFrame_PC(frame) = kos_code_entry(code);
  KosDynFrame_ACTING_PC(frame) = KosDynFrame_PC(frame);
  kos_dynnode_use_inline_segment((KosDynNode *) frame);
  KosDynFrame_NS(frame) = ns;  /* ownership transferred (already stole ref) */
  KosDynFrame_FLAGS(frame) = 0;
  kos_dynframe_note_parent(frame);
  KosDynFrame_DEPTH(frame) = 1;  /* a root until an install says otherwise */
  KosDynFrame_CACHE_SLOT(frame) = NULL;
  KosDynFrame_RESUME_KIND(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;
  KosDynFrame_FN(frame) = NULL;
  KosDynFrame_BACK_FRAME(frame) = NULL;
  return frame;
}


/**
   Clone a frame with its operand segment (continuation capture, delim
   return-frame snapshot, capture baseline). Callers: kos_dyn_clone
   and the direct snapshot sites; the segment copy happens here so the
   direct callers stay faithful without the dispatcher.
*/
KosDynFrame *kos_dynframe_clone(KosDynFrame *frame) {
  if (! frame)
    return NULL;

  KosDynFrame *clone = kos_dynnode_clone_base(frame);
  if (! clone)
    return NULL;

  if (kos_dyn_clone_copy_segment((KosDynNode *) clone,
                                 (KosDynNode *) frame) < 0) {
    kos_dynnode_return_to_freelist(clone);
    return NULL;
  }

  KosDynFrame_CODE(clone) = KosDynFrame_CODE(frame);  /* owned */
  Py_XINCREF(KosDynFrame_CODE(clone));
  KosDynFrame_PC(clone) = KosDynFrame_PC(frame);
  KosDynFrame_ACTING_PC(clone) = KosDynFrame_ACTING_PC(frame);

  /* a captured continuation can be stored back into scope; reveal the
     frame's namespace chain to the cyclic collector */
  KosDynFrame_NS(clone) = KosNamespace_Reveal(KosDynFrame_NS(frame));

  KosDynFrame_FLAGS(clone) = KosDynFrame_FLAGS(frame)
      & ~KOS_FRAME_RETURN_TO_CACHE;
  /* A snapshot stands where its source stood until a splice renumbers
     it: a landed template, or the restitch of a stashed span. */
  KosDynFrame_DEPTH(clone) = KosDynFrame_DEPTH(frame);
  KosDynFrame_CACHE_SLOT(clone) = NULL;
  /* clones opt out of the per-function frame cache */
  KosDynFrame_FN(clone) = NULL;
  /* borrowed unwind cache is meaningless off the source's chain */
  KosDynFrame_BACK_FRAME(clone) = NULL;

  /* A resume whose kind can clone its state copies; one whose kind
     cannot (a provider step, whose state is the step's own) refuses
     the capture by name. The clone's resume starts empty so the
     refusal's free finds nothing to clear. */
  KosDynFrame_RESUME_KIND(clone) = NULL;
  KosDynFrame_RESUME_STATE(clone) = NULL;
  if (KosDynFrame_RESUME_KIND(frame) && KosDynFrame_RESUME_KIND(frame)->clone) {
    KosDynFrame_RESUME_KIND(clone) = KosDynFrame_RESUME_KIND(frame);
    KosDynFrame_RESUME_STATE(clone) =
        KosDynFrame_RESUME_KIND(frame)->clone(KosDynFrame_RESUME_STATE(frame));
    return clone;
  }
  if (KosDynFrame_RESUME_KIND(frame)) {
    PyErr_Format(PyExc_RuntimeError,
                 "cannot capture a continuation across native"
                 " frame '%s'",
                 KosDynFrame_RESUME_NAME(frame)
                     ? KosDynFrame_RESUME_NAME(frame) : "?");
    kos_dynframe_free(clone);
    return NULL;
  }

  return clone;
}


void *kos_resume_clone_shared(void *state) {
  return state;
}


/**
   The clone of a state that is one owned Python object: a new
   reference to the same object.
*/
void *kos_resume_clone_object(void *state) {
  Py_XINCREF((PyObject *) state);
  return state;
}


void kos_dynframe_clear_resume(KosDynFrame *frame) {
  const KosResumeKind *kind = KosDynFrame_RESUME_KIND(frame);

  if (kind && kind->clear)
    kind->clear(KosDynFrame_RESUME_STATE(frame));
  KosDynFrame_RESUME_KIND(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;
}


/**
   Release stack chain and namespace; return node to freelist. Callers:
   kos_dynnode_free, chain teardown.

   When the frame carries a cache-participation fn reference, offer the
   node to the function's frame cache first; cached or not, the fn
   reference is released here, so a cached node sits fn-less and the
   fn -> frame -> fn cycle can never form.
*/
void kos_dynframe_free(KosDynFrame *frame) {
  if (! frame)
    return;

  kos_dynframe_clear_resume(frame);

  /* A frame set by KosNativeFrame_CacheOnFree, completing or declining
     a failure: cache_slot is the holder's cache slot; offer the frame
     there before the ordinary teardown. */
  if (KosDynFrame_FLAGS(frame) & KOS_FRAME_RETURN_TO_CACHE) {
    KosDynFrame **cache =
        (KosDynFrame **) KosDynFrame_CACHE_SLOT(frame);
    KosDynFrame_FLAGS(frame) &= ~KOS_FRAME_RETURN_TO_CACHE;
    KosDynFrame_CACHE_SLOT(frame) = NULL;
    if (cache && kos_dynframe_cache_inert(frame, cache))
      return;
  }

  {
    PyObject *fn = KosDynFrame_FN(frame);
    if (fn) {
#if KOS_TRACE_EVENTS
      /* read before the offer: caching resets the frame flags */
      int coro_root = (KosDynFrame_FLAGS(frame) & KOS_FRAME_CORO_ROOT) != 0;
#endif
      KosDynFrame_FN(frame) = NULL;
      int cached = KosFunction_CacheFrame(fn, frame);
      Py_DECREF(fn);
      if (cached) {
#if KOS_TRACE_EVENTS
        if (coro_root)
          KOS_TRACE_EVENTS_INC(KOS_TE_CORO_FRAME_TO_CACHE);
#endif
        return;
      }
    }
  }

  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = NULL;

  kos_dynnode_return_to_freelist(frame);
}


static int dynnode_to_dict_frame(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "pc",
                        kos_insn_pc(KosDynFrame_PC((KosDynFrame *) node))) < 0)
    return -1;
  if (snapshot_set_uint(data, "acting_pc",
                        kos_insn_pc(KosDynFrame_ACTING_PC(
                            (KosDynFrame *) node))) < 0)
    return -1;
  if (snapshot_set_uint(data, "frame_flags",
                        KosDynFrame_FLAGS((KosDynFrame *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "code", (PyObject *) KosDynFrame_CODE((KosDynFrame *) node)) < 0)
    return -1;
  {
    const char *rname = KosDynFrame_RESUME_NAME((KosDynFrame *) node);
    PyObject *name = rname ? PyUnicode_FromString(rname) : NULL;
    int r;
    if (rname && ! name)
      return -1;
    r = snapshot_set_obj(data, "resume_name", name);
    Py_XDECREF(name);
    if (r < 0)
      return -1;
  }
  return 0;
}


/**
   Push a new frame with code and ns as dyn_top and the live frame. Callers: eval
   when entering a Python function.
*/
KosDynFrame *kos_dyn_push_frame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  if (unlikely(kos_eval_check_frame_room(
          ctx, KosContext_FRAME(ctx), 1) < 0)) {
    Py_XDECREF(ns);
    return NULL;
  }
  return kos_dyn_push_frame_unchecked(ctx, code, ns);
}


/**
   kos_dyn_push_frame with the frame-room check left to the caller: the
   simple call arms verify room once before probing the frame cache, so
   their miss install must not test it again.
*/
KosDynFrame *kos_dyn_push_frame_unchecked(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  KosDynFrame *cur = KosContext_FRAME(ctx);

  // CONTEXT: dirty, touches context directly
  KosDynFrame *frame = kos_dynframe_new(KosContext_DYN_TOP(ctx), code, ns);
  if (! frame)
    return NULL;

  /* Inherit demand extent from the current executing frame so that every
     child frame on the demand path carries the flag directly, enabling O(1)
     checks in kos_eval_in_demand without walking the dynchain. */
  if (cur) {
    KosDynFrame_FLAGS(frame) |= (KosDynFrame_FLAGS(cur) & KOS_FRAME_DEMAND);
    KosDynFrame_DEPTH(frame) = KosDynFrame_DEPTH(cur) + 1;
  }

  /* Inline retarget, specialized from kos_dyn_resync_after_push for a
     fresh FRAME node: flush the outgoing frame's live PCs, then load
     every live register directly from the new frame (its pc is the
     entry from kos_dynframe_new). Mirror of the return-side inline
     retarget in eval_plain_return. */
  if (cur)
    KosContext_FlushLiveRegsToFrame(ctx);
  kos_dyn_stamp_installed_segment(KosDynNode_STACK((KosDynNode *) frame),
                                  KosContext_BLOCK(ctx));
  KosDynFrame_BACK_FRAME(frame) = (KosDynNode *) cur;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_LoadFrame(ctx, frame, &KosDynNode_STACK((KosDynNode *) frame),
                       KosDynNode_NS((KosDynNode *) frame));

  return frame;
}


/**
   Push frame with NULL code/ns (placeholder). Callers: setup paths needing a
   frame before code is bound.
*/
KosDynFrame *kos_dyn_push_frame_empty(KosContext *ctx) {
  return kos_dyn_push_frame(ctx, NULL, NULL);
}


/**
   Push a frame for a native program: no code, and the namespace slot
   holds a reference to the empty namespace. A native frame introduces
   no scope and reads no name, so the fill only keeps the live register
   from being NULL while the frame is current, and the empty namespace
   says exactly that: nothing is visible through it and nothing can be
   stored through it.
*/
KosDynFrame *kos_dyn_push_frame_native(KosContext *ctx) {

  PyObject *empty = KosNamespace_Empty();

  if (! empty)
    return NULL;
  Py_INCREF(empty);
  return kos_dyn_push_frame(ctx, NULL, empty);
}



/**
   Offer a detached native-program frame to a holder's one-frame
   cache: strip it to an inert allocation and store it in *cache when
   the slot is empty. The namespace slot holds only an owned reference
   to the empty namespace (kos_dyn_push_frame_native) and is released
   here; a frame with code or a cache-participation fn is not a
   native-program frame and takes the ordinary free instead.
*/
int kos_dynframe_cache_inert(KosDynFrame *frame, KosDynFrame **cache) {

  if (*cache
      || KosDynFrame_CODE(frame)
      || KosDynFrame_FN(frame)
      || ! KosDynFrame_STACK(frame))
    return 0;

  Py_CLEAR(KosDynFrame_NS(frame));
  KosDynFrame_CACHE_SLOT(frame) = NULL;
  kos_dynframe_clear_resume(frame);

  KosStack_Clear(&KosDynFrame_STACK(frame));
  KosSegment_BACK(KosDynFrame_STACK(frame)) = NULL;
  KosDynFrame_BACK_FRAME(frame) = NULL;

  KosDynNode_PARENT(frame) = NULL;
  KosDynNode_FLAGS(frame) = KOS_DYNF_NONE;
  /* The caps a fresh code-less FRAME node gets from kos_dynframe_new
     (none: the fill it will carry is not an introduced scope), so the
     reinstall is indistinguishable from a new allocation. */
  KosDynNode_CAPS(frame) = 0;
  KosDynFrame_PC(frame) = NULL;
  KosDynFrame_ACTING_PC(frame) = NULL;
  KosDynFrame_FLAGS(frame) = 0;

  *cache = frame;
  return 1;
}


/**
   Drop the discardable extent nodes (NS, LOOP, FOREACH) open above
   frame, in preparation for tail-call frame reuse.  These are the
   extents tail-call promotion allows a call through: their uninstall
   is pure reference release, and it must happen here because the
   reused frame never reaches their POP_* instructions.  Any other node
   kind above the frame is a miscompiled or hand-assembled tail site and
   fails loudly rather than discarding an extent whose uninstall
   behavior must run.

   :return: 0 on success (DYN_TOP == frame), -1 with an exception set
*/
int kos_dyn_teardown_to_frame(KosContext *ctx, KosDynFrame *frame) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);

  while (top && top != (KosDynNode *) frame) {
    switch (KosDynNode_KIND(top)) {
      case KOS_DYN_NS:
      case KOS_DYN_LOOP:
      case KOS_DYN_FOREACH:
        break;
      default:
        PyErr_Format(PyExc_RuntimeError,
                     "tail call through non-discardable %s extent",
                     kos_dynnode_kind_name(KosDynNode_KIND(top)));
        return -1;
    }

    {
      KosDynNode *parent = KosDynNode_PARENT(top);
      kos_dyn_commit_dyn_top(ctx, parent);
      KosDynNode_PARENT(top) = NULL;
      kos_dynnode_free(top);
      top = parent;
    }
  }

  if (! top) {
    PyErr_SetString(PyExc_RuntimeError,
                    "tail-call frame not on the dynamic chain");
    return -1;
  }

  return 0;
}


/**
   Clear stack and metadata for tail-call reuse of the same frame object.
*/
void kos_dynframe_reset_for_tail(KosDynFrame *frame) {
  if (! frame)
    return;

  KosStack_Clear(&KosDynFrame_STACK(frame));

  /* Drop the namespace; the tail install sets the callee's */
  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = NULL;

  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = NULL;
  KosDynFrame_PC(frame) = NULL;
  KosDynFrame_ACTING_PC(frame) = NULL;
  KosDynFrame_FLAGS(frame) &= KOS_FRAME_DEMAND;  /* preserve demand bit */
  KosDynFrame_CACHE_SLOT(frame) = NULL;
  kos_dynframe_clear_resume(frame);
  /* the frame is being repurposed for a different function */
  Py_CLEAR(KosDynFrame_FN(frame));
}


void kos_dyn_swap_frame_for_tail(KosContext *ctx, KosDynFrame *ready) {

  KosDynFrame *live = KosContext_FRAME(ctx);
  KosDynNode *live_node = (KosDynNode *) live;
  KosDynNode *ready_node = (KosDynNode *) ready;

  KosDynNode_PARENT(ready_node) = KosDynNode_PARENT(live_node);
  KosDynNode_NODE_ID(ready_node) = 0;
  /* the demand bit and the parent-kind record are the live frame's,
     the same parent; assigned, not merged, so a ready frame that
     arrived with either bit set does not keep it */
  KosDynFrame_FLAGS(ready) =
      (KosDynFrame_FLAGS(ready)
       & ~(KOS_FRAME_DEMAND | KOS_FRAME_PARENT_IS_FRAME))
      | (KosDynFrame_FLAGS(live)
         & (KOS_FRAME_DEMAND | KOS_FRAME_PARENT_IS_FRAME));
  KosDynFrame_DEPTH(ready) = KosDynFrame_DEPTH(live);
  KosDynFrame_BACK_FRAME(ready) = KosDynFrame_BACK_FRAME(live);
  KosSegment_BACK(KosDynNode_STACK(ready_node)) =
      KosSegment_BACK(KosDynNode_STACK(live_node));

  KosContext_DYN_TOP(ctx) = ready_node;
  KosContext_LoadFrame(ctx, ready, &KosDynNode_STACK(ready_node),
                       KosDynNode_NS(ready_node));

  /* unlinked and no longer named by any register, the live frame goes:
     its block clear drops the call's window, and its release offers it
     to its own function's cache */
  KosDynNode_PARENT(live_node) = NULL;
  KosDynFrame_BACK_FRAME(live) = NULL;
  kos_dynframe_free(live);
}


/**
   Pop top if it is a FRAME; refresh the live frame. Does not run full uninstall
   (no extent teardown). Callers: simple frame exit when no extra nodes on top.
*/
int kos_dyn_pop_frame(KosContext *ctx) {
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to pop");
    return -1;
  }

  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }

  {
    KosDynNode *old_top = KosContext_DYN_TOP(ctx);
    KosDynNode *parent = KosDynNode_PARENT(old_top);

    kos_dyn_commit_dyn_top(ctx, parent);
    kos_dynnode_free(old_top);
  }

  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FRAME: visit the code and fn references, and
   the resume state through its kind.  The frame's namespace and
   operand segment are visited generically by kos_dynnode_traverse.
*/
static int dynnode_traverse_frame(KosDynNode *node, visitproc visit, void *arg) {
  KosDynFrame *frame = (KosDynFrame *) node;
  Py_VISIT(KosDynFrame_CODE(frame));
  Py_VISIT(KosDynFrame_FN(frame));
  return kos_resume_traverse(KosDynFrame_RESUME_KIND(frame),
                             KosDynFrame_RESUME_STATE(frame), visit, arg);
}


/* === Frame depth === */


unsigned kos_dyn_frame_depth_at(KosDynNode *node) {

  for (; node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_FRAME(node))
      return KosDynFrame_DEPTH(node);
  }
  return 0;
}


unsigned kos_dyn_frame_count(KosDynNode *top, KosDynNode *base) {

  unsigned count = 0;
  KosDynNode *node;

  for (node = top; node && node != base; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_FRAME(node))
      count++;
  }
  return count;
}


void kos_dyn_renumber_frames_counted(
    KosDynNode *top,
    KosDynNode *base,
    unsigned base_depth,
    unsigned count) {

  /* Top-down walk: the topmost frame sits count above the base. */
  unsigned depth = base_depth + count;
  KosDynNode *node;

  for (node = top; node && node != base; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_FRAME(node))
      KosDynFrame_DEPTH(node) = (uint16_t) depth--;
  }
}


void kos_dyn_renumber_frames(
    KosDynNode *top,
    KosDynNode *base,
    unsigned base_depth) {

  kos_dyn_renumber_frames_counted(top, base, base_depth,
                                  kos_dyn_frame_count(top, base));
}


// The end.
