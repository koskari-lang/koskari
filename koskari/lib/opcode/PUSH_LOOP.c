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
  uint32_t continue_pc = KosEvalState_PC(es);  /* next insn; fetch advanced */

  /* Derive break target from the extent table (pop_pc + 1) */
  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t pc = KosEvalState_ACTING_PC(es);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_LOOP);
  if (eid == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_LOOP: no LOOP extent at this PC");
    return EVAL_RAISE();
  }
  KosExtentDesc *const ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_LOOP: invalid extent");
    return EVAL_RAISE();
  }
  uint32_t break_pc = ext->pop_pc + 1u;

  KosDynNode *region = (KosDynNode *) kos_dyn_push_loop(es,                                              break_pc, continue_pc);
  if (! region) {
    PyErr_NoMemory();
    return EVAL_RAISE();
  }

  /* Initialize loop result to None */
  Py_INCREF(Py_None);
  KosDynLoop_RESULT((KosDynLoop *) region) = Py_None;
  return EVAL_NORMAL();
}


// The end.
