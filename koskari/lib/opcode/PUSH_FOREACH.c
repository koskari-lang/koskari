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
   PUSH_FOREACH: no arg. Break target is derived from the extent table
   (pop_pc + 1), not from the instruction argument. Stack: [iterable].
   Pops iterable, creates iterator, stores it in region (result defaults
   to NULL), pushes FOREACH region and stack block. Continue target =
   next insn.
*/

KOS_OP_HANDLER(PUSH_FOREACH) {
  uint32_t continue_pc = KosEvalState_PC(es);  /* next insn; fetch advanced */

  PyObject *iterable = KosEvalState_StackPopRef(es);
  if (! iterable) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing iterable");
    return EVAL_RAISE();
  }

  /* Derive break target from the extent table (pop_pc + 1) */
  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t pc = KosEvalState_ACTING_PC(es);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_FOREACH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_FOREACH: no FOREACH extent at this PC");
    return EVAL_RAISE();
  }

  KosExtentDesc *const ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: invalid extent");
    return EVAL_RAISE();
  }
  uint32_t break_pc = ext->pop_pc + 1u;

  /* Create iterator from iterable (sync or async) */
  PyObject *iter = kos_iter_acquire(iterable);
  Py_DECREF(iterable);
  if (! iter)
    return EVAL_RAISE();

  /* Push region */
  KosDynNode *region = (KosDynNode *) kos_dyn_push_foreach(
    es, break_pc, continue_pc);
  if (! region) {
    Py_DECREF(iter);
    PyErr_NoMemory();
    return EVAL_RAISE();
  }

  KosDynForeach_ITER((KosDynForeach *) region) = iter;
  KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 0;

  return EVAL_NORMAL();
}


// The end.
