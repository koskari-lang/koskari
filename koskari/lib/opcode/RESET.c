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
   RESET: arg = extra index. Schema COUNT (0 for the default prompt
   tag, or 1 for a tag popped from the stack, where a popped None is
   the default tag again), CONST (the body's FunctionSpec, a function
   of no arguments).

   Build the body over the live namespace, push a prompt carrying the
   tag, and call the body above it. The body's return, or the return of
   a shift's handler called above the same prompt, takes the prompt
   with it and lands where this instruction's value goes, as any
   callee's return does; this frame goes on at the next instruction.
*/

KOS_OP_HANDLER(RESET) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *tag;
  PyObject *body;
  KosDynPrompt *prompt;
  KosOpResult r;

  if (pop_prompt_tag(ctx, (unsigned) KosExtra_IMM(row, 0), &tag) < 0)
    RESULT_RAISE();

  body = kos_eval_build_function(KosExtra_OBJ(row, 1), NULL, NULL,
                                 CURRENT_NS());
  if (! body) {
    Py_XDECREF(tag);
    RESULT_RAISE();
  }

  /* the prompt takes its own reference; NULL is the default tag */
  prompt = kos_dyn_push_prompt(ctx, tag);
  Py_XDECREF(tag);
  if (! prompt) {
    Py_DECREF(body);
    RESULT_RAISE();
  }

  r = KosContext_Call(ctx, body, NULL, 0, NULL, 0, NULL, NULL);
  Py_DECREF(body);

  switch (r.kind) {
    case KOS_RES_RETURN:
      /* a value in hand: the body ended with no frame of its own */
      if (kos_dyn_pop_prompt(ctx, prompt) < 0) {
        Py_DECREF(r.val);
        RESULT_RAISE();
      }
      RESULT_RELAY(ctx, KosContext_PushOrAwait(ctx, r.val));
    case KOS_RES_RAISE:
      RESULT_RAISE();
    case KOS_RES_AWAIT:
    case KOS_RES_FATAL:
      RESULT_RELAY(ctx, r);
    default:
      /* the body's frame is installed above the prompt */
      RESULT_REPROLOGUE(ctx);
  }
}


// The end.
