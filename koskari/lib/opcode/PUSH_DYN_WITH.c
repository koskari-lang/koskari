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
   manager. Stack before: [..., mgr]. Pop mgr, resolve enter/exit, call enter
   (auto-await if awaitable), bind result to name, push stack block, push
   KOS_DYN_DYN_WITH node.
*/

KOS_OP_HANDLER(PUSH_DYN_WITH) {

  if (KosContext_PENDING_WITH_ENTER_MGR(ctx))
    return kos_complete_pending_with_enter(ctx, out_ev);

  PyObject *mgr = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! mgr)
    return -1;

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  if (resolve_context_enter_exit(mgr, &enter, &exit) < 0) {
    Py_DECREF(mgr);
    return -1;
  }

  PyObject *result = PyObject_CallNoArgs(enter);
  if (! result) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return -1;
  }

  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_DYN_WITH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(result);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: no DYN_WITH extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(result);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_DYN_WITH: invalid extent");
    return -1;
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  PyObject *name_obj = get_name_from_insn(code, insn);
  Py_XINCREF(name_obj);

  return kos_push_with_after_enter(ctx, out_ev, result, mgr, enter, exit,
                                   name_obj, resume_pc, 1);
}


// The end.
