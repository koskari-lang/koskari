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
   request for the next element of iter (KosContext_NextResume, the
   arity picking the resume's kind), the element pushed by the
   request's resume, which runs at once when the element is in hand
   and when it arrives otherwise. The iterator comes off the stack and
   is the resume's owned state; the default stays on the stack under
   the halted instruction and is the result at the end, and the
   one-argument form raises StopIteration there. A request that fails
   before it defers sets no resume, so the iterator is released here.
   Auto-await.
*/
KOS_OP_HANDLER(OBJECT_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  uint16_t n = KosOp_ARG(insn);
  const KosResumeKind *kind;
  PyObject *iter;
  KosOpResult r;

  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "OBJECT_NEXT arg must be 1 or 2, got %u", n);
    RESULT_RAISE();
  }

  if (n == 2) {
    /* [iter, default] -> [default]: the iterator comes out from under
       the default */
    PyObject *dflt = KosContext_StackPopRef(ctx);
    if (unlikely(! dflt))
      RESULT_RAISE();
    iter = KosContext_StackPopRef(ctx);
    if (unlikely(! iter)) {
      Py_DECREF(dflt);
      RESULT_RAISE();
    }
    if (unlikely(KosContext_StackPUSH(ctx, dflt) < 0)) {
      Py_DECREF(iter);
      RESULT_RAISE();
    }
  } else {
    iter = KosContext_StackPopRef(ctx);
    if (unlikely(! iter))
      RESULT_RAISE();
  }

  kind = (n == 2) ? &kos_next_default_resume_kind : &kos_next_resume_kind;
  r = KosContext_NextResume(ctx, iter, kind, iter);
  if (unlikely(r.kind == KOS_RES_RAISE))
    Py_DECREF(iter);
  RESULT_ENTRY(ctx, r, kind, iter);
}


// The end.
