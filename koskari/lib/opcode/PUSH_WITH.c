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
   PUSH_WITH: arg = constant pool index (name or None). Single-shot context
   manager. Stack before: [..., mgr]. Pop mgr, resolve enter/exit, push block,
   push provisional KOS_DYN_WITH node (single-shot, push = link only), then wind.
   Wind may suspend (async enter); otherwise result is bound to name.
*/

KOS_OP_HANDLER(PUSH_WITH) {
  PyObject *mgr = KosEvalState_StackPopRef(es);
  if (! mgr)
    RESULT_RAISE();

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  uint8_t self_bind = 0;
  if (resolve_context_enter_exit(mgr, &enter, &exit, &self_bind) < 0) {
    Py_DECREF(mgr);
    RESULT_RAISE();
  }

  /* the body resumes after the POP_WITH closing the extent */
  uint32_t pop_pc;
  if (kos_site_extent_close(es, insn, KOS_EXT_WITH, "PUSH_WITH", &pop_pc) < 0) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    RESULT_RAISE();
  }

  PyObject *name_obj = KosOp_OBJ(insn);
  Py_XINCREF(name_obj);

  KosDynWith *with_node = kos_dyn_push_with_provisional(
      es, mgr, enter, exit, self_bind, name_obj, pop_pc + 1u,
      KOS_DYN_WITH_SHOT_SINGLE_ACTIVE);
  if (! with_node) {
    Py_DECREF(name_obj);
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    RESULT_RAISE();
  }

  KosDynFrame *const caller = KosEvalState_FRAME(es);
  KosOpResult wr = kos_dyn_extent_enter(es, (KosDynNode *) with_node);

  if (wr.kind == KOS_RES_RAISE) {
    kos_dyn_pop_with(es, with_node);
    RESULT_RAISE();
  }

  /* AWAIT: __enter__ returned an awaitable and the node stays pending;
     the revive settles it with the awaited value and continues into
     the body. */
  if (wr.kind == KOS_RES_AWAIT)
    return wr;

  /* REPROLOGUE: __enter__ is a KosCallable whose frame is installed.
     Do not touch the pc here -- the current frame is now the callee's,
     so threading on runs it; this frame's pc already sits past
     PUSH_WITH, so the with body runs once the callee returns and its
     return settles the node. A plan armed by the install is the
     prologue's to drain. */
  if (wr.kind != KOS_RES_NORMAL) {
    if (wr.kind == KOS_RES_REPROLOGUE
        && KosEvalState_FRAME(es) != caller
        && ! KosEvalState_GOAL(es))
      RESULT_CONTINUE(es);
    return wr;
  }

  RESULT_NEXT(es);
}


// The end.
