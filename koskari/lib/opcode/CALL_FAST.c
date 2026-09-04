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
  CALL_FAST: arg = extra index. Schema FASTVAR (slot), COUNT (arg count).
  Call callable from fast local slot. Stack [arg_0, ..., arg_{N-1}].
  Prologue only: deref the callable and hand it, owned, to the site's
  elected body when the cache slot's guard holds, else to the family's
  electing function (eval/eval_electors.c), which runs the call ladder
  and settles the slot. One cache slot: KOS_CACHE_ELECTOR.
*/
KOS_OP_HANDLER(CALL_FAST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *callable = kos_ns_fast_deref(ns, (size_t) row.a0);
  if (unlikely(! callable))
    RESULT_RAISE();

  KosCacheSlot *slot = kos_op_cache_slot(code, insn);
  if (likely(kos_elector_guard(slot, Py_TYPE(callable)))) {
    KOS_ELECTOR_SAFETYNET(slot, call_fast_classify(Py_TYPE(callable)),
                          callable);
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
    TAILCALL(slot->u.elector.fn, es, insn, callable);
  }

  TAILCALL(el_call_fast_elect, es, insn, callable);
}


// The end.
