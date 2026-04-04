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
   push provisional KOS_DYN_DYN_WITH node (push = link only), then wind (enter).
   Wind may suspend (async enter); otherwise result is bound to name.
*/

KOS_OP_HANDLER(PUSH_DYN_WITH) {

  PyObject *mgr = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! mgr)
    return KOS_OP_POST_ERR();

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  if (resolve_context_enter_exit(mgr, &enter, &exit) < 0) {
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }

  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = CURRENT_ACTING_PC(ctx);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_DYN_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: no DYN_WITH extent at this PC");
    return KOS_OP_POST_ERR();
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: invalid extent");
    return KOS_OP_POST_ERR();
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = get_name_from_insn(code, insn);
  Py_XINCREF(name_obj);

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }

  KosDynWith *with_node = kos_dyn_push_dyn_with_provisional(ctx, mgr, enter,
                                                            exit, name_obj,
                                                            resume_pc);
  if (! with_node) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return KOS_OP_POST_ERR();
  }

  int r = kos_dyn_wind(ctx, (KosDynNode *) with_node, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    uint32_t same_pc = CURRENT_ACTING_PC(ctx);
    CURRENT_PC(ctx) = same_pc;
    KosDynFrame_ACTING_PC(CURRENT_FRAME(ctx)) = same_pc;
    return KOS_OP_POST_SUSPEND();
  }

  if (r < 0) {
    kos_dyn_pop_with(ctx, with_node);
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    return KOS_OP_POST_ERR();
  }

  return KOS_OP_RUN();
}


// The end.
