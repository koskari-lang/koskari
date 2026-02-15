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
   SHIFT: arg = constant index of prompt_id. Capture the continuation up to
   that prompt, unwind the dynamic chain to the prompt, push a new frame for
   the prompt's handler, and push the continuation (as a Python object) on
   the stack; handler runs with continuation at TOS.
*/

KOS_OP_HANDLER(SHIFT) {
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  if (get_prompt_id_from_insn(CURRENT_CODE(ctx), insn, &prompt_id) < 0)
    return -1;

  SAYF("[eval] SHIFT prompt_id=%u\n", (unsigned) prompt_id);
  if (kos_shift_capture(ctx, prompt_id, &k) < 0) {
    SAY("[eval] SHIFT capture failed\n");
    return -1;
  }

  KosDynPrompt *prompt = kos_find_prompt(ctx, prompt_id);
  if (! prompt) {
    kos_continuation_free(k);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt not found");
    return -1;
  }

  SAYF("[eval] SHIFT unwinding to prompt, then handler_pc=%u\n",
       (unsigned) KosDynPrompt_HANDLER_PC(prompt));
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    if (kos_dyn_pop_top(ctx) != 0) {
      kos_continuation_free(k);
      return -1;
    }
  }

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return -1;
  }
  if (! KosDynPrompt_HANDLER_CODE(prompt)) {
    Py_DECREF(wrapper);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt has no handler");
    return -1;
  }

  PyObject *ns = PyDict_New();
  if (! ns) {
    Py_DECREF(wrapper);
    return -1;
  }
  KosDynFrame *handler_frame = kos_dyn_push_frame(ctx, KosDynPrompt_HANDLER_CODE(prompt), ns);
  Py_DECREF(ns);
  if (! handler_frame) {
    Py_DECREF(wrapper);
    return -1;
  }

  CURRENT_PC(ctx) = KosDynPrompt_HANDLER_PC(prompt);
  return push_result(ctx, wrapper);
}
