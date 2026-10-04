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
   CALL_VAR_ATTR_NAMES and TAIL_CALL_VAR_ATTR_NAMES: arg = extra index. Schema
   VAR (name), CONST (attr), CONST (kw names tuple), COUNT (npos). Stack
   [pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. Obj from namespace
   by name; resolve obj.name, call. A TAIL_CALL instruction reaches this same
   handler and adds the tail bit, which the core turns into a frame reset
   instead of a push.
*/

KOS_OP_HANDLER(CALL_VAR_ATTR_NAMES) {
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  unsigned opa = KosExtra_IMM(row, 3);

  // borrow reference
  PyObject *name = KosExtra_OBJ(row, 0);

  // owned reference
  PyObject *obj = frame_ns_deref(es, name);
  if (! obj)
    RESULT_RAISE();

  // borrowed reference
  PyObject *attr_name = KosExtra_OBJ(row, 1);

  // borrowed reference
  PyObject *names = KosExtra_OBJ(row, 2);

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "%s names must be tuple, not %R",
                 kos_opcode_name(KosOp_OP(insn)), names);
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  PyObject *const *ptr = NULL;
  if (npos + nkwds > 0) {
    ptr = KosEvalState_StackTailPtr(es, (size_t)(npos + nkwds));
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
  KosOpResult r = kos_call_invoke(es, resolved,
                                  (send_self == 0) ? obj : NULL,
                                  ptr, npos, nkwds ? names : NULL,
                                  KOS_CALL_ARGV_WINDOW | tail
                                  | KOS_CALL_CALLABLE_OWNED);
  Py_DECREF(obj);
  RESULT_RELAY(es, r);
}


// The end.
