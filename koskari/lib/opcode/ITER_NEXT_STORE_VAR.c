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
  ITER_NEXT_STORE_VAR: arg = extra index. Schema VAR (name), LABEL (else
  pc). The for-each step with its element store: one request for the
  next element of the FOREACH region's iterator (KOS_ITER_STEP), the
  element stored under the name in the frame namespace by the step's
  resume, which runs at once when the element is in hand and when it
  arrives otherwise. Branch to the else pc at the end of the
  iteration. Must be inside a PUSH_FOREACH block. Auto-await. The
  pre-lowering twin of ITER_NEXT_STORE_FAST, which every site with a
  fast slot takes instead.
*/
KOS_OP_HANDLER(ITER_NEXT_STORE_VAR) {
  KOS_SYNC_PC(ctx, insn);
  KOS_ITER_STEP(ctx, insn, &kos_iter_next_store_var_resume_kind,
                KosExtra_TARGET(KosOp_ROW((KosOp *) insn), 1));
}


// The end.
