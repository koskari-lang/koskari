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


/* The continue's result as arg names it: one value popped, none for
   None (NULL), a tuple of several. NULL with an error set on underrun;
   the caller tells the two NULLs apart by arg. */
static inline PyObject *continue_pop_result(KosContext *ctx, unsigned opa) {
  if (likely(opa == 1)) {
    PyObject *result = KosContext_StackPopRef(ctx);
    if (unlikely(! result))
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
    return result;
  }
  if (opa == 0)
    return NULL;
  {
    PyObject *result = KosContext_StackPopTail(ctx, (size_t) opa);
    if (unlikely(! result))
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
    return result;
  }
}


/**
  CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
  Store the result on the innermost enabled LOOP or FOREACH region,
  clear the region's segment, jump to its continue target, and poll.
  With the region at dyn_top that is the whole handler; anything above
  the region takes the unwind walk. Raises RuntimeError if no enabled
  region is in the frame.
*/
KOS_OP_HANDLER(CONTINUE) {
  KOS_SYNC_PC(ctx, insn);
  KosDynNode *region = KosContext_DYN_TOP(ctx);
  PyObject *result = continue_pop_result(ctx, KosOp_ARG(insn));

  if (unlikely(! result && KosOp_ARG(insn)))
    RESULT_RAISE();

  if (likely(KosDynNode_HAS_CAP(region, KOS_DYN_CAP_LOOP_REGION))) {
    continue_store_result(&KosDynRegion_RESULT(region), result);
    kos_stack_clear_fast(KosContext_BLOCK(ctx));
    KosContext_SET_PC(ctx, KosDynRegion_CONTINUE_PC(region));
    RESULT_NEXT_POLL(ctx);
  }

  {
    /* the walk consumes result on every answer */
    KosOpResult wr = kos_dyn_unwind_to_loop(
        ctx, KOS_XFER_EVENT_CONTINUE,
        result ? result : Py_NewRef(Py_None));

    if (unlikely(wr.kind != KOS_RES_NORMAL))
      TAILCALL(kos_eval_relay, ctx, (void *) (uintptr_t) wr.kind, wr.val);
    RESULT_NEXT_POLL(ctx);
  }
}


// The end.
