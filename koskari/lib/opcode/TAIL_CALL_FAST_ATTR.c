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
   TAIL_CALL_FAST_ATTR: arg = extra index. Schema FASTVAR (slot), CONST
   (attr name), COUNT (arg count). Stack [arg_0, ..., arg_{N-1}] only;
   obj is the value at the fast local slot (borrowed). Resolve obj.attr,
   then call. Unbound fn: fn(obj, *args). Bound/other: callable(*args).
   Tail call: do not push a new frame; remove or recycle the current one.
*/
KOS_OP_HANDLER(TAIL_CALL_FAST_ATTR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  unsigned n = row.a2;

  PyObject *attr_name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! attr_name)
    return EVAL_RAISE();

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *obj = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! obj)
    return EVAL_RAISE();

  PyObject *const *argv = NULL;
  if (n > 0) {
    argv = KosEvalState_StackTailPtr(es, (size_t) n);
    if (! argv) {
      Py_DECREF(obj);
      return EVAL_RAISE();
    }
  }

  /* Per-site cache: this opcode requests one slot (opcodes.incl). The index
     is unset when the site got no slot (cache gated off for the code object).
     The resolver treats a NULL slot as an uncached lookup. */
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *cache = (cache_ix == KOS_CACHE_UNSET)
      ? NULL : &KosCode_CACHE(code)[cache_ix];

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, attr_name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  /* Prologue only: kos_call_invoke owns the window drop and the frame
     handoff; an unbound resolution injects obj as self via the core's
     prepend. */
  KosOpResult r = kos_call_invoke(es, resolved,
                                  (send_self == 0) ? obj : NULL,
                                  argv, (Py_ssize_t) n, NULL,
                                  KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                                  | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  return r;
}


// The end.
