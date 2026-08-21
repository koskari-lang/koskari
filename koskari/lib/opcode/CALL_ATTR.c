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
   CALL_ATTR: arg = extra index. Schema CONST (attr name), COUNT (arg count).
   Stack [obj, arg_0, ..., arg_{N-1}]. Resolve obj.name, then call.
   Unbound fn: fn(obj, *args). Bound/other: callable(*args).
*/
KOS_OP_HANDLER_COLD(CALL_ATTR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  unsigned n = row.a1;
  unsigned count = n + 1;

  PyObject *name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! name)
    return EVAL_RAISE();

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (! ptr)
    return EVAL_RAISE();

  PyObject *obj = ptr[0];

  PyObject *resolved;
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *cache = (cache_ix == KOS_CACHE_UNSET)
      ? NULL : &KosCode_CACHE(code)[cache_ix];
  int send_self = kos_resolve_named_attr(obj, name, cache, &resolved);
  if (send_self < 0)
    return EVAL_RAISE();

  /* Unbound: obj is already argv[0] in the window.  Bound: obj is the
     under-slot, dropped with the window but not passed. */
  KosOpResult r;
  if (send_self == 0) {
    r = kos_call_invoke(es, resolved, NULL, ptr, (Py_ssize_t) n + 1,
                        NULL,
                        KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
  } else {
    r = kos_call_invoke(es, resolved, NULL, (n > 0) ? &ptr[1] : NULL,
                        (Py_ssize_t) n, NULL,
                        KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER
                        | KOS_CALL_CALLABLE_OWNED);
  }
  return r;
}


// The end.
