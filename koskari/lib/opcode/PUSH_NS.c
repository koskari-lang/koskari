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
   PUSH_NS: arg = count (opa) of values to bind. Stack layout:
   opa == 0: [NSPEC]
   opa > 0:  [NSPEC, val0, val1, ..., val(N-1), NAMES]
   NSPEC from constant pool; NAMES = tuple of names. Creates child namespace
   from NSPEC, optionally binds values to names, pushes KOS_DYN_NS.
*/

KOS_OP_HANDLER(PUSH_NS) {
  int opa = (int) KosOp_ARG(insn);
  PyObject *nspec = NULL;
  PyObject *names = NULL;
  PyObject *const *args = NULL;
  PyObject *parent = CURRENT_NS(ctx);
  PyObject *new_ns = NULL;
  KosDynNs *ns_node = NULL;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS called with negative count: %i", opa);
    return -1;
  }

  if (opa == 0) {
    nspec = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! nspec) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
  } else {
    names = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! names) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
    if (! PyTuple_CheckExact(names)) {
      Py_DECREF(names);
      PyErr_Format(PyExc_RuntimeError,
                   "PUSH_NS names must be tuple, not %R", names);
      return -1;
    }
    args = KosStackBlock_TailPtr(CURRENT_STACK(ctx), (size_t) opa + 1);
    if (! args) {
      Py_DECREF(names);
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
    nspec = args[0];
    Py_INCREF(nspec);
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    Py_DECREF(nspec);
    Py_XDECREF(names);
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS spec must be a NamespaceSpec, not %R", nspec);
    return -1;
  }

  {
    int i = names ? (int) PyTuple_GET_SIZE(names) : 0;
    if (i != opa) {
      Py_DECREF(nspec);
      Py_XDECREF(names);
      PyErr_Format(PyExc_RuntimeError,
                   "PUSH_NS names length (%i) must match value count (%i)",
                   i, opa);
      return -1;
    }
  }

  /*
   * Use parent only if it is a KosNamespace (supports closure lookup).
   * When frame has a dict (e.g. shift handler), use NULL so nspec
   * must have empty closure_syms.
   */
  PyObject *spec_parent = KosNamespace_Check(parent) ? parent : NULL;
  new_ns = KosNamespace_FromSpec(nspec, spec_parent);
  Py_DECREF(nspec);
  if (! new_ns) {
    Py_XDECREF(names);
    return -1;
  }

  if (opa > 0) {
    for (int i = 0; i < opa; i++) {
      PyObject *name = PyTuple_GET_ITEM(names, i);
      PyObject *val = args[i + 1];
      if (KosNamespace_Store(new_ns, name, val) < 0) {
        Py_DECREF(names);
        Py_DECREF(new_ns);
        return -1;
      }
    }
    KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa + 1);
    Py_DECREF(names);
  }

  Py_INCREF(parent);
  ns_node = kos_dyn_push_ns(ctx, parent);
  if (! ns_node) {
    Py_DECREF(parent);
    Py_DECREF(new_ns);
    return -1;
  }

  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = new_ns;

  (void) out_ev;
  return 0;
}
