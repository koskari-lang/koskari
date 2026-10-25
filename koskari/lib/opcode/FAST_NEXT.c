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
  FAST_NEXT: fused FAST + OBJECT_NEXT. Arg = extra index. Schema
  FASTVAR (iterator slot), COUNT (n = 1 or 2). Stack: [] or [default]
  per n. Load the iterator from its fast slot and make the next request
  (KosContext_NextResume, the arity picking the resume's kind). The
  iterator is the resume's owned state; the default stays on the stack
  under the halted instruction and is the result at the end. A request
  that fails before it defers sets no resume, so the iterator is
  released here. Auto-await.
*/
KOS_OP_HANDLER(FAST_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  uint16_t n = (uint16_t) KosExtra_IMM(row, 1);
  PyObject *ns = CURRENT_NS();
  const KosResumeKind *kind;
  PyObject *iter;
  KosOpResult r;

  KOS_OP_REQUIRE_NS(ns);
  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "FAST_NEXT arg must be 1 or 2, got %u", n);
    RESULT_RAISE();
  }

  iter = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! iter))
    RESULT_RAISE();
  /* the resolved reference is the resume's owned state from here */
  kind = (n == 2) ? &kos_next_default_resume_kind : &kos_next_resume_kind;
  r = KosContext_NextResume(ctx, iter, kind, iter);
  if (unlikely(r.kind == KOS_RES_RAISE))
    Py_DECREF(iter);
  RESULT_ENTRY(ctx, r, kind, iter);
}


// The end.
