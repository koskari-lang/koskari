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
   Shared step core for OBJECT_NEXT and its fused VAR_NEXT / FAST_NEXT
   variants: one step of either iteration protocol. Consumes iter and
   default_val (may be NULL when n == 1). If Koskari async gen we attach
   its frame; else async or sync python iteration. Pushes value or
   default on exhaustion; 1-arg lets StopIteration raise. Auto-await.
*/

static KosOpResult object_next_step(
    KosEvalState *es,
    PyObject *iter,
    PyObject *default_val,
    uint16_t n) {

  /* KosIter provider: flatten into the same eval loop via its step
     slot, with the NEXT boundary node armed for delivery. */
  KosIterSlots *islots;
  if (KosIter_CheckSlots(iter, &islots)) {
    PyObject *out = NULL;

    if (kos_dyn_push_next_boundary(es, iter, default_val, (int) n) < 0) {
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return EVAL_RAISE();
    }

    switch (islots->ki_step(es, iter, NULL, &out)) {
      case KOS_ITER_INSTALLED:
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return EVAL_NORMAL();

      case KOS_ITER_VALUE:
        kos_dyn_next_abort_after_failed_attach(es);
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        if (KosEvalState_StackPUSH(es, out) < 0)
          return EVAL_RAISE();
        return EVAL_NORMAL();

      case KOS_ITER_EXHAUSTED:
        kos_dyn_next_abort_after_failed_attach(es);
        Py_XDECREF(out);
        Py_DECREF(iter);
        if (n == 2 && default_val) {
          if (KosEvalState_StackPushRef(es, default_val) < 0) {
            Py_XDECREF(default_val);
            return EVAL_RAISE();
          }
          Py_XDECREF(default_val);
          return EVAL_NORMAL();
        }
        Py_XDECREF(default_val);
        PyErr_SetNone(PyExc_StopIteration);
        return EVAL_RAISE();

      case KOS_ITER_AWAIT:
        /* Boundary stays armed; the awaited value revives through it. */
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return EVAL_AWAIT(out);

      case KOS_ITER_ERROR:
      default:
        kos_dyn_next_abort_after_failed_attach(es);
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return EVAL_RAISE();
    }
  }

  PyObject *value = NULL;
  PyObject *_await = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(es, &_await, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosEvalState_StackPushRef(es, value) < 0) {
        Py_DECREF(value);
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return EVAL_RAISE();
      }
      Py_DECREF(value);
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return EVAL_NORMAL();

    case GET_NEXT_EXHAUSTED:
      Py_DECREF(iter);
      if (default_val) {
        if (KosEvalState_StackPushRef(es, default_val) < 0) {
          Py_XDECREF(default_val);
          return EVAL_RAISE();
        }
        Py_XDECREF(default_val);
        return EVAL_NORMAL();
      }
      Py_XDECREF(default_val);
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();

    case GET_NEXT_SUSPEND:
      /* The step is parked on its await: arm the NEXT boundary node to
         carry the arity contract across the suspension. The awaited
         value revives through the boundary (revive_await election);
         StopAsyncIteration from the parked step converts to exhaustion
         at the raise walk. Nothing rides the operand stack. */
      if (kos_dyn_push_next_boundary(es, iter, default_val, (int) n) < 0) {
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        Py_XDECREF(_await);
        return EVAL_RAISE();
      }
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return EVAL_AWAIT(_await);

    case GET_NEXT_ERROR:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return EVAL_RAISE();

    default:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return EVAL_RAISE();
  }
}


/**
   OBJECT_NEXT: arg = 1 or 2. Stack: [iter] or [iter, default]; pops
   its operands and runs the shared step core.
*/

KOS_OP_HANDLER_COLD(OBJECT_NEXT) {
  uint16_t n = KosOp_ARG(insn);
  if (n != 1 && n != 2) {
    PyErr_Format(PyExc_ValueError, "OBJECT_NEXT arg must be 1 or 2, got %u", n);
    return EVAL_RAISE();
  }

  PyObject *default_val = NULL;
  if (n == 2) {
    default_val = KosEvalState_StackPopRef(es);
    if (! default_val)
      return EVAL_RAISE();
  }
  PyObject *iter = KosEvalState_StackPopRef(es);
  if (! iter) {
    Py_XDECREF(default_val);
    return EVAL_RAISE();
  }

  return object_next_step(es, iter, default_val, n);
}


// The end.
