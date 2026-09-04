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
  YIELD: arg = 0 (None), 1 (single value), or N (tuple of N values).
  Pop the value(s) from the stack and return EVAL_YIELD(val); apply_kos_op_result
  routes to eval_event_yield for walk/freeze and node delivery.
  Resumption (send value) is handled by py_resume_inject at re-entry.
*/
KOS_OP_HANDLER(YIELD) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosEvalState_StackPopRef(es);
    if (unlikely(! val)) {
      SAY("[eval] YIELD pop underrun\n");
      RESULT_RAISE();
    }

  } else {
    val = KosEvalState_StackPopTail(es, (size_t) opa);
    if (unlikely(! val)) {
      SAY("[eval] YIELD PopTail underrun\n");
      RESULT_RAISE();
    }
  }

  /* A flattened generator delivers to its consumer here and threads
     into it: the same freeze and delivery the trampoline would run for
     the YIELD signal, minus the trip. A generator an adapter drives
     surfaces the value as the terminal YIELD. */
  KosDynGen *gen = kos_dyn_find_gen(es);

  if (gen && KosDynGen_IS_FLATTENED(gen)) {
    KosOpResult r;

    KOS_TRACE_POST_OP_WRAP(event_yield, r = eval_event_yield(es, gen, val));
    if (likely(r.kind == KOS_RES_DISPATCH))
      RESULT_CONTINUE(es);
    if (unlikely(r.kind == KOS_RES_YIELD)) {
      /* The freeze found no consumer for a generator that had one:
         never hand the trampoline a YIELD it would deliver again. */
      Py_DECREF(r.val);
      PyErr_SetString(PyExc_SystemError,
                      "flattened generator yield found no consumer");
      RESULT_RAISE();
    }
    return r;
  }

  return EVAL_YIELD(val);
}


// The end.
