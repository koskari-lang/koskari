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

KOS_OP_HANDLER(PUSH_WITH) {
  PyObject *mgr = KosEvalState_StackPopRef(es);
  if (! mgr)
    return KOS_OP_POST_ERR();

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  if (resolve_context_enter_exit(mgr, &enter, &exit) < 0) {
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }

  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t pc = KosEvalState_ACTING_PC(es);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: no WITH extent at this PC");
    return KOS_OP_POST_ERR();
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: invalid extent");
    return KOS_OP_POST_ERR();
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = get_name_from_insn(code, insn);
  Py_XINCREF(name_obj);

  KosDynWith *with_node = kos_dyn_push_with_provisional(
      es, mgr, enter, exit, name_obj, resume_pc, KOS_DYN_WITH_SHOT_SINGLE_ACTIVE);
  if (! with_node) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }

  KosDynNode_STACK((KosDynNode *) with_node) = KosStackBlock_NewEmpty();
  if (! KosDynNode_STACK((KosDynNode *) with_node)) {
    kos_dyn_pop_with(es, with_node);
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }
  KosEvalState_RefreshDynCaches(es);

  int r = kos_dyn_wind(es, (KosDynNode *) with_node, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return KOS_OP_POST_SUSPEND();
  }

  if (r < 0) {
    kos_dyn_pop_with(es, with_node);
    return KOS_OP_POST_ERR();
  }

  return KOS_OP_RUN();
}


// The end.
