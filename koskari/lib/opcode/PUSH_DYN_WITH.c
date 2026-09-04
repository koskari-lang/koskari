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
   PUSH_DYN_WITH: arg = constant pool index (name or None). Multi-shot context
   manager. Stack before: [..., mgr]. Pop mgr, resolve enter/exit, push block,
   push provisional KOS_DYN_WITH node (multi-shot, push = link only), then wind.
   Wind may suspend (async enter); otherwise result is bound to name.
*/

KOS_OP_HANDLER_COLD(PUSH_DYN_WITH) {
  PyObject *mgr = KosEvalState_StackPopRef(es);
  if (! mgr)
    RESULT_RAISE();

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  uint8_t self_bind = 0;
  if (resolve_context_enter_exit(mgr, &enter, &exit, &self_bind) < 0) {
    Py_DECREF(mgr);
    RESULT_RAISE();
  }

  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t pc = KosEvalState_ACTING_PC(es);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_DYN_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: no DYN_WITH extent at this PC");
    RESULT_RAISE();
  }
  KosExtentDesc *const ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: invalid extent");
    RESULT_RAISE();
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = KosOp_OBJ(insn);
  Py_XINCREF(name_obj);

  KosDynWith *with_node = kos_dyn_push_with_provisional(
      es, mgr, enter, exit, self_bind, name_obj, resume_pc,
      KOS_DYN_WITH_SHOT_MULTI);
  if (! with_node) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    RESULT_RAISE();
  }

  KosDynFrame *const caller = KosEvalState_FRAME(es);
  KosOpResult wr = kos_dyn_wind(es, (KosDynNode *) with_node);
  /* AWAIT: __enter__ returned an awaitable and the node stays
     provisional; the revive binds the settled value
     (kos_dyn_complete_pending) and continues into the body. */
  if (wr.kind == KOS_RES_AWAIT)
    return wr;

  if (wr.kind == KOS_RES_RAISE) {
    kos_dyn_pop_with(es, with_node);
    RESULT_RAISE();
  }

  /* REPROLOGUE means one of two things. Either __enter__ is a Koskari
     function whose frame is now the current frame: run it by
     continuing at the current pc, which is the start of that frame.
     Or the call left work for the trampoline (a transfer plan armed by
     the callee): return so the trampoline drains it first. Continuing
     in that second case would run the with body before the transfer
     it was meant to make. */
  if (wr.kind == KOS_RES_REPROLOGUE
      && (KosEvalState_FRAME(es) == caller || KosEvalState_PLAN(es)))
    return wr;

  RESULT_CONTINUE(es);
}


// The end.
