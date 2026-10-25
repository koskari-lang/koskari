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
  CALL_FAST and TAIL_CALL_FAST: arg = extra index. Schema FASTVAR (slot), COUNT
  (arg count). Call callable from fast local slot. Stack [arg_0, ...,
  arg_{N-1}]. Prologue only: deref the callable, try the cached frame, else
  take the borrowed argv window and delegate to kos_call_invoke (which owns
  the window drop). A TAIL_CALL
  instruction reaches this same handler and adds the tail bit, which the core
  turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_FAST) {
  KOS_SYNC_PC(ctx, insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  unsigned n = KosExtra_IMM(row, 1);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *callable = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! callable))
    RESULT_RAISE();

  /* Frame-cache hit, installed here; the tail twin reuses the live
     frame and never consults the cache. The arm binds from the block
     itself, so the window pointer is taken only for the general call
     below. */
  if (! tail) {
    KosOpResult r;
    int taken = kos_call_cached_frame_install(
        ctx, callable, NULL, 0, (Py_ssize_t) n, &r);

    if (likely(taken > 0))
      RESULT_RELAY(ctx, r);
    if (unlikely(taken < 0)) {
      if (n)
        kos_stack_drop_noerr_fast(KosContext_BLOCK(ctx), n);
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  PyObject *const *argv = NULL;
  if (n) {
    argv = KosContext_StackTailPtr(ctx, n);
    if (unlikely(! argv)) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  RESULT_RELAY(ctx, kos_call_invoke_from_handler(
      ctx, tail, callable, NULL, argv, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED));
}


// The end.
