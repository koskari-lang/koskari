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
   TAIL_CALL: arg = N. Same stack as CALL [callable, arg_0, ..., arg_{N-1}].
   Tail call: do not push a new frame; remove or recycle the current one.

   Both paths unify on frame handling:
   - KosCallable: kos_callable_tail_vcall recycles the frame (reset for tail).
   - Plain Python: kos_dyn_pop_frame removes the frame when we have a parent;
     at root (no parent) we keep the frame.

   Extracts args via PopTail/PopTailPtr before vcall; opcode owns and DECREFs
   the extracted storage. Returns KOS_OP_RES_CONTROL_RETURNED when we push
   and continue (triggers kos_dynnode_on_control_returned for wind thunks).
*/

#define TAIL_CALL_ARRAY_THRESHOLD 16

KOS_OP_HANDLER(TAIL_CALL) {
  unsigned n = KosOp_ARG(insn);
  size_t count = (size_t)(n + 1);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *callable;
  PyObject *const *args;
  PyObject *tup = NULL;
  PyObject *buf[TAIL_CALL_ARRAY_THRESHOLD];
  int used_tuple = 0;

  if (count > TAIL_CALL_ARRAY_THRESHOLD) {
    tup = KosStackBlock_PopTail(stack, count);
    if (! tup)
      return -1;
    used_tuple = 1;
    callable = PyTuple_GET_ITEM(tup, 0);
    args = (n > 0) ? (PyObject *const *)&PyTuple_GET_ITEM(tup, 1) : NULL;

  } else {
    if (KosStackBlock_PopTailPtr(stack, count, buf) < 0)
      return -1;
    callable = buf[0];
    args = (n > 0) ? (PyObject *const *)&buf[1] : NULL;
  }

  int res = 0;
  if (KosCallable_Check(callable)) {
    /* KosCallable: tail vcall recycles the frame (no explicit pop). */
    res = kos_callable_tail_vcall(ctx, out_ev, callable, args,
                                  (Py_ssize_t) n, NULL);

  } else {
    /* Plain Python: drop frame when we have a parent (tail call semantics). */
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame_top);
    if (parent && kos_dyn_pop_frame(ctx) < 0) {
      res = -1;

    } else {
      PyObject *result = PyObject_Vectorcall(callable, args, (size_t) n, NULL);
      if (! result)
        res = -1;
      else {
        res = kos_push_or_await(ctx, out_ev, result);
        if (res == 0)
          res = KOS_OP_RES_CONTROL_RETURNED;
      }
    }
  }

  if (used_tuple) {
    Py_DECREF(tup);
  } else {
    while (count--)
      Py_DECREF(buf[count]);
  }

  return res;
}
