/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   CALL_ATTR_NAMES: arg = extra index. Schema CONST (attr name),
   CONST (kwarg names tuple), COUNT (npos).
   Stack [obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}].
   Resolve obj.name, then call like CALL_NAMES.
*/
KOS_OP_HANDLER_COLD(CALL_ATTR_NAMES) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  uint32_t names_cpool_idx = row.a1;
  unsigned opa = row.a2;

  PyObject *name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! name)
    return EVAL_RAISE();

  PyObject *names = KosEvalState_CONST(es, names_cpool_idx);
  if (! names)
    return EVAL_RAISE();

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_ATTR_NAMES names must be tuple"
                 ", not %R", names);
    return EVAL_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 1);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, ntotal);
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
    r = kos_call_invoke(es, resolved, NULL, ptr, npos + 1,
                        nkwds ? names : NULL,
                        KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
  } else {
    r = kos_call_invoke(es, resolved, NULL,
                        (total > 0) ? &ptr[1] : NULL, npos,
                        nkwds ? names : NULL,
                        KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER
                        | KOS_CALL_CALLABLE_OWNED);
  }
  return r;
}


// The end.
