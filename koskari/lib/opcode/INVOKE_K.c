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
   INVOKE_K: arg unused. Pop value then continuation (Python wrapper). Invoke
   the continuation with that value, replacing current execution state.
*/

KOS_OP_HANDLER(INVOKE_K) {
  PyObject *value = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! value) {
    SAY("[eval] INVOKE_K pop value failed\n");
    return -1;
  }

  PyObject *cont_obj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! cont_obj) {
    SAY("[eval] INVOKE_K pop cont failed\n");
    Py_DECREF(value);
    return -1;
  }

  if (PyLong_Check(value))
    SAYF("[eval] INVOKE_K value=%ld k=%p\n",
         PyLong_AsLong(value), (void *) cont_obj);

  if (! KosPyDelimitedContinuation_Check(cont_obj)) {
    PyErr_SetString(PyExc_TypeError, "INVOKE_K: TOS is not a DelimitedContinuation");
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }

  KosContinuation *k = ((KosPyDelimitedContinuation *) cont_obj)->captured;
  if (! k) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_RuntimeError,
                    "INVOKE_K: continuation has no captured state");
    return -1;
  }

  /* Validate token ownership */
  if (k->token_ref != (PyObject *) KosContext_TOKEN(ctx)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K: continuation does not belong to current evaluator");
    return -1;
  }

  /* Validate prompt exists in current dynamic chain */
  if (! kos_find_prompt(ctx, k->prompt_id)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K: prompt not found in dynamic chain");
    return -1;
  }

  /* PC already advanced: return will resume at CURRENT_PC (next instr) */
  SAYF("[INVOKE_K] calling kos_invoke_continuation: captured_chain=%p frame=%p stack_len=%zu return_pc=%u",
       (void *) k->captured_chain, (void *) CURRENT_FRAME(ctx),
       CURRENT_STACK(ctx) ? KosStackBlock_LEN(CURRENT_STACK(ctx)) : 0,
       (unsigned) CURRENT_PC(ctx));
  if (kos_invoke_continuation(ctx, k, value, 0, cont_obj) < 0) {
    SAY("[eval] INVOKE_K kos_invoke_continuation returned -1\n");
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }
  Py_DECREF(value);
  /* cont_obj not DECREF'd: invoke transfers control on success */
  (void) out_ev;
  (void) insn;
  return 0;
}
