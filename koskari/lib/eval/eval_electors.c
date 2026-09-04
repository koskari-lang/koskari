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
   koskari/lib/eval/eval_electors.c

   Electors: handlers elected once for a site's guarded operand and
   tail-called on every later execution. This file is #included into
   eval.c after the opcode handler macros and before handlers.incl, so
   a handler can name its family's electing function; it is not a
   standalone translation unit.

   A cache-style elector lives in a KOS_CACHE_ELECTOR slot. The handler
   fetches the guarded operand once, tests the slot's type guard, and
   tail-calls the elected body with the operand as its value, ownership
   transferring with it. A guard miss and an ungated site take the same
   tail call into the family's electing function, which runs the
   family's ladder, settles the slot, and dispatches. Demotion follows
   the method cache: when the misses since the last population reach
   the family's limit the slot becomes a MEGAMORPHIC bypass whose guard
   never matches.

   See design/interpreter/electors.md.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* === Elector prototype and slot access === */


/* Every elector has the KosThreadFn prototype, as a handler does. It is
   a direct musttail target, so it stays out of line. insn is the site's
   own KosOp and value is the guarded operand, with the ownership the
   elector's family fixes. */
#define KOS_ELECTOR(name)                                               \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult el_##name(          \
      KosEvalState *es, void *insn, PyObject *value)


/**
   The hot-path guard: the slot holds an elector for exactly tp. A
   demoted slot has a NULL guard type and never matches.
*/
static inline int kos_elector_guard(KosCacheSlot *slot, PyTypeObject *tp) {
  return slot
      && slot->kind == KOS_CACHE_ELECTOR
      && slot->u.elector.type == tp;
}


/* Under the safety net a guard hit re-runs the family's classification
   and refuses to dispatch when it disagrees with the elected body. The
   owned operand is released on that exit. */
#if KOS_EVAL_SAFETYNET
# define KOS_ELECTOR_SAFETYNET(slot, expect, owned)                     \
  do {                                                                  \
    if (unlikely((slot)->u.elector.fn != (expect))) {                   \
      Py_XDECREF(owned);                                                \
      PyErr_SetString(PyExc_SystemError,                                \
                      "elector disagrees with its family's ladder");    \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_ELECTOR_SAFETYNET(slot, expect, owned)  ((void) 0)
#endif


/**
   Settle a slot after its family's ladder ran for an operand of type tp
   and chose fn (NULL when the family has no elector for that operand).
   Populates an empty slot, re-populates an occupied one under a new
   type while counting the miss, and demotes at limit. A demoted slot is
   left alone.

   The hit path is the handler and takes no store, so misses count since
   the last population rather than consecutively.
*/
static void kos_elector_settle(
    KosCacheSlot *slot,
    PyTypeObject *tp,
    KosThreadFn fn,
    uint8_t limit) {

  if (! slot) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_UNCACHED);
    return;
  }

  if (slot->kind == KOS_CACHE_ELECTOR) {
    uint8_t n;

    if (slot->u.elector.mode == KOS_ELECTOR_MEGAMORPHIC) {
      KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_BYPASS);
      return;
    }

    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
    n = (uint8_t) (slot->u.elector.misses + 1);
    if (n >= limit) {
      slot->u.elector.fn = NULL;
      slot->u.elector.type = NULL;
      slot->u.elector.misses = 0;
      slot->u.elector.mode = KOS_ELECTOR_MEGAMORPHIC;
      return;
    }
    if (! fn) {
      slot->u.elector.misses = n;
      return;
    }
    slot->u.elector.fn = fn;
    slot->u.elector.type = tp;
    slot->u.elector.misses = n;
    return;
  }

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  if (fn) {
    slot->u.elector.fn = fn;
    slot->u.elector.type = tp;
    slot->u.elector.misses = 0;
    slot->u.elector.mode = KOS_ELECTOR_ELECTED;
    slot->kind = KOS_CACHE_ELECTOR;
  }
}


/* === CALL_FAST family === */


/* Misses since the last population before a CALL_FAST site demotes. */
#define KOS_ELECTOR_CALL_FAST_MISS_LIMIT 8


/**
   The generic body: the CALL_FAST prologue as it stands without an
   election. value is the owned callable; the argument window is the
   stack tail, and kos_call_invoke owns the window drop.
*/
KOS_ELECTOR(call_fast_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  unsigned n = KosExtra_IMM(row, 1);
  PyObject *const *argv = NULL;

  if (n) {
    argv = KosEvalState_StackTailPtr(es, n);
    if (unlikely(! argv)) {
      Py_DECREF(value);
      RESULT_RAISE();
    }
  }

  RESULT_RELAY(es, kos_call_invoke(
      es, value, NULL, argv, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED));
}


/**
   Exact KosFunction at full positional supply on non-yielding code with
   no self-bind: steal-bind the window into the function's parked frame
   when one is cached, else into a fresh namespace, and install. The
   owned callable transfers into the frame's FN tag. A callable of the
   guard type that no longer fits that shape (the fast slot was rebound
   to another function) takes the generic body without re-electing.
*/
KOS_ELECTOR(call_fast_kosfunction_simple) {
  PyObject *callable = value;
  const KosExtra *row = KosOp_ROW(insn);
  Py_ssize_t nargs = (Py_ssize_t) KosExtra_IMM(row, 1);
  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  KosSegment **caller_slot = KosEvalState_BLOCK(es);
  KosDynFrame *frame;

  if (unlikely(nargs + 1 != KosFunction_SIMPLE_ARITY1(callable)
               || KosFunction_BIND_SELF_STATE(callable) != 2
               || (code && KosCode_YIELDS(code))))
    TAILCALL(el_call_fast_generic, es, insn, value);

  frame = KosFunction_CACHED_FRAME(callable);
  if (likely(frame)) {
    /* Steal-bind into the parked namespace first: on failure nothing is
       stolen, the window is still on the block, and the frame stays
       parked. */
    if (unlikely(KosFormals_SimpleStealApplyBlock(
            KosDynFrame_NS(frame), *caller_slot, nargs) < 0)) {
      if (nargs)
        kos_stack_drop_noerr_fast(caller_slot, (size_t) nargs);
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    KosFunction_CACHED_FRAME(callable) = NULL;
    kos_dyn_push_ready_frame(es, frame);
    KosDynFrame_FN(frame) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
    RESULT_RELAY(es, kos_call_installed(es));
  }

  {
    PyObject *frame_ns = KosFunction_SimpleCallNamespaceStealBlock(
        callable, *caller_slot, nargs);

    if (unlikely(! frame_ns)) {
      if (nargs)
        kos_stack_drop_noerr_fast(caller_slot, (size_t) nargs);
      Py_DECREF(callable);
      return EVAL_RAISE();
    }

    /* The stolen span left the window, so nothing drops after the
       install. kos_dyn_push_frame consumes frame_ns even on failure. */
    frame = kos_dyn_push_frame(es, code, frame_ns);
    if (unlikely(! frame)) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    KosDynFrame_FN(frame) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_STEAL);
    RESULT_RELAY(es, kos_call_installed(es));
  }
}


/**
   The CALL_FAST ladder's verdict for a callable of type tp: the elector
   that serves it, or NULL when the generic body does. Only static types
   may be elected while the slot layout keeps no version tag.
*/
static inline KosThreadFn call_fast_classify(PyTypeObject *tp) {
  if (tp == KOS_EVALUATED_PYTYPE(KosFunctionType))
    return el_call_fast_kosfunction_simple;
  return NULL;
}


/**
   The electing function, reached when the guard missed or the site has
   no slot: classify the owned callable, settle the slot, and dispatch.
*/
KOS_ELECTOR(call_fast_elect) {
  KosCacheSlot *slot = KosOp_SLOT(insn);
  PyTypeObject *tp = Py_TYPE(value);
  KosThreadFn fn = call_fast_classify(tp);

  kos_elector_settle(slot, tp, fn, KOS_ELECTOR_CALL_FAST_MISS_LIMIT);
  if (fn)
    TAILCALL(fn, es, insn, value);
  TAILCALL(el_call_fast_generic, es, insn, value);
}


// The end.
