/* Included by dynchain.c — dyn_prompt */
/* === KosDynPrompt implementation === */


/**
   Allocate PROMPT delimiter: barrier + stack block pairing in install. Callers:
   kos_dyn_push_prompt.
*/
KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent,
                   KOS_DYN_CAP_BARRIER
                   | KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynPrompt_ID(prompt) = prompt_id;
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  return prompt;
}


/**
   Clone prompt id for captured segments; pending shift wrapper not cloned.
*/
static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_PROMPT, NULL, KosDynNode_CAPS(prompt));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(prompt) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(prompt);
  KosDynPrompt_ID(clone) = KosDynPrompt_ID(prompt);
  KosDynPrompt_PENDING_SHIFT_WRAPPER(clone) = NULL;
  return clone;
}


/**
   DECREF pending wrapper; return node to freelist.
*/
void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  Py_XDECREF(KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt));
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  kos_dynnode_return_to_freelist(prompt);
}


/**
   Push prompt via kos_dyn_push (install pushes stack block on frame). Callers:
   opcode PUSH_PROMPT.
*/
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynprompt_new(KosContext_DYN_TOP(ctx), prompt_id);
  if (! prompt)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) prompt) < 0) {
    kos_dynprompt_free(prompt);
    return NULL;
  }

  return prompt;
}


/**
   Run uninstall handler (drop block), unlink, free. Used when popping prompt
   without full kos_dyn_uninstall (exception_unwind, unwind walks).
*/
void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    return;
  }

  kos_dyn_uninstall_handler(ctx, (KosDynNode *) prompt,
                            KOS_XFER_EVENT_NONE, NULL);

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(prompt);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynprompt_free(prompt);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PROMPT: visit pending shift wrapper if any.
*/
static int dynnode_traverse_prompt(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynPrompt_PENDING_SHIFT_WRAPPER((KosDynPrompt *) node));
  return 0;
}

