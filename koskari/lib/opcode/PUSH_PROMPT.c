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
   PUSH_PROMPT: arg = constant index of prompt_id. Push a delimited prompt
   onto the dynamic chain for shift/reset. The region's closing pc is
   resolved from the PROMPT extent here, once, and stored on the node;
   POP_SHIFT reads the answer instead of re-deriving it from a pc that
   slice execution has long since moved.
*/

KOS_OP_HANDLER(PUSH_PROMPT) {
  uint32_t prompt_id;
  uint32_t pop_pc;

  if (get_prompt_id_from_insn(es, insn, &prompt_id) < 0)
    RESULT_RAISE();

  {
    PyObject *code = (PyObject *) KosEvalState_CurrentCode(es);
    uint32_t eid = kos_extent_find(code, KosEvalState_ACTING_PC(es),
                                   KOS_EXT_PROMPT);
    KosExtentDesc *extent;

    if (eid == KOS_EXTENT_NONE) {
      PyErr_SetString(PyExc_RuntimeError,
                      "PUSH_PROMPT: no PROMPT extent at pc");
      RESULT_RAISE();
    }
    extent = kos_extent_desc(code, eid);
    if (! extent)
      RESULT_RAISE();
    pop_pc = extent->pop_pc;
  }

  if (! kos_dyn_push_prompt(es, prompt_id, pop_pc))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
