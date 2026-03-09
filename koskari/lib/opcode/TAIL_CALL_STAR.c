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
   TAIL_CALL_STAR: arg = bitflags (1 = has *args, 2 = has **kwargs).
   Same stack layout as CALL_STAR: [callable, pos, kwd]. Tail call: do not
   push a new frame; remove or recycle the current one.

   Both paths unify on frame handling (matching TAIL_CALL / TAIL_CALL_NAMES):
   - KosCallable: kos_callable_tail_call recycles frame.
   - Plain Python: kos_dyn_pop_frame when parent exists, then call, push.
*/


static int ensure_tuple_star(PyObject **val) {
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


KOS_OP_HANDLER(TAIL_CALL_STAR) {
  unsigned opa = KosOp_ARG(insn);

  PyObject *fun = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  if ((opa & 3) == 3) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    pos = KosStackBlock_PopRef(stack);
    if (! pos) {
      Py_DECREF(kwd);
      return -1;
    }
    fun = KosStackBlock_PopRef(stack);
    if (! fun) {
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return -1;
    }
    if (ensure_tuple_star(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return -1;
    }
  } else if (opa & 2) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    fun = KosStackBlock_PopRef(stack);
    if (! fun) {
      Py_DECREF(kwd);
      return -1;
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      Py_DECREF(kwd);
      return -1;
    }
  } else if (opa & 1) {
    pos = KosStackBlock_PopRef(stack);
    if (! pos)
      return -1;
    fun = KosStackBlock_PopRef(stack);
    if (! fun) {
      Py_DECREF(pos);
      return -1;
    }
    if (ensure_tuple_star(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      return -1;
    }
    kwd = NULL;
  } else {
    fun = KosStackBlock_PopRef(stack);
    if (! fun)
      return -1;
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      return -1;
    }
    kwd = NULL;
  }

  int res = 0;
  if (KosCallable_Check(fun)) {
    /* KosCallable: tail call recycles frame. */
    res = kos_callable_tail_call(ctx, out_ev, fun, pos, kwd);

  } else {
    /* Plain Python: drop frame when parent exists, call, push. */
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame_top);
    if (parent && kos_dyn_pop_frame(ctx) < 0) {
      res = -1;

    } else {
      PyObject *result = (kwd || PyTuple_GET_SIZE(pos))
        ? PyObject_Call(fun, pos, kwd)
        : PyObject_CallNoArgs(fun);

      if (! result) {
        res = -1;

      } else {
        res = kos_push_or_await(ctx, out_ev, result);
        if (res == 0)
          res = KOS_OP_RES_CONTROL_RETURNED;
      }
    }
  }

  Py_DECREF(fun);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return res;
}
