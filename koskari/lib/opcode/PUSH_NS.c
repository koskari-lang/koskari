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
   PUSH_NS: insn.arg = extra row index. Schema: CONST (nspec cpool idx),
   CONST (names cpool idx or None), NOARG, NOARG. Count opa = len(names)
   if names is a tuple, else 0 if names is None.

   Stack: opa == 0: no values. opa > 0: [val0, ..., val_{opa-1}] with TOS =
   last value. NSPEC and NAMES come from the constant pool only.
*/

KOS_OP_HANDLER(PUSH_NS) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosCode *code = CURRENT_CODE(ctx);
  uint16_t eidx = (uint16_t) KosOp_ARG(insn);
  PyObject *nspec = NULL;
  PyObject *names = NULL;
  PyObject *const *args = NULL;
  PyObject *parent = CURRENT_NS(ctx);
  PyObject *new_ns = NULL;
  KosDynNs *ns_node = NULL;
  int opa = 0;

  if (unlikely(eidx >= KosCode_EXTRA_LEN(code))) {
    PyErr_Format(PyExc_IndexError, "PUSH_NS extra index out of bounds: %u",
                 (unsigned) eidx);
    return KOS_OP_POST_ERR();
  }

  {
    KosExtra row = KosCode_GetExtra(code, eidx);
    uint32_t i0 = row.a0;
    uint32_t i1 = row.a1;

    if (unlikely(i0 >= KosCode_CPOOL_LEN(code))) {
      PyErr_Format(PyExc_IndexError, "PUSH_NS nspec constant index out of"
                   " bounds: %u", (unsigned) i0);
      return KOS_OP_POST_ERR();
    }
    if (unlikely(i1 >= KosCode_CPOOL_LEN(code))) {
      PyErr_Format(PyExc_IndexError, "PUSH_NS names constant index out of"
                   " bounds: %u", (unsigned) i1);
      return KOS_OP_POST_ERR();
    }

    nspec = KosCode_GetConstant((PyObject *) code, i0);
    if (unlikely(! nspec)) {
      return KOS_OP_POST_ERR();
    }
    Py_INCREF(nspec);

    names = KosCode_GetConstant((PyObject *) code, i1);
    if (unlikely(! names)) {
      Py_DECREF(nspec);
      return KOS_OP_POST_ERR();
    }
  }

  if (unlikely(! KosNamespaceSpec_CheckExact(nspec))) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS spec must be a NamespaceSpec, not %R", nspec);
    Py_DECREF(nspec);
    return KOS_OP_POST_ERR();
  }

  if (unlikely(names == Py_None)) {
    opa = 0;
    names = NULL;

  } else {
    if (unlikely(! PyTuple_CheckExact(names))) {
      PyErr_Format(PyExc_RuntimeError,
                   "PUSH_NS names must be tuple or None, not %R", names);
      Py_DECREF(nspec);
      return KOS_OP_POST_ERR();
    }
    opa = (int) PyTuple_GET_SIZE(names);
  }

  if (opa > 0) {
    args = KosEvalState_StackTailPtr(es, (size_t) opa);
    if (unlikely(! args)) {
      Py_DECREF(nspec);
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return KOS_OP_POST_ERR();
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
  if (unlikely(! new_ns)) {
    return KOS_OP_POST_ERR();
  }

  if (opa > 0) {
    for (int i = 0; i < opa; i++) {
      PyObject *name = PyTuple_GET_ITEM(names, i);
      PyObject *val = args[i];
      if (unlikely(KosNamespace_Store(new_ns, name, val) < 0)) {
        Py_DECREF(new_ns);
        return KOS_OP_POST_ERR();
      }
    }
    KosEvalState_StackDropNoErr(es, (size_t) opa);
  }

  Py_INCREF(parent);
  ns_node = kos_dyn_push_ns(ctx, parent);
  if (unlikely(! ns_node)) {
    Py_DECREF(parent);
    Py_DECREF(new_ns);
    return KOS_OP_POST_ERR();
  }

  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = new_ns;

  (void) out_ev;
  return KOS_OP_RUN();
}


// The end.
