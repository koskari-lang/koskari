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


/* Install a continue's result in a region's result home. A NULL result
   means None: the home already holds None on every pass but the first
   after a valued continue, so that pass alone touches a refcount. */
static inline void continue_store_result(PyObject **home, PyObject *result) {
  if (result)
    Py_XSETREF(*home, result);
  else if (*home != Py_None)
    Py_XSETREF(*home, Py_NewRef(Py_None));
}


/**
  CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
  Jump to innermost loop/foreach continue target. Updates the region result
  and clears the loop segment before jumping. Raises RuntimeError if
  no LOOP/FOREACH region on the chain.
*/
KOS_OP_HANDLER(CONTINUE) {
  KOS_SYNC_PC(es, insn);
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;   /* NULL stands for None until it is needed */

  if (opa == 1) {
    result = KosEvalState_StackPopRef(es);
    if (unlikely(! result)) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
      RESULT_RAISE();
    }

  } else if (opa > 1) {
    result = KosEvalState_StackPopTail(es, (size_t) opa);
    if (unlikely(! result)) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
      RESULT_RAISE();
    }
  }

  /* Region on top: the general walk below would find this node first,
     prove the frame its ancestor, and unwind nothing, so its step 4
     runs here directly. The proof is the register invariant: FRAME is
     the first frame node below DYN_TOP, so a region at DYN_TOP has it
     as its nearest frame ancestor. A FOREACH region's continue target
     is its ITER_NEXT row; the step runs here as well, with the region
     in hand, so the back-edge is one dispatch. Anything above the
     region takes the walk, where the offer and the suspension handling
     live. */
  {
    KosDynNode *region = KosEvalState_DYN_TOP(es);
    KosDynFrame *frame = KosEvalState_FRAME(es);

#if KOS_EVAL_SAFETYNET
    if (region && frame && ! KosDynNode_IS_FRAME(region)) {
      KosDynNode *n = KosDynNode_PARENT(region);
      while (n && ! KosDynNode_IS_FRAME(n))
        n = KosDynNode_PARENT(n);
      if (n != (KosDynNode *) frame) {
        Py_XDECREF(result);
        PyErr_SetString(PyExc_SystemError,
                        "CONTINUE: FRAME is not the nearest frame below"
                        " DYN_TOP");
        RESULT_RAISE();
      }
    }
#endif

    if (likely(frame && region) && KosDynNode_IS_LOOP(region)
        && KosDynLoop_ENABLED(region)) {
      continue_store_result(&KosDynLoop_RESULT(region), result);
      kos_stack_clear_fast(KosEvalState_BLOCK(es));
      KosEvalState_SET_PC(es, KosDynLoop_CONTINUE_PC(region));
      RESULT_NEXT_POLL(es);
    }

    if (likely(frame && region) && KosDynNode_IS_FOREACH(region)
        && KosDynForeach_ENABLED(region)) {
      uint32_t cpc = KosDynForeach_CONTINUE_PC(region);
      KosOp *step = &KosEvalState_OPS(es)[cpc];
      PyObject *out = NULL;

      continue_store_result(&KosDynForeach_RESULT(region), result);
      kos_stack_clear_fast(KosEvalState_BLOCK(es));

      if (unlikely(KosOp_OP(step) != KOS_OP_ITER_NEXT)) {
        KosEvalState_SET_PC(es, cpc);
        RESULT_NEXT_POLL(es);
      }

      /* As ITER_NEXT: the else pc is recorded before the step, and the
         live pcs placed as a dispatch of that row would place them, so
         a suspended step resumes past it. */
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
                                            result ? result
                                                   : Py_NewRef(Py_None));
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
