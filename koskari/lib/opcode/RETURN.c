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
  RETURN: arg = 0 (None), 1 (single value), or N (tuple of N values).
  Pop the return value(s) from the stack; eval_event_return delivers
  KOS_RES_RETURN at the driver boundary.
*/
KOS_OP_HANDLER(RETURN) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  /* === Phase 1: Pop return value from stack ===
     opa=0 => Py_None, opa=1 => single value, opa=N => tuple of N values */
  SAYF("[RETURN] opa=%u frame=%p stack=%p len=%zu (pc advanced to %u)",
       opa, (void *) KosEvalState_FRAME(es),
       (void *) KosEvalState_StackHead(es),
       KosEvalState_StackLen(es),
       (unsigned) KosEvalState_PC(es));

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosEvalState_StackPopRef(es);
    if (unlikely(! val)) {
      SAYF("[RETURN] pop underrun: stack=%p len=%zu",
           (void *) KosEvalState_StackHead(es),
           (unsigned long) KosEvalState_StackLen(es));
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      RESULT_RAISE();
    }

  } else {
    val = KosEvalState_StackPopTail(es, (size_t) opa);
    if (unlikely(! val)) {
      SAY("[eval] RETURN PopTail underrun\n");
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      RESULT_RAISE();
    }
  }

  /* === Phase 2: deliver ===
     The three shapes that need no walk thread straight on: a plain
     bytecode parent takes the value on its stack and continues at its
     saved pc (the bytecode frame's own resume); a discardable extent
     over such a parent likewise; a parent waiting on a resume takes
     the value by a tail call into that resume. Anything else -- a
     try/with/wind above the frame, a generator or iteration-node
     parent, a delimited-invoke bookmark, an unsettled value -- signals
     RETURN and the trampoline's return machinery walks it. */
  {
    KosDynNode *child, *parent;
    KosDynFrame *ft;
    KosOpResult r;

    if (likely(kos_return_is_plain(es, &child, &parent))) {
      KOS_TRACE_POST_OP_WRAP(plain_return,
          r = eval_ng_plain_return(es, child, parent, val));
      if (likely(r.kind == KOS_RES_DISPATCH))
        RESULT_NEXT(es);
      return r;
    }

    if (kos_return_is_extent_plain(es, &child, &parent, &ft)) {
      KOS_TRACE_POST_OP_WRAP(plain_return,
          r = eval_ng_extent_return(es, child, parent, ft, val));
      if (likely(r.kind == KOS_RES_DISPATCH))
        RESULT_NEXT(es);
      return r;
    }

    if (kos_return_is_resume(es, &parent)) {
      KosResumeFn resume;
      void *state;
      int took = kos_return_take_resume(es, parent, val, &resume, &state);

      if (took > 0) {
        RESUME_HOP(resume, es, state, val);
      }
      if (unlikely(took < 0))
        RESULT_RAISE();
    }
  }

  return EVAL_RETURN(val);
}


// The end.
