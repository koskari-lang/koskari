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
   PUSH_CC: no arg. Push a CC block onto the dynamic chain. The resume PC
   (instruction after the matching POP_CC) is taken from the block table
   (extent pop_pc + 1), not from an instruction argument.
*/

KOS_OP_HANDLER(PUSH_CC) {
  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_CC);
  if (eid == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_CC: no CC extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_CC: invalid extent");
    return -1;
  }
  uint32_t resume_pc = ext->pop_pc + 1u;
  if (! kos_dyn_push_cc(ctx, resume_pc)) {
    return -1;
  }
  (void) out_ev;
  return 0;
}
