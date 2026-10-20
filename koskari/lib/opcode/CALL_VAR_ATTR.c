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
   CALL_VAR_ATTR and TAIL_CALL_VAR_ATTR: arg = extra index. Schema VAR (name),
   CONST (attr name), COUNT (arg count). Stack [arg_0, ..., arg_{N-1}] only;
   obj is loaded from the frame namespace by VAR name. Resolve obj.attr, then
   call. Unbound fn: fn(obj, *args). Bound/other: callable(*args). A TAIL_CALL
   instruction reaches this same handler and adds the tail bit, which the core
   turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_VAR_ATTR) {
  KOS_SYNC_PC(ctx, insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  unsigned n = KosExtra_IMM(row, 2);

  PyObject *var_name = KosExtra_OBJ(row, 0);

  PyObject *attr_name = KosExtra_OBJ(row, 1);

  PyObject *obj = frame_ns_deref(ctx, var_name);
  if (! obj)
    RESULT_RAISE();

  PyObject *const *ptr = NULL;
  if (n > 0) {
    ptr = KosContext_StackTailPtr(ctx, (size_t) n);
    if (! ptr) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
  }

  PyObject *resolved;
  KosCacheSlot *cache = KosOp_SLOT(insn);
  int send_self = kos_resolve_named_attr(obj, attr_name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  /* Prologue only: kos_call_invoke owns the window drop; an unbound
     resolution injects obj as self via the core's prepend. */
  KosOpResult r = kos_call_invoke_from_handler(
      ctx, tail, resolved, (send_self == 0) ? obj : NULL, ptr,
      (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  RESULT_RELAY(ctx, r);
}


// The end.
