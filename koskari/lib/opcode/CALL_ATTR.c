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
   CALL_ATTR and TAIL_CALL_ATTR: arg = extra index. Schema CONST (attr name),
   COUNT (arg count). Stack [obj, arg_0, ..., arg_{N-1}]. Resolve obj.name,
   then call. Unbound fn: fn(obj, *args). Bound/other: callable(*args). A
   TAIL_CALL instruction reaches this same handler and adds the tail bit, which
   the core turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_ATTR) {
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  unsigned n = KosExtra_IMM(row, 1);
  unsigned count = n + 1;

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
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
    r = kos_call_invoke(es, resolved, NULL, ptr, (Py_ssize_t) n + 1,
                        NULL,
                        KOS_CALL_ARGV_WINDOW | tail | KOS_CALL_CALLABLE_OWNED);
  } else {
    r = kos_call_invoke(es, resolved, NULL, (n > 0) ? &ptr[1] : NULL,
                        (Py_ssize_t) n, NULL,
                        KOS_CALL_ARGV_WINDOW | tail | KOS_CALL_WINDOW_UNDER
                        | KOS_CALL_CALLABLE_OWNED);
  }
  RESULT_RELAY(es, r);
}


// The end.
