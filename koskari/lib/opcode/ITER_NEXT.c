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

/**
   ITER_NEXT over a flattened Koskari async gen: arm the FOREACH step
   phase and attach the gen frame.  Out of line — this runs once per
   flattened agen step, never on the sync iteration fast path.
*/
static KOS_NOINLINE KosOpResult iter_next_step_kos_agen(
    KosEvalState *es,
    KosOp *insn,
    KosDynNode *region,
    KosPyAsyncGen *agen) {

  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return EVAL_RAISE();
  }
  if (agen->closed || ! agen->ctx) {
    KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
    KosEvalState_SET_PC(es, KosOp_ARG(insn));
    return EVAL_NORMAL();
  }
  agen->in_progress = 1;
  KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) region) = KosOp_ARG(insn);
  kos_dyn_foreach_set_iter_step_phase(
      (KosDynForeach *) region, KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN);
  if (op_attach_koskari_agen_frame_for_iter_next(es, agen, region) < 0) {
    agen->in_progress = 0;
    kos_dyn_foreach_set_iter_step_phase(
        (KosDynForeach *) region, KOS_FOREACH_ITER_STEP_IDLE);
    return EVAL_RAISE();
  }
  return EVAL_NORMAL();
}


KOS_OP_HANDLER(ITER_NEXT) {
  KosDynNode *region = kos_find_node_for_loop(es);
  if (! region || ! KosDynNode_IS_FOREACH(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    return EVAL_RAISE();
  }

  PyObject *iter = KosDynForeach_ITER((KosDynForeach *) region);
  if (unlikely(KosPyAsyncGen_Check(iter)))
    return iter_next_step_kos_agen(es, insn, region, (KosPyAsyncGen *) iter);

  PyObject *value = NULL;
  PyObject *_await = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(es, &_await, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosEvalState_StackPushRef(es, value) < 0) {
        Py_DECREF(value);
        return EVAL_RAISE();
      }
      Py_DECREF(value);
      return EVAL_NORMAL();
    case GET_NEXT_EXHAUSTED:
      KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
      KosEvalState_SET_PC(es, KosOp_ARG(insn));
      return EVAL_NORMAL();
    case GET_NEXT_SUSPEND:
      return EVAL_AWAIT(_await);
    case GET_NEXT_ERROR:
      return EVAL_RAISE();
    default:
      return EVAL_RAISE();
  }
}


// The end.
