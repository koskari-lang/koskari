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
   TAIL_CALL_VAR_ATTR: arg = extra index. Schema VAR (name), CONST (attr
   name), COUNT (arg count). Stack [arg_0, ..., arg_{N-1}] only; obj is
   loaded from the frame namespace by VAR name. Resolve obj.attr, then
   call. Unbound fn: fn(obj, *args). Bound/other: callable(*args). Tail
   call: do not push a new frame; remove or recycle the current one.
*/
KOS_OP_HANDLER_COLD(TAIL_CALL_VAR_ATTR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  unsigned n = row.a2;

  PyObject *var_name = KosEvalState_CONST(es, name_cpool_idx);
  if (! var_name)
    RESULT_RAISE();

  PyObject *attr_name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! attr_name)
    RESULT_RAISE();

  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj)
    RESULT_RAISE();

  PyObject *const *ptr = NULL;
  if (n > 0) {
    ptr = KosEvalState_StackTailPtr(es, (size_t) n);
    if (! ptr) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
  }

  PyObject *resolved;
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *cache = (cache_ix == KOS_CACHE_UNSET)
      ? NULL : &KosCode_CACHE(code)[cache_ix];
  int send_self = kos_resolve_named_attr(obj, attr_name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  /* Prologue only: kos_call_invoke owns the window drop and the frame
     handoff; an unbound resolution injects obj as self via the core's
     prepend. */
  KosOpResult r = kos_call_invoke(es, resolved,
                                  (send_self == 0) ? obj : NULL,
                                  ptr, (Py_ssize_t) n, NULL,
                                  KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                                  | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  RESULT_RELAY(es, r);
}


// The end.
