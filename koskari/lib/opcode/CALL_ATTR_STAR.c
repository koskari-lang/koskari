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
   CALL_ATTR_STAR: arg = extra index. Schema CONST (attr name),
   COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs).
   Stack [obj, pos, kwd] or subsets per flags. Pop per flags, ensure
   pos is tuple, resolve obj.name, prepend obj to args if send_self,
   call via KosContext_CallArgs.
*/


static int ensure_tuple_attr_star(PyObject **val) {
  if (! (val && *val))
    return -1;

  if (! PyTuple_CheckExact(*val)) {
    PyObject *fixed = PySequence_Tuple(*val);
    Py_DECREF(*val);
    *val = fixed;

    if (! fixed)
      return -1;
  }

  return 0;
}


KOS_OP_HANDLER(CALL_ATTR_STAR) {
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return -1;

  PyObject *obj = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  if ((opa & 3) == 3) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    pos = KosStackBlock_PopRef(stack);
    if (! pos) {
      Py_DECREF(kwd);
      return -1;
    }
    obj = KosStackBlock_PopRef(stack);
    if (! obj) {
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return -1;
    }
    if (ensure_tuple_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return -1;
    }
  } else if (opa & 2) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    obj = KosStackBlock_PopRef(stack);
    if (! obj) {
      Py_DECREF(kwd);
      return -1;
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      Py_DECREF(kwd);
      return -1;
    }
  } else if (opa & 1) {
    pos = KosStackBlock_PopRef(stack);
    if (! pos)
      return -1;
    obj = KosStackBlock_PopRef(stack);
    if (! obj) {
      Py_DECREF(pos);
      return -1;
    }
    if (ensure_tuple_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      return -1;
    }
    kwd = NULL;
  } else {
    obj = KosStackBlock_PopRef(stack);
    if (! obj)
      return -1;
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      return -1;
    }
    kwd = NULL;
  }

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    Py_DECREF(pos);
    Py_XDECREF(kwd);
    return -1;
  }

  PyObject *args_for_call = pos;
  PyObject *combined = NULL;

  if (send_self == 0) {
    combined = PyTuple_Pack(1, obj);
    if (! combined) {
      Py_DECREF(obj);
      Py_DECREF(resolved);
      Py_DECREF(pos);
      Py_XDECREF(kwd);
      return -1;
    }
    args_for_call = PySequence_Concat(combined, pos);
    Py_DECREF(combined);
    if (! args_for_call) {
      Py_DECREF(obj);
      Py_DECREF(resolved);
      Py_DECREF(pos);
      Py_XDECREF(kwd);
      return -1;
    }
    Py_DECREF(pos);
  }

  Py_DECREF(obj);

  int res = KosContext_CallArgs(ctx, out_ev, resolved, args_for_call, kwd, 0);

  Py_DECREF(resolved);
  Py_DECREF(args_for_call);
  Py_XDECREF(kwd);
  return res;
}


// The end.
