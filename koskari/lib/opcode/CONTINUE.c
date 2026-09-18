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
  CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
  Jump to innermost loop/foreach continue target. Updates the region result
  and clears the loop segment before jumping. Raises RuntimeError if
  no LOOP/FOREACH region on the chain.
*/
KOS_OP_HANDLER(CONTINUE) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  if (likely(opa == 0)) {
    result = Py_None;
    Py_INCREF(result);

  } else if (opa == 1) {
    result = KosEvalState_StackPopRef(es);
    if (unlikely(! result)) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
      RESULT_RAISE();
    }

  } else {
    result = KosEvalState_StackPopTail(es, (size_t) opa);
    if (unlikely(! result)) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
      RESULT_RAISE();
    }
  }

  /* Region on top: the general walk below would find this node first,
     prove the frame its ancestor by the same parent walk, and unwind
     nothing, so its step 4 runs here directly. A FOREACH region's
     continue target is its ITER_NEXT row; the step runs here as well,
     with the region in hand, so the back-edge is one dispatch. Anything
     above the region takes the walk, where the offer and the
     suspension handling live. */
  {
    KosDynNode *region = KosEvalState_DYN_TOP(es);
    KosDynFrame *frame = KosEvalState_FRAME(es);
    KosDynNode *n = region ? KosDynNode_PARENT(region) : NULL;

    while (n && ! KosDynNode_IS_FRAME(n))
      n = KosDynNode_PARENT(n);

    if (likely(frame && n == (KosDynNode *) frame)
        && KosDynNode_IS_LOOP(region)) {
      Py_XDECREF(KosDynLoop_RESULT(region));
      KosDynLoop_RESULT(region) = result;
      if (unlikely(KosEvalState_StackClear(es) < 0))
        RESULT_RAISE();
      KosEvalState_SET_PC(es, KosDynLoop_CONTINUE_PC(region));
      RESULT_NEXT_POLL(es);
    }

    if (likely(frame && n == (KosDynNode *) frame)
        && KosDynNode_IS_FOREACH(region)) {
      uint32_t cpc = KosDynForeach_CONTINUE_PC(region);
      KosOp *step = &KosEvalState_OPS(es)[cpc];
      PyObject *out = NULL;

      Py_XDECREF(KosDynForeach_RESULT(region));
      KosDynForeach_RESULT(region) = result;
      if (unlikely(KosEvalState_StackClear(es) < 0))
        RESULT_RAISE();

      if (unlikely(KosOp_OP(step) != KOS_OP_ITER_NEXT)) {
        KosEvalState_SET_PC(es, cpc);
        RESULT_NEXT_POLL(es);
      }

      /* As ITER_NEXT: else pc armed before the step, and the live pcs
         placed as a dispatch of that row would place them, so a
         suspended step resumes past it. */
      KosDynForeach_ITER_NEXT_ELSE_PC(region) = KosOp_ARG(step);
      KosEvalState_ACTING_PC(es) = cpc;
      KosEvalState_SET_PC(es, cpc + 1u);

      switch (KosIterRequest_Next(es, region, NULL, &out)) {
        case KOS_IREQ_DELIVERED:
          if (unlikely(KosEvalState_StackPUSH(es, out) < 0))
            RESULT_RAISE();
          RESULT_NEXT_POLL(es);

        case KOS_IREQ_EXHAUSTED:
          Py_XDECREF(out);
          KosDynForeach_EXHAUSTED(region) = 1;
          RESULT_JUMP_AT(es, KosOp_TARGET(step));

        case KOS_IREQ_CONTINUES:
          RESULT_RELAY(es, kos_iter_installed(es));

        case KOS_IREQ_EXIT:
          RESULT_AWAIT(es, out);

        case KOS_IREQ_ERROR:
        default:
          RESULT_RAISE();
      }
    }
  }

  {
    /* the walk consumes result on every answer */
    KosOpResult wr = kos_dyn_unwind_to_loop(es, KOS_XFER_EVENT_CONTINUE,
                                            result);
    if (unlikely(wr.kind != KOS_RES_NORMAL))
      return wr;

    /* The loop's back-edge is the one cycle the compiler closes with
       an instruction, so it is where a running loop cooperates: a GIL
       request or a pending signal is polled here before the next
       iteration. */
    RESULT_NEXT_POLL(es);
  }
}


// The end.
