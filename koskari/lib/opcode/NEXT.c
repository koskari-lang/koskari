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
   NEXT: arg = 1 or 2 (iterator [and optional default]). If Koskari async
   gen we attach its frame; else we use get_await_next (async) or sync
   iteration. Pushes value or default on exhaustion; 1-arg lets
   StopIteration raise. Auto-await.
*/

KOS_OP_HANDLER(NEXT) {
  uint16_t n = KosOp_ARG(insn);
  if (n != 1 && n != 2) {
    PyErr_Format(PyExc_ValueError, "NEXT arg must be 1 or 2, got %u", n);
    return KOS_OP_POST_ERR();
  }

  PyObject *default_val = NULL;
  PyObject *iter = NULL;
  if (n == 2) {
    default_val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! default_val)
      return KOS_OP_POST_ERR();
  }
  iter = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! iter) {
    Py_XDECREF(default_val);
    return KOS_OP_POST_ERR();
  }

  /* If not already an iterator, get one (NEXT allows iterables). */
  /* this was a stupid idea, but I'm leaving it here for posterity */
  // if (! KosPyAsyncGen_Check(iter)) {
  //   PyObject *awaitable = get_await_next(iter);
  //   if (awaitable) {
  //     Py_DECREF(awaitable);
  //   } else if (! PyIter_Check(iter)) {
  //     PyObject *it = PyObject_GetIter(iter);
  //     Py_DECREF(iter);
  //     if (! it) {
  //       Py_XDECREF(default_val);
  //       return KOS_OP_POST_ERR();
  //     }
  //     iter = it;
  //   }
  // }

  /* Koskari async gen: flatten into same eval loop (attach gen frame, return 0). */
  if (KosPyAsyncGen_Check(iter)) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) iter;
    if (agen->in_progress) {
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator already being iterated");
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return KOS_OP_POST_ERR();
    }
    if (agen->closed || ! agen->ctx) {
      Py_DECREF(iter);
      if (n == 2 && default_val) {
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), default_val) < 0) {
          Py_XDECREF(default_val);
          return KOS_OP_POST_ERR();
        }
        Py_XDECREF(default_val);
        return KOS_OP_RUN();
      }
      Py_XDECREF(default_val);
      PyErr_SetNone(PyExc_StopIteration);
      return KOS_OP_POST_ERR();
    }
    agen->in_progress = 1;
    KosContext_PENDING_AGEN(ctx) = (void *) agen;
    if (default_val) {
      Py_INCREF(default_val);
      KosContext_PENDING_AGEN_DEFAULT(ctx) = default_val;
    } else {
      KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    }
    KosContext_PENDING_AGEN_N(ctx) = (int) n;
    if (op_attach_koskari_agen_frame(ctx, agen, iter, default_val) < 0)
      return KOS_OP_POST_ERR();
    Py_DECREF(iter);
    Py_XDECREF(default_val);
    return KOS_OP_RUN();
  }

  PyObject *value = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(ctx, out_ev, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), value) < 0) {
        Py_DECREF(value);
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return KOS_OP_POST_ERR();
      }
      Py_DECREF(value);
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return KOS_OP_RUN();

    case GET_NEXT_EXHAUSTED:
      Py_DECREF(iter);
      if (default_val) {
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), default_val) < 0) {
          Py_XDECREF(default_val);
          return KOS_OP_POST_ERR();
        }
        Py_XDECREF(default_val);
        return KOS_OP_RUN();
      }
      Py_XDECREF(default_val);
      PyErr_SetNone(PyExc_StopIteration);
      return KOS_OP_POST_ERR();

    case GET_NEXT_SUSPEND:
      if (n == 2) {
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), default_val) < 0) {
          Py_DECREF(default_val);
          Py_DECREF(iter);
          return KOS_OP_POST_ERR();
        }
      }
      if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), iter) < 0) {
        if (n == 2) {
          PyObject *rollback_default = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
          Py_XDECREF(rollback_default);
          Py_DECREF(default_val);
        }
        Py_DECREF(iter);
        return KOS_OP_POST_ERR();
      }
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return KOS_OP_POST_SUSPEND();

    case GET_NEXT_ERROR:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return KOS_OP_POST_ERR();

    default:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return KOS_OP_POST_ERR();
  }
}
