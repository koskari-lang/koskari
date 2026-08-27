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
  ITER_NEXT over a KosIter provider: arm the FOREACH step phase and
  dispatch the provider's step slot.  Out of line -- this runs once per
  flattened provider step, never on the sync iteration fast path.
*/
static KOS_NOINLINE KosOpResult iter_next_step_kos_iter(
    KosEvalState *es,
    KosOp *insn,
    KosDynNode *region,
    PyObject *iter,
    KosIterSlots *islots) {

  KosDynForeach *fe = (KosDynForeach *) region;
  PyObject *out = NULL;

  /* Arm the boundary before stepping: an INSTALLED drive delivers
     through it, and the provider never touches site state. */
  KosDynForeach_ITER_NEXT_ELSE_PC(fe) = KosOp_ARG(insn);
  kos_dyn_foreach_set_iter_step_phase(
      fe, KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN);

  switch (kos_iter_drive_step(es, iter, islots, NULL, NULL, NULL, NULL, &out, NULL)) {
    case KID_INSTALLED:
      return EVAL_NORMAL();

    case KID_INSTALLED_VALUE:
      kos_dyn_foreach_set_iter_step_phase(
          fe, KOS_FOREACH_ITER_STEP_AWAITING_KOS_VALUE);
      return EVAL_NORMAL();

    case KID_INSTALLED_RESUME:
      kos_dyn_foreach_set_iter_step_phase(
          fe, KOS_FOREACH_ITER_STEP_AWAITING_KOS_ITER);
      return EVAL_NORMAL();

    case KID_VALUE:
      kos_dyn_foreach_set_iter_step_phase(fe, KOS_FOREACH_ITER_STEP_IDLE);
      if (KosEvalState_StackPUSH(es, out) < 0)
        return EVAL_RAISE();
      return EVAL_NORMAL();

    case KID_EXHAUSTED:
      kos_dyn_foreach_set_iter_step_phase(fe, KOS_FOREACH_ITER_STEP_IDLE);
      Py_XDECREF(out);
      KosDynForeach_EXHAUSTED(fe) = 1;
      KosEvalState_SET_PC(es, KosOp_ARG(insn));
      return EVAL_NORMAL();

    case KID_PARKED:
      /* Provider park under the re-entry contract: the awaited value
         feeds back into ki_step at revive. */
      kos_dyn_foreach_set_iter_step_phase(
          fe, KOS_FOREACH_ITER_STEP_AWAITING_KOS_ITER);
      return EVAL_AWAIT(out);

    case KID_ERROR:
    default:
      kos_dyn_foreach_set_iter_step_phase(fe, KOS_FOREACH_ITER_STEP_IDLE);
      return EVAL_RAISE();
  }
}


/**
  ITER_NEXT: region-based loop step; arg = else PC. Same KosIter vs
  Python async vs sync iterator dispatch as NEXT. Branch to else_pc on
  exhaustion. Must be inside PUSH_FOREACH block. Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  KosDynNode *region = kos_dyn_find_loop(es);

  if (unlikely(! region || ! KosDynNode_IS_FOREACH(region))) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    return EVAL_RAISE();
  }

  PyObject *iter = KosDynForeach_ITER((KosDynForeach *) region);
  KosIterSlots *islots;
  if (unlikely(KosIter_CheckSlots(iter, &islots)))
    return iter_next_step_kos_iter(es, insn, region, iter, islots);

  PyObject *value = NULL;
  PyObject *_await = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(es, &_await, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (unlikely(KosEvalState_StackPushRef(es, value) < 0)) {
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
      /* The step is parked on its await: arm the else pc and the
         in-flight phase so the awaited value revives through the
         boundary and StopAsyncIteration from the parked step converts
         to exhaustion (branch to else) at the raise walk. */
      KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) region) =
          KosOp_ARG(insn);
      kos_dyn_foreach_set_iter_step_phase(
          (KosDynForeach *) region, KOS_FOREACH_ITER_STEP_AWAITING_PY);
      return EVAL_AWAIT(_await);

    case GET_NEXT_ERROR:
      return EVAL_RAISE();

    default:
      return EVAL_RAISE();
  }
}


// The end.
