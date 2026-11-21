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
   POP_FINALLY: NOARG. Promote one value (TOS) from finally block to parent
   before dropping. Verify top is a FINALLY region; if we were handling a
   raise, restore the exception. Then pop the region.
*/

KOS_OP_HANDLER(POP_FINALLY) {
  KOS_SYNC_PC(ctx, insn);

  KosDynNode *region = KosContext_DYN_TOP(ctx);
  if (! region || ! KosDynNode_IS_FINALLY(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_FINALLY: top is not a FINALLY region");
    RESULT_RAISE();
  }

  if (KosDynFinally_HANDLING_RAISE((KosDynFinally *) region)) {
    /* Re-raise the exception the region kept when it consumed it; the
       pop below gives back the handler-visible state it displaced. */
    KosExc_Reraise(KosDynFinally_HANDLING((KosDynFinally *) region));
    KosDynFinally_HANDLING_RAISE((KosDynFinally *) region) = 0;

    /* Raise delivery cleared the region stack (dyn_walk consume), so
       there is no promoted value; pop the region and let the restored
       exception continue through the prologue. */
    if (unlikely(kos_dyn_pop_finally(ctx, (KosDynFinally *) region) < 0))
      RESULT_RAISE();
    RESULT_REPROLOGUE(ctx);
  }

  /* A control transfer stored here by an unwind walk resumes as itself
     once the region is gone; the finally body ran for its side effects
     only. Without this the promoted value would just sit on the parent
     stack, and only a trailing RETURN would carry it out. */
  uint8_t pending = KosDynFinally_HANDLING_CONTROL((KosDynFinally *) region);
  if (pending)
    KosDynFinally_HANDLING_CONTROL((KosDynFinally *) region) = 0;

  {
    PyObject *promoted = KosStack_PopRef(&KosDynNode_STACK(region));
    if (! promoted)
      RESULT_RAISE();
    if (unlikely(kos_dyn_pop_finally(ctx, (KosDynFinally *) region) < 0)) {
      Py_DECREF(promoted);
      RESULT_RAISE();
    }

    /* The region is gone before the walk resumes, so an enclosing
       enabled FINALLY takes its own delivery. */
    switch (pending) {
      case KOS_PENDING_RETURN_FRAME:
        RESULT_RETURN(ctx, promoted);
      case KOS_PENDING_RETURN_BREAK:
        RESULT_RELAY(ctx, kos_dyn_unwind_to_loop(
            ctx, KOS_XFER_EVENT_BREAK, promoted));
      case KOS_PENDING_RETURN_CONTINUE:
        RESULT_RELAY(ctx, kos_dyn_unwind_to_loop(
            ctx, KOS_XFER_EVENT_CONTINUE, promoted));
      default:
        break;
    }

    if (KosContext_StackPushRef(ctx, promoted) < 0) {
      Py_DECREF(promoted);
      RESULT_RAISE();
    }
    Py_DECREF(promoted);
  }

  /* Restoring the active exception must run through the prologue so
     eval_phase_pending_exception observes PyErr before the next opcode. */
  if (PyErr_Occurred())
    RESULT_REPROLOGUE(ctx);
  RESULT_NEXT(ctx);
}


// The end.
