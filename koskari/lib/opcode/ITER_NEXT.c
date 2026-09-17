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
  ITER_NEXT: region-based loop step; arg = else PC. One iteration
  request against the FOREACH node's armed record -- provider, python
  async, and sync iterators all step through KosIterRequest_Next.
  Branch to else_pc on exhaustion. Must be inside PUSH_FOREACH block.
  Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  KosDynNode *region = kos_dyn_find_loop(es);

  if (unlikely(! region || ! KosDynNode_IS_FOREACH(region))) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    RESULT_RAISE();
  }

  /* The else pc is armed before the step so a delivery or a raise-walk
     exhaustion conversion lands on the right resume point. */
  KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) region) =
      KosOp_ARG(insn);

  {
    PyObject *out = NULL;

    switch (KosIterRequest_Next(es, region, NULL, &out)) {
      case KOS_IREQ_DELIVERED:
        if (unlikely(KosEvalState_StackPUSH(es, out) < 0))
          RESULT_RAISE();
        RESULT_NEXT(es);

      case KOS_IREQ_EXHAUSTED:
        Py_XDECREF(out);
        KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
        RESULT_JUMP_AT(es, KosOp_TARGET(insn));

      case KOS_IREQ_CONTINUES: {
        /* A frame the install left current is threaded into only when
           it can be: a code-less native frame takes the entry
           sequence, and a frame parked inside a yield-from runs that
           node's phase resume first. */
        RESULT_RELAY(es, kos_iter_installed(es));
      }

      case KOS_IREQ_EXIT:
        RESULT_AWAIT(es, out);

      case KOS_IREQ_ERROR:
      default:
        RESULT_RAISE();
    }
  }
}


// The end.
