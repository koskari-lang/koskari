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
  Pop the return value(s) from the stack; eval_event_return delivers
  KOS_RES_RETURN at the driver boundary.
*/
KOS_OP_HANDLER(RETURN) {
  KOS_SYNC_PC(ctx, insn);
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  /* === Phase 1: Pop return value from stack ===
     opa=0 => Py_None, opa=1 => single value, opa=N => tuple of N values */
  SAYF("[RETURN] opa=%u frame=%p stack=%p len=%zu (pc advanced to %u)",
       opa, (void *) KosContext_FRAME(ctx),
       (void *) KosContext_StackHead(ctx),
       KosContext_StackLen(ctx),
       (unsigned) KosContext_PC(ctx));

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosContext_StackPopRef(ctx);
    if (unlikely(! val)) {
      SAYF("[RETURN] pop underrun: stack=%p len=%zu",
           (void *) KosContext_StackHead(ctx),
           (unsigned long) KosContext_StackLen(ctx));
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      RESULT_RAISE();
    }

  } else {
    val = KosContext_StackPopTail(ctx, (size_t) opa);
    if (unlikely(! val)) {
      SAY("[eval] RETURN PopTail underrun\n");
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      RESULT_RAISE();
    }
  }

  /* === Phase 2: deliver === */
  TAILCALL(kos_return_deliver, ctx, insn, val);
}


// The end.
