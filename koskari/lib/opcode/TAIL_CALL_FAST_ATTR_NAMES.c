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
   TAIL_CALL_FAST_ATTR_NAMES: arg = extra index. Schema FASTVAR (slot),
   CONST (attr), CONST (kw names tuple), COUNT (npos). Stack [pos_0, ...,
   pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. Obj from fast slot; resolve
   obj.name, call. Tail call: do not push a new frame; remove or recycle
   the current one.
*/

KOS_OP_HANDLER_COLD(TAIL_CALL_FAST_ATTR_NAMES) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  uint32_t names_cpool_idx = row.a2;
  unsigned opa = row.a3;

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *obj = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! obj)
    return EVAL_RAISE();

  PyObject *name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! name) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  PyObject *names = KosEvalState_CONST(es, names_cpool_idx);
  if (! names) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "TAIL_CALL_FAST_ATTR_NAMES names must"
                 " be tuple, not %R", names);
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  size_t ntotal = (size_t) (npos + nkwds);

  PyObject *const *argv = NULL;
  if (ntotal) {
    argv = KosEvalState_StackTailPtr(es, ntotal);
    if (! argv) {
      Py_DECREF(obj);
      return EVAL_RAISE();
    }
  }

  PyObject *resolved;
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *cache = (cache_ix == KOS_CACHE_UNSET)
      ? NULL : &KosCode_CACHE(code)[cache_ix];
  int send_self = kos_resolve_named_attr(obj, name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  /* Prologue only: kos_call_invoke owns the window drop and the frame
     handoff; an unbound resolution injects obj as self via the core's
     prepend. */
  KosOpResult r = kos_call_invoke(es, resolved,
                                  (send_self == 0) ? obj : NULL,
                                  argv, npos, nkwds ? names : NULL,
                                  KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                                  | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  return r;
}


// The end.
