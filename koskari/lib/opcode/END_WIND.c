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
   END_WIND: arg unused. End wind scope (terminates before/after hook block).
   Calls kos_end_hook; if unwind was in progress, resumes unwinding and
   delivers any pending return value.
*/

KOS_OP_HANDLER(END_WIND) {
  if (kos_end_hook(ctx) < 0)
    return -1;

  while (KosContext_UNWIND_TARGET_FRAME(ctx)) {
    KosDynFrame *target = KosContext_UNWIND_TARGET_FRAME(ctx);
    int unw = kos_dyn_unwind_to_frame(ctx, target, out_ev);
    if (unw == KOS_DYN_UNWIND_ASYNC_STARTED)
      return 0;
    KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
    if (unw < 0)
      return -1;
    if (KosContext_PENDING_RETURN_VALUE(ctx)) {
      PyObject *val = KosContext_PENDING_RETURN_VALUE(ctx);
      KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
      if (KosContext_PENDING_RETURN_TO_PARENT(ctx)) {
        KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;
        if (kos_dyn_detach_top_frame(ctx, NULL) < 0) {
          Py_DECREF(val);
          return -1;
        }
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0) {
          Py_DECREF(val);
          return -1;
        }
        Py_DECREF(val);
      } else {
        KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;
        KosEvent_KIND(out_ev) = KOS_EV_RETURN;
        KosEvent_PAYLOAD(out_ev) = val;
        KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
        KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
        return 1;
      }
    }
    break;
  }
  (void) insn;
  return 0;
}
