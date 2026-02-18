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
   manager. Stack before: [..., mgr]. Pop mgr, resolve __enter__/__exit__,
   call enter, optionally bind result to name, push stack block, push
   KOS_DYN_WITH node. Phase 1: sync only.
*/

KOS_OP_HANDLER(PUSH_WITH) {
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

  PyObject *name_obj = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (name_obj && name_obj != Py_None) {
    if (frame_ns_bind(ctx, name_obj, result) < 0) {
      Py_DECREF(result);
      Py_DECREF(exit);
      Py_DECREF(enter);
      Py_DECREF(mgr);
      return -1;
    }
  }
  else
    Py_DECREF(result);

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return -1;
  }

  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_WITH);
  if (eid == KOS_EXTENT_NONE) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: no WITH extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WITH: invalid extent");
    return -1;
  }
  uint32_t resume_pc = ext->pop_pc + 1u;

  if (! kos_dyn_push_with(ctx, mgr, enter, exit, NULL, resume_pc)) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    return -1;
  }

  (void) out_ev;
  return 0;
}


// The end.
