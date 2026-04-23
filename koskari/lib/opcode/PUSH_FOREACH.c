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
   (pop_pc + 1), not from the instruction argument. Stack: [loop_result, iterable].
   Pops iterable and loop_result, creates iterator, stores both in region,
   pushes FOREACH region and stack block. Continue target = next insn.
*/

KOS_OP_HANDLER(PUSH_FOREACH) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  uint32_t continue_pc = CURRENT_PC(ctx);  /* already advanced by drive loop */

  /* Pop iterable (TOS) and loop_result from stack */
  PyObject *iterable = KosEvalState_StackPopRef(es);
  if (! iterable) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing iterable");
    return KOS_OP_POST_ERR();
  }
  PyObject *loop_result = KosEvalState_StackPopRef(es);
  if (! loop_result) {
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing loop_result");
    return KOS_OP_POST_ERR();
  }

  /* Derive break target from the extent table (pop_pc + 1) */
  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = CURRENT_ACTING_PC(ctx);
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_FOREACH);
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(loop_result);
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_FOREACH: no FOREACH extent at this PC");
    return KOS_OP_POST_ERR();
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    Py_DECREF(loop_result);
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: invalid extent");
    return KOS_OP_POST_ERR();
  }
  uint32_t break_pc = ext->pop_pc + 1u;

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
    return KOS_OP_POST_ERR();
  }

  /* Push region */
  KosDynNode *region = (KosDynNode *) kos_dyn_push_foreach(es,                                              break_pc, continue_pc);
  if (! region) {
    Py_DECREF(iter);
    Py_DECREF(loop_result);
    PyErr_NoMemory();
    return KOS_OP_POST_ERR();
  }

  /* Store result and iterator in region (takes ownership) */
  KosDynForeach_RESULT((KosDynForeach *) region) = loop_result;
  KosDynForeach_ITER((KosDynForeach *) region) = iter;
  KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 0;

  KosDynNode_STACK(region) = KosStackBlock_NewEmpty();
  if (! KosDynNode_STACK(region)) {
    if (kos_dyn_abort_push_region(es, region) < 0) {
      PyErr_Clear();
      PyErr_SetString(PyExc_RuntimeError,
                      "PUSH_FOREACH: unexpected region kind during abort");
    }
    return KOS_OP_POST_ERR();
  }
  KosEvalState_RefreshDynCaches(es);

  (void) out_ev;
  return KOS_OP_RUN();
}
