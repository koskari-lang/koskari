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
   PUSH_FOREACH: arg = break target PC. Stack: [loop_result, iterable].
   Pops iterable and loop_result, creates iterator, stores both in region,
   pushes FOREACH region and stack block. Continue target = next insn.
*/

KOS_OP_HANDLER(PUSH_FOREACH) {
  uint32_t break_pc = insn->arg;
  uint32_t continue_pc = CURRENT_PC(ctx);  /* already advanced by drive loop */

  /* Pop iterable (TOS) and loop_result from stack */
  PyObject *iterable = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! iterable) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing iterable");
    return -1;
  }
  PyObject *loop_result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! loop_result) {
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing loop_result");
    return -1;
  }

  /* Create iterator from iterable (sync or async) */
  PyObject *iter = NULL;
  if (KosPyAsyncGen_Check(iterable)) {
    iter = iterable;
    Py_INCREF(iter);
    Py_DECREF(iterable);
  } else if (Py_TYPE(iterable)->tp_as_async &&
             Py_TYPE(iterable)->tp_as_async->am_aiter) {
    iter = Py_TYPE(iterable)->tp_as_async->am_aiter(iterable);
    Py_DECREF(iterable);
  } else {
    iter = PyObject_GetIter(iterable);
    Py_DECREF(iterable);
  }
  if (! iter) {
    Py_DECREF(loop_result);
    return -1;
  }

  /* Push region */
  KosDynRegion *region = kos_dyn_push_region(ctx, KOS_REGION_FOREACH,
                                             break_pc, continue_pc);
  if (! region) {
    Py_DECREF(iter);
    Py_DECREF(loop_result);
    PyErr_NoMemory();
    return -1;
  }

  /* Store result and iterator in region (takes ownership) */
  KosDynRegion_RESULT(region) = loop_result;
  KosDynRegion_ITER(region) = iter;
  KosDynRegion_EXHAUSTED(region) = 0;

  /* Push stack block for iteration temporaries */
  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    kos_dyn_pop_region(ctx, region);
    return -1;
  }

  (void) out_ev;
  return 0;
}
