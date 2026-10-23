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
  VAR_NEXT: fused VAR + OBJECT_NEXT. Arg = extra index. Schema VAR
  (iterator name), COUNT (n = 1 or 2). Stack: [] or [default] per n.
  Resolve the iterator name and make the next request (kos_next_step)
  with the request's resume; the default stays on the stack under the
  halted instruction and is the result at the end. Auto-await.
*/
KOS_OP_HANDLER(VAR_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  uint16_t n = (uint16_t) KosExtra_IMM(row, 1);
  PyObject *iter;

  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "VAR_NEXT arg must be 1 or 2, got %u", n);
    RESULT_RAISE();
  }

  iter = frame_ns_deref(ctx, KosExtra_OBJ(row, 0));
  if (unlikely(! iter))
    RESULT_RAISE();

  RESULT_ENTRY(ctx, kos_next_step(ctx, (KosOp *) insn, iter),
               &kos_next_resume_kind, insn);
}


// The end.
