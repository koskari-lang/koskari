/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

/**
   CALL_FAST_ATTR_NAMES: arg = extra index. Schema FASTVAR (slot), CONST (attr),
   CONST (kw names tuple), COUNT (npos). Stack [pos_0, ..., pos_{N-1},
   kwd_val_0, ..., kwd_val_{M-1}]. Obj from fast slot; resolve obj.name, call.
*/

KOS_OP_HANDLER(CALL_FAST_ATTR_NAMES) {
  KosContext *ctx = KosEvalState_Context(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  uint32_t names_cpool_idx = row.a2;
  unsigned opa = row.a3;

  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "CALL_FAST_ATTR_NAMES on NULL namespace");
    return KOS_OP_POST_ERR();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_FAST_ATTR_NAMES on %R", ns);
    return KOS_OP_POST_ERR();
  }

  PyObject *obj = KosNamespace_FastDeref(ns, (size_t) slot_idx);
  if (! obj)
    return KOS_OP_POST_ERR();

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return KOS_OP_POST_ERR();

  PyObject *names = KosCode_GetConstant((PyObject *) code, names_cpool_idx);
  if (! names)
    return KOS_OP_POST_ERR();

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_FAST_ATTR_NAMES names must be tuple"
                 ", not %R", names);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  if (npos + nkwds > 0 &&
      KosEvalState_StackTailPtr(es, (size_t)(npos + nkwds)) == NULL)
    return KOS_OP_POST_ERR();

  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t) total;

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0)
    return KOS_OP_POST_ERR();

  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    int steal_ok;
    if (send_self == 0) {
      steal_ok = KosStackSteal_TailPrepend(&steal, obj, ntotal, (ctx ? KosContext_BLOCK(ctx) : NULL));
    } else {
      steal_ok = KosStackSteal_Tail(&steal, ntotal, (ctx ? KosContext_BLOCK(ctx) : NULL));
    }
    if (steal_ok < 0) {
      Py_DECREF(resolved);
      return KOS_OP_POST_ERR();
    }
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = steal.vals;
      nargs = npos + 1;
    } else {
      args = (total > 0) ? steal.vals : NULL;
      nargs = npos;
    }
    int r = KosContext_Call(ctx, out_ev, resolved, args, nargs, names, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  KosStackSteal steal = { 0 };
  int steal_ok;
  if (send_self == 0) {
    steal_ok = KosStackSteal_TailPrepend(&steal, obj, ntotal, (ctx ? KosContext_BLOCK(ctx) : NULL));
  } else {
    steal_ok = KosStackSteal_Tail(&steal, ntotal, (ctx ? KosContext_BLOCK(ctx) : NULL));
  }
  if (steal_ok < 0) {
    Py_DECREF(resolved);
    return KOS_OP_POST_ERR();
  }
  PyObject *result;
  if (send_self == 0) {
    result = PyObject_Vectorcall(resolved, steal.vals,
                                 (size_t)(npos + 1), names);
  } else {
    result = PyObject_Vectorcall(resolved,
        (total > 0) ? steal.vals : NULL, (size_t) npos, names);
  }
  KosStackSteal_Free(&steal);
  Py_DECREF(resolved);
  if (! result)
    return KOS_OP_POST_ERR();
  return kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result));
}
