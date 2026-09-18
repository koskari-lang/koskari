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
   CALL_ATTR_NAMES and TAIL_CALL_ATTR_NAMES: arg = extra index. Schema CONST
   (attr name), CONST (kwarg names tuple), COUNT (npos). Stack [obj, pos_0,
   ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. Resolve obj.name, then call
   like CALL_NAMES. A TAIL_CALL instruction reaches this same handler and adds
   the tail bit, which the core turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_ATTR_NAMES) {
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
      unsigned opa = KosExtra_IMM(row, 2);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *names = KosExtra_OBJ(row, 1);

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "%s names must be tuple, not %R",
                 kos_opcode_name(KosOp_OP(insn)), names);
    RESULT_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 1);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, ntotal);
  if (! ptr)
    RESULT_RAISE();

  PyObject *obj = ptr[0];

  PyObject *resolved;
  KosCacheSlot *cache = KosOp_SLOT(insn);
  int send_self = kos_resolve_named_attr(obj, name, cache, &resolved);
  if (send_self < 0)
    RESULT_RAISE();

  /* Unbound: obj is already argv[0] in the window.  Bound: obj is the
     under-slot, dropped with the window but not passed. */
  KosOpResult r;
  if (send_self == 0) {
    r = kos_call_invoke(es, resolved, NULL, ptr, npos + 1,
                        nkwds ? names : NULL,
                        KOS_CALL_ARGV_WINDOW | tail | KOS_CALL_CALLABLE_OWNED);
  } else {
    r = kos_call_invoke(es, resolved, NULL,
                        (total > 0) ? &ptr[1] : NULL, npos,
                        nkwds ? names : NULL,
                        KOS_CALL_ARGV_WINDOW | tail | KOS_CALL_WINDOW_UNDER
                        | KOS_CALL_CALLABLE_OWNED);
  }
  RESULT_RELAY(es, r);
}


// The end.
