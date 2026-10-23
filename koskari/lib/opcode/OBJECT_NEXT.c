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
   OBJECT_NEXT: arg = 1 or 2. Stack: [iter] or [iter, default]. One
   request for the next element of iter (kos_next_step), the element
   pushed by the request's resume, which runs at once when the element
   is in hand and when it arrives otherwise. The operands stay on the
   stack under the halted instruction, the iterator on top and the
   default under it: the resume takes the iterator and the default is
   the result at the end; the one-argument form raises StopIteration
   there. Auto-await.
*/
KOS_OP_HANDLER(OBJECT_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  uint16_t n = KosOp_ARG(insn);
  PyObject *iter;

  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "OBJECT_NEXT arg must be 1 or 2, got %u", n);
    RESULT_RAISE();
  }

  if (n == 2) {
    /* [iter, default] -> [default, iter] */
    PyObject *default_val = KosContext_StackPopRef(ctx);
    if (unlikely(! default_val))
      RESULT_RAISE();
    iter = KosContext_StackPopRef(ctx);
    if (unlikely(! iter)) {
      Py_DECREF(default_val);
      RESULT_RAISE();
    }
    if (unlikely(KosContext_StackPUSH(ctx, default_val) < 0)) {
      Py_DECREF(iter);
      RESULT_RAISE();
    }
    if (unlikely(KosContext_StackPUSH(ctx, iter) < 0))
      RESULT_RAISE();
  } else {
    iter = KosContext_StackPeek(ctx);
    if (unlikely(! iter))
      RESULT_RAISE();
  }

  RESULT_ENTRY(ctx, kos_next_step(ctx, (KosOp *) insn, iter),
               &kos_next_resume_kind, insn);
}


// The end.
