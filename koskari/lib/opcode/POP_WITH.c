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
   POP_WITH: arg unused. Pop single-shot with context. Call exit(mgr, typ, val, tb);
   if exception and exit returns true, swallow; else re-raise. Drop stack block,
   pop KOS_DYN_WITH node, push None. Phase 1: sync exit only.
*/

KOS_OP_HANDLER(POP_WITH) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WITH: top is not a with node");
    return -1;
  }
  KosDynWith *with_node = (KosDynWith *) top;
  PyObject *exit_fn = KosDynWith_EXIT(with_node);

  PyObject *typ = NULL;
  PyObject *val = NULL;
  PyObject *tb = NULL;
  if (PyErr_Occurred())
    PyErr_GetExcInfo(&typ, &val, &tb);
  else
    typ = val = tb = Py_None;

  PyObject *result = PyObject_CallFunctionObjArgs(
      exit_fn, typ, val, tb, NULL);

  if (! result) {
    if (typ != Py_None)
      Py_XDECREF(typ);
    if (val != Py_None)
      Py_XDECREF(val);
    if (tb != Py_None)
      Py_XDECREF(tb);
    return -1;
  }

  int had_exception = (typ != Py_None);

  if (had_exception && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_DECREF(result);
    if (typ != Py_None)
      Py_XDECREF(typ);
    if (val != Py_None)
      Py_XDECREF(val);
    if (tb != Py_None)
      Py_XDECREF(tb);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
      return -1;
    kos_dyn_pop_with(ctx, with_node);
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
      return -1;
    (void) out_ev;
    (void) insn;
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
      PyErr_Restore(typ, val, tb);
      return -1;
    }
    kos_dyn_pop_with(ctx, with_node);
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  if (typ != Py_None)
    Py_XDECREF(typ);
  if (val != Py_None)
    Py_XDECREF(val);
  if (tb != Py_None)
    Py_XDECREF(tb);
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
    return -1;
  kos_dyn_pop_with(ctx, with_node);
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
    return -1;

  (void) out_ev;
  (void) insn;
  return 0;
}


// The end.
