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
   RETURN: arg = 0 (None), 1 (single value), or N (tuple of N values).
   Pop the return value(s) from the stack and stop the context with
   KOS_EV_RETURN.
*/

KOS_OP_HANDLER(RETURN) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  /* === Phase 1: Pop return value from stack ===
     opa=0 => Py_None, opa=1 => single value, opa=N => tuple of N values */
  SAYF("[RETURN] opa=%u frame=%p stack=%p len=%zu (pc advanced to %u)",
       opa, (void *) KosEvalState_FRAME(es),
       (void *) KosEvalState_StackHead(es),
       KosEvalState_StackLen(es),
       (unsigned) KosEvalState_PC(es));

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosEvalState_StackPopRef(es);
    if (! val) {
      SAYF("[RETURN] pop underrun: stack=%p len=%zu",
           (void *) KosEvalState_StackHead(es),
           (unsigned long) KosEvalState_StackLen(es));
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return EVAL_RAISE();
    }

  } else {
    val = KosEvalState_StackPopTail(es, (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return EVAL_RAISE();
    }
  }

  /* === Phase 2a: Delimited continuation return (k(value)) ===
     When inside a reset/prompt, k(value) returns the value to the call site
     of the continuation. This bypasses the normal parent chain. Rare path. */
  KosDynDelim *ret_delim = kos_find_nearest_delim(es, KOS_DYN_DELIM_MODE_K_RETURN);
  if (unlikely(ret_delim
               && KosDynDelim_RETURN_CHAIN(ret_delim) != NULL
               && KosDynDelim_RETURN_PC(ret_delim) != (uint32_t) -1)) {
    return KosEvalState_KReturnDelimited(es, ret_delim, val);
  }

  /* === Phase 2b/2c: centralized in eval_event_return === */
  return EVAL_RETURN(val);
}


// The end.
