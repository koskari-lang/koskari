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
   CALL_FAST_ATTR and TAIL_CALL_FAST_ATTR: arg = extra index. Schema FASTVAR
   (slot), CONST (attr name), COUNT (arg count). Stack [arg_0, ..., arg_{N-1}]
   only; obj is the value at the fast local slot (borrowed). Resolve obj.attr,
   then call. Unbound fn: fn(obj, *args). Bound/other: callable(*args). A
   TAIL_CALL instruction reaches this same handler and adds the tail bit, which
   the core turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_FAST_ATTR) {
  KOS_SYNC_PC(es, insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  uint32_t slot_idx = KosExtra_IMM(row, 0);
  unsigned n = KosExtra_IMM(row, 2);

  PyObject *attr_name = KosExtra_OBJ(row, 1);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *obj = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! obj)
    RESULT_RAISE();

  /* Per-site cache: this opcode requests one slot (opcodes.incl). The index
     is unset when the site got no slot (cache gated off for the code object).
     The resolver treats a NULL slot as an uncached lookup. */
  KosCacheSlot *cache = KosOp_SLOT(insn);

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, attr_name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  PyObject *self_arg = (send_self == 0) ? obj : NULL;

  /* Frame-cache hit, installed here; the tail twin reuses the live
     frame and never consults the cache. The handler's receiver
     reference moves into slot 0 when the callee takes self, so a taken
     arm releases it only when it did not. */
  if (! tail) {
    KosOpResult r;
    int taken = kos_call_cached_frame_install(
        es, resolved, self_arg, self_arg != NULL, (Py_ssize_t) n, &r);

    if (likely(taken > 0)) {
      if (! self_arg)
        Py_DECREF(obj);
      RESULT_RELAY(es, r);
    }
    if (unlikely(taken < 0)) {
      if (n)
        kos_stack_drop_noerr_fast(KosEvalState_BLOCK(es), n);
      Py_DECREF(resolved);
      Py_DECREF(obj);
      RESULT_RAISE();
    }
  }

  /* Prologue only: the window pointer is taken here, past the cached
     arm that binds from the block itself; kos_call_invoke owns the
     window drop; an unbound resolution injects obj as self via the
     core's prepend. */
  PyObject *const *argv = NULL;
  if (n > 0) {
    argv = KosEvalState_StackTailPtr(es, (size_t) n);
    if (unlikely(! argv)) {
      Py_DECREF(resolved);
      Py_DECREF(obj);
      RESULT_RAISE();
    }
  }

  KosOpResult r = kos_call_invoke_from_handler(
      es, tail, resolved, self_arg, argv, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  RESULT_RELAY(es, r);
}


// The end.
