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
   PUSH_SHIFT: arg = constant index of prompt_id. Capture the continuation
   from POP_SHIFT+1 to POP_PROMPT (via extent), unwind to the matching
   prompt, push a new frame for the shift body, push k on stack, and
   enter the shift block (first instruction = push_pc + 1).
*/

KOS_OP_HANDLER(PUSH_SHIFT) {
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  uint32_t resume_pc;
  uint32_t shift_body_pc;

  if (get_prompt_id_from_insn(CURRENT_CODE(ctx), insn, &prompt_id) < 0)
    return -1;

  resume_pc = kos_shift_ic_for((PyObject *) CURRENT_CODE(ctx),
                               CURRENT_ACTING_PC(ctx));
  if (resume_pc == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
    return -1;
  }

  SAYF("[eval] PUSH_SHIFT prompt_id=%u resume_pc=%u\n",
       (unsigned) prompt_id, (unsigned) resume_pc);
  if (kos_shift_capture(ctx, prompt_id, resume_pc, &k) < 0) {
    SAY("[eval] PUSH_SHIFT capture failed\n");
    return -1;
  }

  KosDynPrompt *prompt = kos_find_prompt(ctx, prompt_id);
  if (! prompt) {
    kos_continuation_free(k);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: prompt not found");
    return -1;
  }

  SAY("[eval] PUSH_SHIFT unwinding to prompt");
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    int pop_res = kos_dyn_pop_top(ctx, out_ev);
    if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
      return 0;
    if (pop_res != 0) {
      kos_continuation_free(k);
      return -1;
    }
  }

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return -1;
  }

  PyObject *ns = PyDict_New();
  if (! ns) {
    Py_DECREF(wrapper);
    return -1;
  }
  shift_body_pc = CURRENT_ACTING_PC(ctx) + 1;
  KosDynFrame *handler_frame = kos_dyn_push_frame(ctx, CURRENT_CODE(ctx), ns);
  Py_DECREF(ns);
  if (! handler_frame) {
    Py_DECREF(wrapper);
    return -1;
  }

  KosDynFrame_PC(handler_frame) = shift_body_pc;
  return push_result(ctx, wrapper);
}
