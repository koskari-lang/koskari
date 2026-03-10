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
   push provisional KOS_DYN_DYN_WITH node, call enter (auto-await if awaitable),
   bind result to name.
*/

KOS_OP_HANDLER(PUSH_DYN_WITH) {

  PyObject *mgr = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! mgr)
    return -1;

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  if (resolve_context_enter_exit(mgr, &enter, &exit) < 0) {
    Py_DECREF(mgr);
    return -1;
  }

  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = CURRENT_ACTING_PC(ctx);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_DYN_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: no DYN_WITH extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: invalid extent");
    return -1;
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = get_name_from_insn(code, insn);
  Py_XINCREF(name_obj);

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return -1;
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
    return -1;
  }

  int r = kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL);
  if (r == 1)
    return 1;

  if (r < 0) {
    kos_dyn_pop_with(ctx, with_node);
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    return -1;
  }

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result) {
    kos_dyn_pop_with(ctx, with_node);
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    return -1;
  }

  return finalize_provisional_with_enter(ctx, result, with_node);
}


// The end.
