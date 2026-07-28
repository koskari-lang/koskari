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
   PUSH_WITH: arg = constant pool index (name or None). Single-shot context
   manager. Stack before: [..., mgr]. Pop mgr, resolve enter/exit, push block,
   push provisional KOS_DYN_WITH node (single-shot, push = link only), then wind.
   Wind may suspend (async enter); otherwise result is bound to name.
*/

KOS_OP_HANDLER_COLD(PUSH_WITH) {
  PyObject *mgr = KosEvalState_StackPopRef(es);
  if (! mgr)
    return EVAL_RAISE();

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  if (resolve_context_enter_exit(mgr, &enter, &exit) < 0) {
    Py_DECREF(mgr);
    return EVAL_RAISE();
  }

  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t pc = KosEvalState_ACTING_PC(es);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: no WITH extent at this PC");
    return EVAL_RAISE();
  }
  KosExtentDesc *const ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: invalid extent");
    return EVAL_RAISE();
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = get_name_from_insn(es, insn);
  Py_XINCREF(name_obj);

  KosDynWith *with_node = kos_dyn_push_with_provisional(
      es, mgr, enter, exit, name_obj, resume_pc, KOS_DYN_WITH_SHOT_SINGLE_ACTIVE);
  if (! with_node) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return EVAL_RAISE();
  }

  KosOpResult wr = kos_dyn_wind(es, (KosDynNode *) with_node);

  if (wr.kind == KOS_RES_RAISE) {
    kos_dyn_pop_with(es, with_node);
    return EVAL_RAISE();
  }

  if (wr.kind == KOS_RES_AWAIT) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return wr;
  }

  /* REPROLOGUE: __enter__ is a KosCallable whose frame is installed and
     runs when we hand control back. Do not touch the pc here — the
     current frame is now the callee's. This frame's pc already sits past
     PUSH_WITH, so the with body runs once the callee returns and
     kos_dynnode_on_control_returned binds the result. */
  if (wr.kind != KOS_RES_NORMAL)
    return wr;

  return EVAL_NORMAL();
}


// The end.
