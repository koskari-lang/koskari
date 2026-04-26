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
   ITER_NEXT: region-based loop step; arg = else PC. Same async-gen vs
   Python async vs sync iterator dispatch as NEXT. Branch to else_pc on
   exhaustion. Must be inside PUSH_FOREACH block. Auto-await.
*/

KOS_OP_HANDLER(ITER_NEXT) {
  KosContext *ctx = KosEvalState_CONTEXT(es); /* agen attach paths */
  KosDynNode *region = kos_find_node_for_loop(es);
  if (! region || ! KosDynNode_IS_FOREACH(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    return KOS_OP_POST_ERR();
  }

  PyObject *iter = KosDynForeach_ITER((KosDynForeach *) region);
  if (KosPyAsyncGen_Check(iter)) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) iter;
    if (agen->in_progress) {
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator already being iterated");
      return KOS_OP_POST_ERR();
    }
    if (agen->closed || ! agen->ctx) {
      KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
      KosEvalState_SET_PC(es, KosOp_ARG(insn));
      return KOS_OP_RUN();
    }
    agen->in_progress = 1;
    KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) region) = KosOp_ARG(insn);
    KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) region) =
        KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN;
    if (op_attach_koskari_agen_frame_for_iter_next(es, agen, region) < 0) {
      agen->in_progress = 0;
      KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) region) =
          KOS_FOREACH_ITER_STEP_IDLE;
      return KOS_OP_POST_ERR();
    }
    return KOS_OP_RUN();
  }

  PyObject *value = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(ctx, out_ev, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosEvalState_StackPushRef(es, value) < 0) {
        Py_DECREF(value);
        return KOS_OP_POST_ERR();
      }
      Py_DECREF(value);
      return KOS_OP_RUN();
    case GET_NEXT_EXHAUSTED:
      KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
      KosEvalState_SET_PC(es, KosOp_ARG(insn));
      return KOS_OP_RUN();
    case GET_NEXT_SUSPEND:
      return KOS_OP_POST_SUSPEND();
    case GET_NEXT_ERROR:
      return KOS_OP_POST_ERR();
    default:
      return KOS_OP_POST_ERR();
  }
}
