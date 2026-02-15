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
   INVOKE_K_VAR: arg = constant index (name). Pop value, look up name in
   current namespace, invoke that continuation with value.
*/

KOS_OP_HANDLER(INVOKE_K_VAR) {
  KosCode *code = CURRENT_CODE(ctx);
  PyObject *name = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! name)
    return -1;

  PyObject *value = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! value)
    return -1;

  PyObject *ns = CURRENT_NS(ctx);
  PyObject *cont_obj = NULL;
  if (KosNamespace_Check(ns)) {
    cont_obj = KosNamespace_Deref(ns, name);
  } else if (PyDict_Check(ns)) {
    cont_obj = PyDict_GetItemWithError(ns, name);
    if (cont_obj)
      Py_INCREF(cont_obj);
  } else {
    Py_DECREF(value);
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace or dict, got %R", ns);
    return -1;
  }

  if (! cont_obj) {
    Py_DECREF(value);
    return -1;
  }

  if (! KosPyDelimitedContinuation_Check(cont_obj)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_TypeError,
                    "INVOKE_K_VAR: name is not a DelimitedContinuation");
    return -1;
  }

  KosContinuation *k = ((KosPyDelimitedContinuation *) cont_obj)->captured;

  if (! k) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_RuntimeError,
                    "INVOKE_K_VAR: continuation has no captured state");
    return -1;
  }

  if (k->token_ref != (PyObject *) KosContext_TOKEN(ctx)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K_VAR: continuation does not belong to current"
                    " evaluator");
    return -1;
  }

  if (! kos_find_prompt(ctx, k->prompt_id)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K_VAR: prompt not found in dynamic chain");
    return -1;
  }

  if (kos_invoke_continuation(ctx, k, value, 0, cont_obj) < 0) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }
  Py_DECREF(value);
  /* cont_obj not DECREF'd: invoke transfers control on success */
  (void) out_ev;
  (void) insn;
  return 0;
}
