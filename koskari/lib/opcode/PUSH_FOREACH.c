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
   PUSH_FOREACH: no arg. Break target is the pc after the POP_FOREACH
   closing the FOREACH extent, resolved once per site into the cache
   slot. Stack: [iterable].
   Pops iterable, creates iterator, stores it in region (result defaults
   to NULL), pushes FOREACH region (with its own segment). Continue target =
   next insn.
*/

KOS_OP_HANDLER(PUSH_FOREACH) {
  KOS_SYNC_PC(es, insn);
  uint32_t continue_pc = KosEvalState_PC(es);  /* next insn; fetch advanced */
  uint32_t pop_pc;

  PyObject *iterable = KosEvalState_StackPopRef(es);
  if (! iterable) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing iterable");
    RESULT_RAISE();
  }

  /* break lands after the POP_FOREACH that closes the extent */
  if (kos_site_extent_close(es, insn, KOS_EXT_FOREACH, "PUSH_FOREACH",
                            &pop_pc) < 0) {
    Py_DECREF(iterable);
    RESULT_RAISE();
  }

  /* Create iterator from iterable (sync or async) */
  PyObject *iter = kos_iter_acquire(iterable);
  Py_DECREF(iterable);
  if (! iter)
    RESULT_RAISE();

  /* Push region */
  KosDynNode *region = (KosDynNode *) kos_dyn_push_foreach(
    es, pop_pc + 1u, continue_pc);
  if (! region) {
    Py_DECREF(iter);
    PyErr_NoMemory();
    RESULT_RAISE();
  }

  KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 0;

  /* Set the request record for the loop's lifetime, handing it the
     iterator reference; per-step drives only touch the mode. */
  kos_dyn_iter_request_bind_iter(KosDynForeach_REQ(region), iter);

  RESULT_NEXT(es);
}


// The end.
