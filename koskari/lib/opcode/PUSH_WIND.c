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
   PUSH_WIND: arg = COUNT (1 = before only, 2 = both). Stack layout for
   arg=2: [..., before, after] (before below, after above). Pop thunks,
   validate callable or Py_None, push wind node, run wind phase.
*/

KOS_OP_HANDLER(PUSH_WIND) {
  KOS_SYNC_PC(ctx, insn);
  unsigned count = KosOp_ARG(insn);
  PyObject *before = NULL;
  PyObject *after = NULL;  /* set only when count==2 */

  if (count != 1 && count != 2) {
    PyErr_Format(PyExc_ValueError,
                 "PUSH_WIND: COUNT must be 1 or 2, got %u", count);
    RESULT_RAISE();
  }

  if (count == 2) {
    after = KosContext_StackPopRef(ctx);
    if (! after) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_WIND: stack underrun (after)");
      RESULT_RAISE();
    }
  }
  before = KosContext_StackPopRef(ctx);
  if (! before) {
    Py_XDECREF(after);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_WIND: stack underrun (before)");
    RESULT_RAISE();
  }

  if (before != Py_None && ! KosCallable_Check(before)) {
    Py_DECREF(before);
    Py_XDECREF(after);
    PyErr_SetString(PyExc_TypeError, "PUSH_WIND: before thunk must be callable or nil");
    RESULT_RAISE();
  }
  if (after && after != Py_None && ! KosCallable_Check(after)) {
    Py_DECREF(before);
    Py_DECREF(after);
    PyErr_SetString(PyExc_TypeError, "PUSH_WIND: after thunk must be callable or nil");
    RESULT_RAISE();
  }

  PyObject *b = (before == Py_None) ? NULL : before;
  PyObject *a = (after == Py_None || ! after) ? NULL : after;
  if (before == Py_None)
    Py_DECREF(before);
  if (after == Py_None || ! after)
    Py_XDECREF(after);

  KosDynWind *wind = kos_dyn_push_wind(ctx, b, a);
  if (! wind) {
    Py_XDECREF(b);
    Py_XDECREF(a);
    RESULT_RAISE();
  }
  if (b)
    Py_DECREF(b);
  if (a)
    Py_DECREF(a);

  KosDynFrame *const caller = KosContext_FRAME(ctx);
  KosOpResult wr = kos_dyn_extent_enter(ctx, (KosDynNode *) wind);
  if (wr.kind == KOS_RES_RAISE)
    RESULT_RAISE();
  /* the before-thunk suspended on an awaitable; the revive settles the
     wind with the awaited value and continues into the body */
  if (wr.kind == KOS_RES_AWAIT)
    return wr;

  /* REPROLOGUE means one of two things. Either the thunk is a Koskari
     function whose frame is now the current frame: continue from ctx->pc
     runs it, and its return settles the wind. Or the call left work for
     the trampoline (a transfer goal pushed by the callee): return so the
     trampoline drains it first. Continuing in that second case would
     run the wind body before the transfer it was meant to make. */
  if (wr.kind == KOS_RES_REPROLOGUE) {
    if (KosContext_FRAME(ctx) != caller && ! KosContext_GOAL(ctx))
      RESULT_CONTINUE(ctx);
    return wr;
  }

  /* NORMAL: no thunk, or it ran in place */
  RESULT_NEXT(ctx);
}


// The end.
