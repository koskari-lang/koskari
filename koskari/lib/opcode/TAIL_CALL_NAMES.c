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
   TAIL_CALL_NAMES: arg = N (number of positional arguments). Same stack
   layout as CALL_NAMES: [callable, pos_0, ..., pos_{N-1}, kwd_val_0, ...,
   kwd_val_{M-1}, names]. Tail call: do not push a new frame; remove or
   recycle the current one.

   Extracts args via KosStackSteal before vcall; KosStackSteal_Free at end.
   - KosCallable: tail vcall recycles frame.
   - Plain Python: kos_dyn_pop_frame when parent != NULL, call, push.
*/

KOS_OP_HANDLER(TAIL_CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);
  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *names = KosStackBlock_Peek(*stack);
  if (! names)
    return -1;
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "TAIL_CALL_NAMES names must be tuple"
                 ", not %R", names);
    return -1;
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 2);

  KosStackSteal steal;
  if (KosStackSteal_Tail(&steal, ntotal, stack) < 0)
    return -1;

  PyObject *callable = steal.vals[0];
  PyObject *const *args =
    (npos > 0) ? (PyObject *const *)&steal.vals[1] : NULL;
  PyObject *kwnames_arg = nkwds ? steal.vals[total + 1] : NULL;

  int res = 0;
  if (KosCallable_Check(callable)) {
    /* KosCallable: tail vcall recycles frame. */
    res = kos_callable_tail_vcall(ctx, out_ev, callable, args,
                                  npos, kwnames_arg);

  } else {
    /* Plain Python: drop frame when parent exists, call, push. */
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame_top);
    if (parent && kos_dyn_pop_frame(ctx) < 0) {
      res = -1;

    } else {
      PyObject *result =
        PyObject_Vectorcall(callable, args, (size_t) npos, kwnames_arg);
      if (! result) {
        res = -1;
      } else {
        res = kos_push_or_await(ctx, out_ev, result);
        if (res == 0)
          res = KOS_OP_RES_CONTROL_RETURNED;
      }
    }
  }

  KosStackSteal_Free(&steal);
  return res;
}
