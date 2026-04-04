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
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_FINALLY: top is not a region");
    return KOS_OP_POST_ERR();
  }
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! KosDynNode_IS_FINALLY(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_FINALLY: top is not a FINALLY region");
    return KOS_OP_POST_ERR();
  }

  if (KosDynFinally_HANDLING_RAISE((KosDynFinally *) region)) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_GetExcInfo(&typ, &val, &tb);
    PyErr_Restore(typ, val, tb);
    PyErr_SetExcInfo(NULL, NULL, NULL);
    KosDynFinally_HANDLING_RAISE((KosDynFinally *) region) = 0;
  }
  if (KosDynFinally_HANDLING_RETURN((KosDynFinally *) region))
    KosDynFinally_HANDLING_RETURN((KosDynFinally *) region) = 0;
  if (KosStackBlock_PopBlock(&CURRENT_STACK(ctx), 1) < 0)
    return KOS_OP_POST_ERR();
  if (KosDynNode_IS_TRY(region)) kos_dyn_pop_try(ctx, (KosDynTry *) region); else if (KosDynNode_IS_FINALLY(region)) kos_dyn_pop_finally(ctx, (KosDynFinally *) region); else if (KosDynNode_IS_LOOP(region)) kos_dyn_pop_loop(ctx, (KosDynLoop *) region); else if (KosDynNode_IS_FOREACH(region)) kos_dyn_pop_foreach(ctx, (KosDynForeach *) region);
  (void) out_ev;
  /* Restoring the active exception must run through the prologue so
     eval_phase_pending_exception observes PyErr before the next opcode. */
  if (PyErr_Occurred())
    return KOS_OP_PROLOGUE();
  return KOS_OP_RUN();
}
