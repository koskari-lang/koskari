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
   PUSH_LOOP: no arg. Break target is derived from the extent table
   (pop_pc + 1). Push LOOP region and stack block; continue target =
   next instruction. Initializes region result to None.
*/

KOS_OP_HANDLER(PUSH_LOOP) {
  uint32_t continue_pc = CURRENT_PC(ctx);  /* already advanced by drive loop */

  /* Derive break target from the extent table (pop_pc + 1) */
  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = CURRENT_ACTING_PC(ctx);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_LOOP);
  if (eid == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_LOOP: no LOOP extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_LOOP: invalid extent");
    return -1;
  }
  uint32_t break_pc = ext->pop_pc + 1u;

  KosDynRegion *region = kos_dyn_push_region(ctx, KOS_REGION_LOOP,
                                             break_pc, continue_pc);
  if (! region) {
    PyErr_NoMemory();
    return -1;
  }

  /* Initialize loop result to None */
  Py_INCREF(Py_None);
  KosDynRegion_RESULT(region) = Py_None;

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    kos_dyn_pop_region(ctx, region);
    return -1;
  }
  (void) out_ev;
  return 0;
}
