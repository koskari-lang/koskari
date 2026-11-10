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
  AWAIT: arg = stack element count. 1: pop TOS (awaitable), start/resume
  awaiting it; may return EVAL_AWAIT(val) or push the result. 0: bare
  cooperative suspend, pops nothing; EVAL_AWAIT(Py_None) payload and
  no py_await_iter. The resume pushes None and discards a sent value;
  (await) compiles to AWAIT 0 alone.
*/
KOS_OP_HANDLER(AWAIT) {
  KOS_SYNC_PC(ctx, insn);
  uint32_t arg = KosOp_ARG(insn);

  if (arg == 0u)
    RESULT_AWAIT(ctx, Py_NewRef(Py_None));

  if (unlikely(arg != 1u)) {
    PyErr_Format(PyExc_RuntimeError,
                 "AWAIT: arg must be 0 or 1 (stack count), got %u",
                 (unsigned) arg);
    RESULT_RAISE();
  }

  PyObject *val = KosContext_StackPopRef(ctx);
  if (unlikely(! val))
    RESULT_RAISE();

  /* An awaitable with an await suite is taken on its await frame now:
     the frame is installed with no resume and the kind is hopped into
     with the owned awaitable, the delivery the staged settle would
     have made on the next pass. Everything else goes through the
     settle. */
  {
    KosAwaitSlots *slots;

    if (KosAwait_CheckSlots(val, &slots)) {
      if (unlikely(! kos_await_frame_install(ctx, NULL))) {
        Py_DECREF(val);
        RESULT_RAISE();
      }
      RESUME_HOP(slots->ka_install, ctx, NULL, val);
    }
  }

  RESULT_RELAY(ctx, kos_await_dispatch(ctx, val));
}


// The end.
